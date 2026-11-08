/**
 * @file flash_layout.c
 * @brief Identify the partitions of a raw SPI NOR flash image
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * SPI NOR flash in cameras carries no partition table. The layout lives in
 * an mtdparts= string, usually in the U-Boot environment (the mtdparts
 * variable, or inside bootargs) and sometimes only in the bootloader's
 * built-in default environment. This finds that string, preferring one in a
 * U-Boot environment whose CRC checks out, and identifies what each
 * partition holds from well-known headers: Ingenic and SigmaStar boot
 * images, U-Boot environments, uImage, FIT/device tree, squashfs, JFFS2, UBI
 * and compressed streams. Without an mtdparts string, the headers alone give
 * the boundaries.
 */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flash_layout.h"

#define SCAN_STEP   0x1000UL    /* headers start on 4 KB sector boundaries */
#define JFFS2_BLOCK 0x10000UL   /* erase block size assumed for JFFS2 */
#define MAX_ENVS    8
#define MAX_SPEC    512         /* longest mtdparts string considered */

struct env_info {
	unsigned long off;
	unsigned long size;
	int redund;             /* redundant env: flag byte after the CRC */
	int nvars;
};

/* ------------------------------------------------------------------ */
/* Helpers */

static uint32_t be32(const unsigned char *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t le32(const unsigned char *p)
{
	return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

static uint16_t le16(const unsigned char *p)
{
	return (uint16_t)(p[1] << 8 | p[0]);
}

static uint64_t le64(const unsigned char *p)
{
	return (uint64_t)le32(p + 4) << 32 | le32(p);
}

static uint32_t crc32_ieee(const unsigned char *p, unsigned long n)
{
	static uint32_t table[256];
	uint32_t c;

	if (!table[1]) {
		for (uint32_t i = 0; i < 256; i++) {
			c = i;
			for (int k = 0; k < 8; k++)
				c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
			table[i] = c;
		}
	}
	c = 0xFFFFFFFFu;
	while (n--)
		c = table[(c ^ *p++) & 0xFF] ^ (c >> 8);
	return ~c;
}

static int is_blank(const unsigned char *p, unsigned long n)
{
	while (n--)
		if (*p++ != 0xFF)
			return 0;
	return 1;
}

/* a + b, saturated instead of wrapping: partition sizes come from the image */
static unsigned long add_sat(unsigned long a, unsigned long b)
{
	return b > ULONG_MAX - a ? ULONG_MAX : a + b;
}

/* Bytes from s up to and including the last non-0xFF byte before e */
static unsigned long used_bytes(const unsigned char *img, unsigned long s, unsigned long e)
{
	while (e > s && img[e - 1] == 0xFF)
		e--;
	return e - s;
}

/* "320 KB" for partition sizes: whole KB when exact, as mtdparts writes them */
static void fmt_part(char *buf, size_t n, unsigned long v)
{
	if (v && v % 1024 == 0)
		snprintf(buf, n, "%lu KB", v / 1024);
	else
		snprintf(buf, n, "%lu B", v);
}

/* "5.72 MB" / "251 KB" / "12 B" for amounts in use */
static void fmt_used(char *buf, size_t n, unsigned long v)
{
	if (v >= 1024UL * 1024)
		snprintf(buf, n, "%.2f MB", v / (1024.0 * 1024));
	else if (v >= 1024)
		snprintf(buf, n, "%lu KB", (v + 1023) / 1024);
	else
		snprintf(buf, n, "%lu B", v);
}

/* ------------------------------------------------------------------ */
/* U-Boot environment */

static int env_name_char(unsigned char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
}

/* "name=" at p, as the first variable of an environment starts */
static int looks_like_var(const unsigned char *p, unsigned long avail)
{
	unsigned long i = 0;

	while (i < avail && i < 40 && env_name_char(p[i]))
		i++;
	return i > 0 && i < avail && p[i] == '=';
}

/* An environment at off: CRC32 (LE) over the rest of a power-of-two sized
 * block, after an extra flag byte for redundant environments. */
static int env_at(const unsigned char *img, unsigned long len, unsigned long off,
		  struct env_info *e)
{
	static const unsigned long sizes[] = {
		0x1000, 0x2000, 0x4000, 0x8000, 0x10000, 0x20000, 0x40000,
	};

	for (int red = 0; red <= 1; red++) {
		unsigned long data = off + 4 + red;

		if (data >= len || !looks_like_var(img + data, len - data))
			continue;
		uint32_t want = le32(img + off);
		for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
			unsigned long sz = sizes[i];

			if (off + sz > len)
				break;
			if (crc32_ieee(img + data, sz - 4 - red) != want)
				continue;
			e->off = off;
			e->size = sz;
			e->redund = red;
			e->nvars = 0;
			for (unsigned long p = data; p < off + sz && img[p]; ) {
				unsigned long q = p;
				while (q < off + sz && img[q])
					q++;
				e->nvars++;
				p = q + 1;
			}
			return 1;
		}
	}
	return 0;
}

static int find_envs(const unsigned char *img, unsigned long len, struct env_info *envs)
{
	int n = 0;

	for (unsigned long off = 0; off + SCAN_STEP <= len && n < MAX_ENVS; off += SCAN_STEP)
		if (env_at(img, len, off, &envs[n]))
			n++;
	return n;
}

static const struct env_info *env_containing(const struct env_info *envs, int nenv,
					     unsigned long off)
{
	for (int i = 0; i < nenv; i++)
		if (off >= envs[i].off && off < envs[i].off + envs[i].size)
			return &envs[i];
	return NULL;
}

/* ------------------------------------------------------------------ */
/* mtdparts */

struct mtd_spec {
	char id[32];
	int n;
	struct {
		unsigned long start, size;
		char name[32];
	} p[FL_MAX_PARTS];
};

static int parse_size(const char **s, unsigned long *v)
{
	char *end;
	unsigned long mul = 1;
	unsigned long x = strtoul(*s, &end, 0);

	if (end == *s)
		return 0;
	switch (*end) {
	case 'k': case 'K': mul = 1024; end++; break;
	case 'm': case 'M': mul = 1024UL * 1024; end++; break;
	case 'g': case 'G': mul = 1024UL * 1024 * 1024; end++; break;
	}
	/* A size that does not fit in unsigned long cannot describe a real
	 * partition and would overflow the arithmetic that uses it. */
	if (x > ULONG_MAX / mul)
		return 0;
	*s = end;
	*v = x * mul;
	return 1;
}

/* <mtd-id>:<size>[@<offset>](<name>)[ro|lk],...  for the first mtd-id.
 * A "-" size takes the rest of the flash. Entries that cover the whole
 * flash or overlap earlier ones (e.g. 16384k@0(all)) are aliases and are
 * skipped. Returns the number of partitions. */
static int parse_mtdparts(const char *spec, unsigned long flash_size, struct mtd_spec *m)
{
	const char *s = spec;
	unsigned long pos = 0;
	size_t i;

	memset(m, 0, sizeof(*m));
	while (!strncmp(s, "mtdparts=", 9))
		s += 9;
	for (i = 0; s[i] && s[i] != ':' && i < sizeof(m->id) - 1; i++) {
		if (!env_name_char((unsigned char)s[i]))
			return 0;
		m->id[i] = s[i];
	}
	if (s[i] != ':' || i == 0)
		return 0;
	s += i + 1;

	while (*s && m->n < FL_MAX_PARTS) {
		unsigned long size, off;
		int has_off = 0;

		if (*s == '-') {
			size = flash_size > pos ? flash_size - pos : 0;
			s++;
		} else if (!parse_size(&s, &size)) {
			return 0;
		}
		off = pos;
		if (*s == '@') {
			s++;
			if (!parse_size(&s, &off))
				return 0;
			has_off = 1;
		}
		if (*s != '(')
			return 0;
		const char *close = strchr(s, ')');
		if (!close || close - s - 1 >= (long)sizeof(m->p[0].name) || close == s + 1)
			return 0;

		/* whole-flash aliases such as 16384k@0(all), wherever listed */
		int alias = size == 0 ||
			    (has_off && off == 0 && size >= flash_size) ||
			    (has_off && off < pos);
		if (!alias) {
			m->p[m->n].start = off;
			m->p[m->n].size = size;
			memcpy(m->p[m->n].name, s + 1, close - s - 1);
			m->p[m->n].name[close - s - 1] = '\0';
			m->n++;
			pos = off + size;
		}
		s = close + 1;
		while (!strncmp(s, "ro", 2) || !strncmp(s, "lk", 2))
			s += 2;
		if (*s != ',')
			break;
		s++;
	}
	return m->n;
}

/* Copy the printable string starting at off (up to a NUL, space or
 * non-printable byte) into buf. */
static void copy_string(const unsigned char *img, unsigned long len, unsigned long off,
			char *buf, size_t n)
{
	size_t i = 0;

	while (off + i < len && i < n - 1 && img[off + i] > ' ' && img[off + i] < 0x7F) {
		buf[i] = (char)img[off + i];
		i++;
	}
	buf[i] = '\0';
}

/* Find the layout: an mtdparts string in a valid U-Boot environment (as the
 * mtdparts variable or in bootargs) wins over one found anywhere else. */
static int find_mtdparts(const unsigned char *img, unsigned long len, unsigned long flash_size,
			 const struct env_info *envs, int nenv, struct mtd_spec *best,
			 char *source, size_t source_len, int *distinct)
{
	static const char key[] = "mtdparts=";
	const size_t klen = sizeof(key) - 1;
	char spec[MAX_SPEC], first[MAX_SPEC] = "";
	int found = 0, in_env = 0;

	*distinct = 0;
	for (unsigned long off = 0; off + klen < len; off++) {
		if (img[off] != 'm' || memcmp(img + off, key, klen))
			continue;
		struct mtd_spec m;

		copy_string(img, len, off, spec, sizeof(spec));
		if (parse_mtdparts(spec, flash_size, &m) < 2)
			continue;
		const char *canon = strchr(spec, ':');
		if (!first[0])
			snprintf(first, sizeof(first), "%s", canon);
		else if (strcmp(first, canon))
			*distinct = 2;
		if (!*distinct)
			*distinct = 1;

		const struct env_info *e = env_containing(envs, nenv, off);
		if (!found || (e && !in_env)) {
			*best = m;
			found = 1;
			in_env = e != NULL;
			if (e)
				snprintf(source, source_len, "mtdparts (%s) in the U-Boot env at 0x%06lX",
					 m.id, e->off);
			else
				snprintf(source, source_len, "mtdparts (%s) found at 0x%06lX, "
					 "not in a valid U-Boot env", m.id, off);
		}
		off += klen;
	}
	return found;
}

/* ------------------------------------------------------------------ */
/* Contents */

static const char *uimage_comp(int c)
{
	static const char *names[] = { "uncompressed", "gzip", "bzip2", "lzma", "lzo", "lz4", "zstd" };
	return c >= 0 && c < (int)(sizeof(names) / sizeof(names[0])) ? names[c] : "unknown compression";
}

static const char *uimage_type(int t)
{
	switch (t) {
	case 2: return "kernel";
	case 3: return "ramdisk";
	case 4: return "multi";
	case 5: return "firmware";
	case 6: return "script";
	case 7: return "filesystem";
	case 8: return "device tree";
	default: return "image";
	}
}

static const char *squashfs_comp(int c)
{
	static const char *names[] = { "?", "gzip", "lzma", "lzo", "xz", "lz4", "zstd" };
	return c > 0 && c < (int)(sizeof(names) / sizeof(names[0])) ? names[c] : "unknown compression";
}

/* uImage header with a valid header CRC */
static int is_uimage(const unsigned char *p, unsigned long avail)
{
	unsigned char hdr[64];

	if (avail < 64 || be32(p) != 0x27051956)
		return 0;
	memcpy(hdr, p, sizeof(hdr));
	memset(hdr + 4, 0, 4);
	return crc32_ieee(hdr, sizeof(hdr)) == be32(p + 4);
}

static int is_squashfs(const unsigned char *p, unsigned long avail)
{
	return avail >= 96 && !memcmp(p, "hsqs", 4) && le16(p + 28) == 4;
}

static int is_fdt(const unsigned char *p, unsigned long avail)
{
	return avail >= 40 && be32(p) == 0xD00DFEED && be32(p + 4) >= 40;
}

static int is_jffs2(const unsigned char *p, unsigned long avail)
{
	return avail >= 12 && p[0] == 0x85 && p[1] == 0x19;
}

/* Describe a known header at p; returns 1 and the bytes it covers in *span,
 * or 0. */
static int describe_header(const unsigned char *img, unsigned long len, unsigned long off,
			   const struct env_info *envs, int nenv,
			   char *what, size_t n, unsigned long *span)
{
	const unsigned char *p = img + off;
	unsigned long avail = len - off;
	char u[32];

	*span = 0;
	for (int i = 0; i < nenv; i++) {
		if (envs[i].off == off) {
			fmt_used(u, sizeof(u), envs[i].size);
			snprintf(what, n, "U-Boot env (%s%s, CRC OK, %d variables)",
				 u, envs[i].redund ? ", redundant" : "", envs[i].nvars);
			*span = envs[i].size;
			return 1;
		}
	}
	if (off == 0 && avail >= 9 && !memcmp(p, "\x06\x05\x04\x03\x02\x55\xAA\x55\xAA", 9)) {
		snprintf(what, n, "Ingenic boot image (SPL + U-Boot)");
		return 1;
	}
	if (off == 0 && avail >= 8 && !memcmp(p + 4, "IPL_", 4)) {
		snprintf(what, n, "SigmaStar IPL + U-Boot");
		return 1;
	}
	if (is_uimage(p, avail)) {
		char name[33];
		unsigned long size = be32(p + 12);

		memcpy(name, p + 32, 32);
		name[32] = '\0';
		for (char *c = name; *c; c++)   /* the name is free-form bytes */
			if (*c < ' ' || *c > '~')
				*c = '.';
		fmt_used(u, sizeof(u), size + 64);
		snprintf(what, n, "uImage \"%s\" (%s, %s), %s", name, uimage_type(p[30]),
			 uimage_comp(p[31]), u);
		*span = size + 64;
		return 1;
	}
	if (is_fdt(p, avail)) {
		unsigned long size = be32(p + 4);

		fmt_used(u, sizeof(u), size);
		snprintf(what, n, "FIT image / device tree, %s", u);
		*span = size;
		return 1;
	}
	if (is_squashfs(p, avail)) {
		unsigned long used = (unsigned long)le64(p + 40);

		fmt_used(u, sizeof(u), used);
		snprintf(what, n, "squashfs 4.0 (%s), %s used", squashfs_comp(le16(p + 20)), u);
		*span = used;
		return 1;
	}
	if (avail >= 4 && !memcmp(p, "UBI#", 4)) {
		snprintf(what, n, "UBI");
		return 1;
	}
	if (avail >= 3 && p[0] == 0x1F && p[1] == 0x8B && p[2] == 0x08) {
		snprintf(what, n, "gzip data");
		return 1;
	}
	if (avail >= 6 && !memcmp(p, "\xFD" "7zXZ\0", 6)) {
		snprintf(what, n, "xz data");
		return 1;
	}
	if (avail >= 0x28 && le32(p + 0x24) == 0x016F2818) {
		snprintf(what, n, "Linux zImage");
		return 1;
	}
	return 0;
}

/* JFFS2 over [s, e): formatted-but-empty erase blocks hold only a 12-byte
 * clean marker (node type 0x2003); anything else is in use. */
static void describe_jffs2(const unsigned char *img, unsigned long s, unsigned long e,
			   char *what, size_t n)
{
	unsigned long blocks = 0, clean = 0, used = 0;

	for (unsigned long b = s; b + JFFS2_BLOCK <= e; b += JFFS2_BLOCK) {
		blocks++;
		if (is_jffs2(img + b, JFFS2_BLOCK) && le16(img + b + 2) == 0x2003 &&
		    le32(img + b + 4) == 12 && is_blank(img + b + 12, JFFS2_BLOCK - 12))
			clean++;
		else if (!is_blank(img + b, JFFS2_BLOCK))
			used++;
	}
	if (used == 0)
		snprintf(what, n, "JFFS2, empty (%lu of %lu erase blocks formatted)", clean, blocks);
	else
		snprintf(what, n, "JFFS2, %lu of %lu erase blocks in use", used, blocks);
}

/* What the region [s, s + size) holds */
static void identify(const unsigned char *img, unsigned long len, unsigned long s,
		     unsigned long size, const struct env_info *envs, int nenv,
		     char *what, size_t n)
{
	unsigned long e, span;
	char u[32], hdr[128];

	if (s >= len) {
		snprintf(what, n, "not in this image (it ends at 0x%06lX)", len);
		return;
	}
	/* s + size may overflow for a corrupt or hostile mtdparts string:
	 * clamp before the end is used as a scan bound. */
	e = size > len - s ? len : s + size;
	if (is_blank(img + s, e - s)) {
		snprintf(what, n, "blank");
		return;
	}
	if (is_jffs2(img + s, e - s)) {
		describe_jffs2(img, s, e, what, n);
		if (size > len - s)
			snprintf(what + strlen(what), n - strlen(what), " (image ends inside)");
		return;
	}
	/* A header at the start, or failing that the first one inside */
	for (unsigned long off = s; off < e; off += SCAN_STEP) {
		if (!describe_header(img, len, off, envs, nenv, hdr, sizeof(hdr), &span))
			continue;
		if (off == s)
			snprintf(what, n, "%s", hdr);
		else
			snprintf(what, n, "%s at +0x%lX", hdr, off - s);
		if (!span) {
			fmt_used(u, sizeof(u), used_bytes(img, s, e));
			snprintf(what + strlen(what), n - strlen(what), ", %s used", u);
		}
		if (size > len - s)
			snprintf(what + strlen(what), n - strlen(what), " (image ends inside)");
		return;
	}
	fmt_used(u, sizeof(u), used_bytes(img, s, e));
	snprintf(what, n, "data, %s used%s", u, size > len - s ? " (image ends inside)" : "");
}

/* ------------------------------------------------------------------ */
/* Layout without mtdparts: boundaries from headers */

static int detect_regions(const unsigned char *img, unsigned long len, unsigned long flash_size,
			  const struct env_info *envs, int nenv, struct fl_layout *l)
{
	unsigned long starts[FL_MAX_PARTS];
	int n = 0;
	unsigned long skip_to = 0;
	char what[128];

	for (unsigned long off = 0; off < len && n < FL_MAX_PARTS - 1; off += SCAN_STEP) {
		unsigned long span = 0;

		if (off < skip_to)
			continue;
		if (off % JFFS2_BLOCK == 0 && is_jffs2(img + off, len - off)) {
			/* one region for the whole run of JFFS2 erase blocks */
			unsigned long end = off;
			while (end + JFFS2_BLOCK <= len &&
			       (is_jffs2(img + end, JFFS2_BLOCK) || is_blank(img + end, JFFS2_BLOCK)))
				end += JFFS2_BLOCK;
			starts[n++] = off;
			skip_to = end;
			continue;
		}
		if (describe_header(img, len, off, envs, nenv, what, sizeof(what), &span)) {
			starts[n++] = off;
			if (span)
				skip_to = off + ((span + SCAN_STEP - 1) & ~(SCAN_STEP - 1));
		}
	}
	if (n == 0 || starts[0] != 0) {
		memmove(starts + 1, starts, n * sizeof(starts[0]));
		starts[0] = 0;
		n++;
	}

	unsigned long end_all = flash_size > len ? flash_size : len;
	l->nparts = 0;
	for (int i = 0; i < n; i++) {
		struct fl_part *p = &l->parts[l->nparts++];

		p->start = starts[i];
		p->size = (i + 1 < n ? starts[i + 1] : end_all) - starts[i];
		p->name[0] = '\0';
		identify(img, len, p->start, p->size, envs, nenv, p->what, sizeof(p->what));
	}
	return l->nparts;
}

/* ------------------------------------------------------------------ */
/* Public interface */

int flash_layout_analyze(const unsigned char *img, unsigned long len,
			 unsigned long flash_size, struct fl_layout *out)
{
	struct env_info envs[MAX_ENVS];
	struct mtd_spec m;
	int nenv, distinct;
	int flash_known = flash_size != 0;

	memset(out, 0, sizeof(*out));
	if (len == 0) {
		snprintf(out->source, sizeof(out->source), "an empty image");
		return 0;
	}
	if (!flash_known)
		flash_size = len;       /* for "-" (rest of flash) sizes */
	if (len >= 8 && !memcmp(img, "ANJOY888", 8))
		snprintf(out->note, sizeof(out->note), "this is an Anjvision ANJOY888 upgrade "
			 "package, not a raw flash image; offsets are file offsets");

	nenv = find_envs(img, len, envs);
	if (find_mtdparts(img, len, flash_size, envs, nenv, &m, out->source,
			  sizeof(out->source), &distinct)) {
		unsigned long end = 0;

		for (int i = 0; i < m.n; i++) {
			struct fl_part *p = &out->parts[out->nparts++];

			p->start = m.p[i].start;
			p->size = m.p[i].size;
			snprintf(p->name, sizeof(p->name), "%s", m.p[i].name);
			identify(img, len, p->start, p->size, envs, nenv, p->what, sizeof(p->what));
			end = add_sat(p->start, p->size);
		}
		if (distinct > 1)
			snprintf(out->source + strlen(out->source),
				 sizeof(out->source) - strlen(out->source),
				 "; the image holds other, different mtdparts strings too");
		if (out->note[0])
			;
		else if (flash_known && end > flash_size)
			snprintf(out->note, sizeof(out->note), "the layout covers 0x%06lX bytes "
				 "but the flash is 0x%06lX: an image for a bigger chip?", end, flash_size);
		else if (end > len)
			snprintf(out->note, sizeof(out->note), "the image ends at 0x%06lX, inside the "
				 "layout (a trimmed dump?); the rest is not in this file", len);
		else if (end < len && !is_blank(img + end, len - end))
			snprintf(out->note, sizeof(out->note), "data after the last partition "
				 "(0x%06lX)", end);
		return out->nparts;
	}

	snprintf(out->source, sizeof(out->source), "detected headers; no mtdparts string found");
	return detect_regions(img, len, flash_size, envs, nenv, out);
}

void flash_layout_print(const struct fl_layout *l)
{
	char sz[24];
	int szw = 1, namew = 1;

	if (l->note[0])
		printf("Layout note: %s\n", l->note);
	printf("Layout from %s:\n", l->source);
	/* columns only as wide as their widest entry: the web log is narrow */
	for (int i = 0; i < l->nparts; i++) {
		int w;

		fmt_part(sz, sizeof(sz), l->parts[i].size);
		if ((w = (int)strlen(sz)) > szw)
			szw = w;
		if ((w = (int)strlen(l->parts[i].name)) > namew)
			namew = w;
	}
	for (int i = 0; i < l->nparts; i++) {
		const struct fl_part *p = &l->parts[i];

		fmt_part(sz, sizeof(sz), p->size);
		printf("  0x%06lX-0x%06lX %*s %-*s %s\n", p->start, add_sat(p->start, p->size - 1),
		       szw, sz, namew, p->name[0] ? p->name : "-", p->what);
	}
}

/* A bounded JSON writer: once something does not fit, `over` is set and
 * nothing more is written. */
struct jbuf {
	char *p;
	size_t n, len;
	int over;
};

static void jput(struct jbuf *b, const char *s, size_t k)
{
	if (b->over || b->len + k >= b->n) {
		b->over = 1;
		return;
	}
	memcpy(b->p + b->len, s, k);
	b->len += k;
	b->p[b->len] = '\0';
}

static void jputs(struct jbuf *b, const char *s)
{
	jput(b, s, strlen(s));
}

static void jstr(struct jbuf *b, const char *s)
{
	char e[8];

	jputs(b, "\"");
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;

		if (c == '"' || c == '\\') {
			e[0] = '\\';
			e[1] = (char)c;
			jput(b, e, 2);
		} else if (c < 0x20 || c > 0x7E) {
			snprintf(e, sizeof(e), "\\u%04x", c);
			jputs(b, e);
		} else {
			jput(b, s, 1);
		}
	}
	jputs(b, "\"");
}

