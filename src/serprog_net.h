/*
 * serprog_net.h
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __SERPROG_NET_H__
#define __SERPROG_NET_H__

/* flashrom's serprog over TCP, as the thingino backpack serves it. */
#define SERPROG_NET_PORT "8888"

/* host, host:port, [IPv6]:port or a bare IPv6 address; 0 on success. */
int serprog_net_set_host(const char *spec);
int serprog_net_has_host(void);

#endif /* __SERPROG_NET_H__ */
