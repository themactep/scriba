/**
 * @file serprog_net.c
 * @brief flashrom's serprog protocol over TCP: the thingino backpack, or any
 *        serprog programmer on the network
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * serprog has no chip-select command: O_SPIOP is a whole transaction, CS low,
 * the bytes written, the bytes read back, CS high. scriba drives CS itself, so
 * the bytes written after cs_select are kept and go out with the next read, or
 * alone at cs_deselect, as one O_SPIOP. Every flash routine writes its command
 * before it reads within one select, which this relies on; a second read in the
 * same select is refused rather than sent without its command.
 *
 * S_PIN_STATE on is where the programmer takes the chip: a backpack powers it,
 * or parks the camera, then. A programmer that cannot answers NAK, and the
 * reason is in its own log or status page.
 *
 * Protocol: flashrom's Documentation/serprog/serprog-protocol.rst.
 */

#include <errno.h>
#include <stdint.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <unistd.h>

#include "serprog_net.h"
#include "spi_controller.h"

extern int debug_enabled;
extern int trace_enabled;

#define S_ACK          0x06
#define S_NAK          0x15

#define S_CMD_NOP      0x00
#define S_CMD_Q_IFACE  0x01
#define S_CMD_Q_CMDMAP 0x02
#define S_CMD_Q_PGMNAME 0x03
#define S_CMD_Q_BUSTYPE 0x05
#define S_CMD_Q_WRNMAXLEN 0x08
#define S_CMD_SYNCNOP  0x10
#define S_CMD_Q_RDNMAXLEN 0x11
#define S_CMD_S_BUSTYPE 0x12
#define S_CMD_O_SPIOP  0x13
#define S_CMD_S_SPI_FREQ 0x14
#define S_CMD_S_PIN_STATE 0x15

#define S_BUS_SPI      (1 << 3)

/* A backpack parking a camera takes a few seconds to hand the chip over, and a
 * 64 KB read at a slow clock a few more. */
#define IO_TIMEOUT_S   30

static char host[256];
static char port[16] = SERPROG_NET_PORT;
static int sock = -1;
static unsigned char cmdmap[32];
static unsigned long max_write, max_read;

/* What has been written since cs_select, and whether a read has gone out. */
static unsigned char *pending;
static size_t pending_len, pending_cap;
static int selected, read_sent;

int serprog_net_set_host(const char *spec)
{
	const char *at = spec;
	size_t len;

	if (*spec == '[') {
		const char *end = strchr(spec, ']');
		if (!end)
			return -1;
		at = spec + 1;
		len = (size_t)(end - at);
		if (end[1] == ':')
			snprintf(port, sizeof(port), "%s", end + 2);
		else if (end[1])
			return -1;
	} else {
		const char *colon = strchr(spec, ':');
		/* One colon is host:port; more is a bare IPv6 address. */
		if (colon && !strchr(colon + 1, ':')) {
			len = (size_t)(colon - spec);
			snprintf(port, sizeof(port), "%s", colon + 1);
		} else {
			len = strlen(spec);
		}
	}
	if (!len || len >= sizeof(host) || !port[0])
		return -1;
	memcpy(host, at, len);
	host[len] = '\0';
	return 0;
}

int serprog_net_has_host(void)
{
	return host[0] != '\0';
}

/* writev() takes a non-const iov_base but only reads through it. */
static void *out_buf(const void *p)
{
	return (void *)(uintptr_t)p;
}

/* Sends all of `iov`, which it advances in place as the kernel takes it. */
static int send_all(struct iovec *iov, int count)
{
	while (count > 0) {
		ssize_t n = writev(sock, iov, count);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "serprog: send: %s\n", strerror(errno));
			return -1;
		}
		while (count > 0 && (size_t)n >= iov->iov_len) {
			n -= (ssize_t)iov->iov_len;
			iov++;
			count--;
		}
		if (count > 0) {
			iov->iov_base = (char *)iov->iov_base + n;
			iov->iov_len -= (size_t)n;
		}
	}
	return 0;
}

static int recv_all(unsigned char *buf, size_t len)
{
	while (len) {
		ssize_t n = recv(sock, buf, len, 0);
		if (n == 0) {
			fprintf(stderr, "serprog: the programmer closed the connection\n");
			return -1;
		}
		if (n < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "serprog: receive: %s\n",
				errno == EAGAIN || errno == EWOULDBLOCK ? "timed out" : strerror(errno));
			return -1;
		}
		buf += n;
		len -= (size_t)n;
	}
	return 0;
}

static int supports(unsigned char cmd)
{
	return (cmdmap[cmd / 8] >> (cmd % 8)) & 1;
}

/* Sends `cmd` with its parameters; 0 when the programmer ACKs and `answer`
 * holds what followed, 1 on NAK, -1 when the connection failed. */