int flash_layout_json(const struct fl_layout *l, char *out, size_t n)
{
	struct jbuf b = { out, n, 0, 0 };
	char num[64];

	if (!out || n == 0)
		return -1;
	out[0] = '\0';
	jputs(&b, "{\"source\":");
	jstr(&b, l->source);
	jputs(&b, ",\"note\":");
	jstr(&b, l->note);
	jputs(&b, ",\"parts\":[");
	for (int i = 0; i < l->nparts; i++) {
		const struct fl_part *p = &l->parts[i];

		snprintf(num, sizeof(num), "%s{\"start\":%lu,\"size\":%lu,\"name\":",
			 i ? "," : "", p->start, p->size);
		jputs(&b, num);
		jstr(&b, p->name);
		jputs(&b, ",\"what\":");
		jstr(&b, p->what);
		jputs(&b, "}");
	}
	jputs(&b, "]}");
	if (b.over) {
		out[0] = '\0';
		return -1;
	}
	return (int)b.len;
}

void flash_layout_report(const unsigned char *img, unsigned long len, unsigned long flash_size)
{
	struct fl_layout *l = malloc(sizeof(*l));

	if (!l)
		return;
	flash_layout_analyze(img, len, flash_size, l);
	flash_layout_print(l);
	free(l);
}