static int command(unsigned char cmd, const unsigned char *params, size_t plen,
		   unsigned char *answer, size_t alen)
{
	struct iovec iov[2] = {
		{ .iov_base = &cmd, .iov_len = 1 },
		{ .iov_base = out_buf(params), .iov_len = plen },
	};
	unsigned char status;

	if (send_all(iov, plen ? 2 : 1) || recv_all(&status, 1))
		return -1;
	if (status == S_NAK)
		return 1;
	if (status != S_ACK) {
		fprintf(stderr, "serprog: command 0x%02x answered 0x%02x\n", cmd, status);
		return -1;
	}
	return alen ? (recv_all(answer, alen) ? -1 : 0) : 0;
}

/* One transaction: CS low, `wlen` bytes out, `rlen` bytes in, CS high. */
static int spi_op(const unsigned char *wbuf, size_t wlen, unsigned char *rbuf, size_t rlen)
{
	unsigned char head[7] = {
		S_CMD_O_SPIOP,
		wlen & 0xff, (wlen >> 8) & 0xff, (wlen >> 16) & 0xff,
		rlen & 0xff, (rlen >> 8) & 0xff, (rlen >> 16) & 0xff,
	};
	struct iovec iov[2] = {
		{ .iov_base = head, .iov_len = sizeof(head) },
		{ .iov_base = out_buf(wbuf), .iov_len = wlen },
	};
	unsigned char status;

	if (wlen > max_write || rlen > max_read) {
		fprintf(stderr, "serprog: a %zu/%zu byte transaction is more than the programmer takes\n",
			wlen, rlen);
		return -1;
	}
	if (trace_enabled)
		printf("serprog: op out %zu%s%02x, in %zu\n", wlen, wlen ? " first " : "",
		       wlen ? wbuf[0] : 0, rlen);
	if (send_all(iov, wlen ? 2 : 1) || recv_all(&status, 1))
		return -1;
	if (status != S_ACK) {
		fprintf(stderr, "serprog: the programmer refused an SPI transaction (0x%02x)\n", status);
		return -1;
	}
	return rlen ? recv_all(rbuf, rlen) : 0;
}

static unsigned long u24(const unsigned char *b)
{
	unsigned long n = b[0] | (b[1] << 8) | ((unsigned long)b[2] << 16);
	return n ? n : 1ul << 24;
}

static int connect_to_host(void)
{
	struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
	struct addrinfo *res, *ai;
	struct timeval tv = { .tv_sec = IO_TIMEOUT_S };
	int one = 1;
	int err = getaddrinfo(host, port, &hints, &res);

	if (err) {
		fprintf(stderr, "serprog: %s: %s\n", host, gai_strerror(err));
		return -1;
	}
	for (ai = res; ai; ai = ai->ai_next) {
		sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (sock < 0)
			continue;
		if (connect(sock, ai->ai_addr, ai->ai_addrlen) == 0)
			break;
		close(sock);
		sock = -1;
	}
	freeaddrinfo(res);
	if (sock < 0) {
		fprintf(stderr, "serprog: cannot connect to %s port %s: %s\n", host, port, strerror(errno));
		return -1;
	}
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	return 0;
}

/* SYNCNOP answers NAK then ACK; whatever a previous session left in flight
 * comes before them. */
static int synchronize(void)
{
	unsigned char cmd = S_CMD_SYNCNOP, b, last = 0;
	struct iovec iov = { .iov_base = &cmd, .iov_len = 1 };

	if (send_all(&iov, 1))
		return -1;
	for (int i = 0; i < 4096; i++) {
		if (recv_all(&b, 1))
			return -1;
		if (last == S_NAK && b == S_ACK)
			return 0;
		last = b;
	}
	fprintf(stderr, "serprog: no sync with the programmer\n");
	return -1;
}

static int serprog_shutdown(void)
{
	if (sock >= 0) {
		unsigned char off = 0;
		if (supports(S_CMD_S_PIN_STATE))
			command(S_CMD_S_PIN_STATE, &off, 1, NULL, 0);
		close(sock);
		sock = -1;
	}
	free(pending);
	pending = NULL;
	pending_len = pending_cap = 0;
	selected = read_sent = 0;
	return 0;
}

static int serprog_init(void)
{
	unsigned char answer[32];
	char name[17] = "";
	unsigned char spi = S_BUS_SPI, on = 1;
	int r;

	if (!host[0])
		return -1;
	if (connect_to_host() || synchronize())
		goto fail;
	if (command(S_CMD_Q_IFACE, NULL, 0, answer, 2) || answer[0] != 1 || answer[1] != 0) {
		fprintf(stderr, "serprog: %s does not speak serprog interface version 1\n", host);
		goto fail;
	}
	if (command(S_CMD_Q_CMDMAP, NULL, 0, cmdmap, sizeof(cmdmap)))
		goto fail;
	if (!supports(S_CMD_O_SPIOP)) {
		fprintf(stderr, "serprog: %s has no SPI operation\n", host);
		goto fail;
	}
	if (supports(S_CMD_Q_PGMNAME) && command(S_CMD_Q_PGMNAME, NULL, 0, (unsigned char *)name, 16) == 0)
		name[16] = '\0';
	if (supports(S_CMD_Q_BUSTYPE) && command(S_CMD_Q_BUSTYPE, NULL, 0, answer, 1) == 0
	    && !(answer[0] & S_BUS_SPI)) {
		fprintf(stderr, "serprog: %s has no SPI bus\n", host);
		goto fail;
	}
	if (supports(S_CMD_S_BUSTYPE) && command(S_CMD_S_BUSTYPE, &spi, 1, NULL, 0)) {
		fprintf(stderr, "serprog: %s would not select its SPI bus\n", host);
		goto fail;
	}
	max_write = max_read = 1ul << 24;
	if (supports(S_CMD_Q_WRNMAXLEN) && command(S_CMD_Q_WRNMAXLEN, NULL, 0, answer, 3) == 0)
		max_write = u24(answer);
	if (supports(S_CMD_Q_RDNMAXLEN) && command(S_CMD_Q_RDNMAXLEN, NULL, 0, answer, 3) == 0)
		max_read = u24(answer);

	unsigned int khz = spi_controller_get_speed_khz();
	if (khz && supports(S_CMD_S_SPI_FREQ)) {
		unsigned long hz = khz * 1000ul;
		unsigned char want[4] = { hz & 0xff, (hz >> 8) & 0xff, (hz >> 16) & 0xff, (hz >> 24) & 0xff };
		if (command(S_CMD_S_SPI_FREQ, want, 4, answer, 4) == 0)
			khz = (unsigned int)((answer[0] | (answer[1] << 8) | (answer[2] << 16) |
					      ((unsigned long)answer[3] << 24)) / 1000);
		else
			fprintf(stderr, "serprog: %s kept its own SPI clock\n", host);
	}

	if (supports(S_CMD_S_PIN_STATE)) {
		r = command(S_CMD_S_PIN_STATE, &on, 1, NULL, 0);
		if (r == 1) {
			fprintf(stderr, "serprog: %s would not drive the chip. On a thingino backpack: is the\n"
					"camera off, with VCC from the backpack, or parkable? Its status page says.\n",
				host);
			goto fail;
		}
		if (r)
			goto fail;
	}
	printf("serprog: %s at %s port %s%s", name[0] ? name : "programmer", host, port,
	       khz ? "" : "\n");
	if (khz)
		printf(", SPI at %u kHz\n", khz);
	return 0;

fail:
	serprog_shutdown();
	return -1;
}

static int serprog_reinit(void)
{
	serprog_shutdown();
	return serprog_init();
}

static int serprog_cs_select(void)
{
	selected = 1;
	read_sent = 0;
	pending_len = 0;
	return 0;
}

static int serprog_cs_deselect(void)
{
	int r = 0;
	if (selected && pending_len && !read_sent)
		r = spi_op(pending, pending_len, NULL, 0);
	selected = 0;
	pending_len = 0;
	return r ? -1 : 0;
}

static int serprog_send_command(unsigned int writecnt, unsigned int readcnt,
				const unsigned char *writearr, unsigned char *readarr)
{
	if (sock < 0)
		return -1;
	/* Outside a select, a call is a transaction of its own. */
	if (!selected) {
		if (spi_op(writearr, writecnt, readarr, readcnt))
			return -1;
		return 0;
	}
	if (read_sent && (writecnt || readcnt)) {
		fprintf(stderr, "serprog: more SPI traffic after a read in one chip select; serprog "
				"cannot continue a transaction\n");
		return -1;
	}
	if (writecnt) {
		if (pending_len + writecnt > pending_cap) {
			size_t cap = pending_cap ? pending_cap : 256;
			while (cap < pending_len + writecnt)
				cap *= 2;
			unsigned char *grown = realloc(pending, cap);
			if (!grown)
				return -1;
			pending = grown;
			pending_cap = cap;
		}
		memcpy(pending + pending_len, writearr, writecnt);
		pending_len += writecnt;
	}
	if (readcnt) {
		if (spi_op(pending, pending_len, readarr, readcnt))
			return -1;
		read_sent = 1;
		pending_len = 0;
	}
	return 0;
}

static const char *serprog_libusb_version(void)
{
	return "none (network)";
}

const struct spi_programmer serprog_net_programmer = {
	.name           = "serprog",
	.init           = serprog_init,
	.shutdown       = serprog_shutdown,
	.send_command   = serprog_send_command,
	.cs_select      = serprog_cs_select,
	.cs_deselect    = serprog_cs_deselect,
	.libusb_version = serprog_libusb_version,
	.reinit         = serprog_reinit,
};
