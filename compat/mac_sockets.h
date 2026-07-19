/*
 * This file is part of ottd-macos9 — a port of OpenTTD to Mac OS 9 / PowerPC.
 * Copyright (c) 2026 Felipe De Bene.
 *
 * Derived from and/or built against OpenTTD, Copyright (c) the OpenTTD
 * Development Team. Modified for the Mac OS 9 / PowerPC port in 2026.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2, as published
 * by the Free Software Foundation. This program comes with NO WARRANTY. See
 * the LICENSE and NOTICE files in the repository root for the full terms.
 */

// BSD sockets API surface for classic Mac OS (Retro68 has no sys/socket.h).
// DECLARATIONS ONLY -- the implementation will be backed by Open Transport
// (or MacTCP) in a later mac_net.cpp seam. This is enough to COMPILE OpenTTD's
// network layer; linking a real game needs the OT backend.
#ifndef OTTD_COMPAT_MAC_SOCKETS_H
#define OTTD_COMPAT_MAC_SOCKETS_H

#include <cstdint>
#include <sys/types.h>
#include <sys/time.h>    // timeval  (present in Retro68 newlib)
#include <sys/select.h>  // fd_set, FD_SET/FD_ZERO/FD_ISSET, FD_SETSIZE

typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;
typedef unsigned short sa_family_t;
#ifndef socklen_t
typedef int socklen_t;
#endif

/* Address families / socket types / protocols */
#define AF_UNSPEC   0
#define AF_INET     2
#define AF_INET6    30
#define PF_UNSPEC   AF_UNSPEC
#define PF_INET     AF_INET
#define PF_INET6    AF_INET6
#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17
#define IPPROTO_IP   0
#define IPPROTO_IPV6 41
#define IPV6_V6ONLY  27

/* ioctl request codes (values arbitrary for the shim; interpreted by the OT backend) */
#define FIONBIO   0x8004667e
#define FIONREAD  0x4004667f

/* setsockopt levels/options */
#define SOL_SOCKET     0xffff
#define SO_REUSEADDR   0x0004
#define SO_REUSEPORT   0x0200
#define SO_BROADCAST   0x0020
#define SO_ERROR       0x1007
#define TCP_NODELAY    0x0001

/* getaddrinfo / getnameinfo flags */
#define AI_PASSIVE     0x0001
#define AI_ADDRCONFIG  0x0400
#define NI_NUMERICHOST 0x0002
#define NI_MAXHOST     1025
#define NI_MAXSERV     32

#ifndef INADDR_NONE
#define INADDR_NONE  0xffffffffU
#endif
#ifndef INADDR_ANY
#define INADDR_ANY   0x00000000U
#endif

/* recv/send flags */
#define MSG_NOSIGNAL 0

struct in_addr { in_addr_t s_addr; };
struct in6_addr { unsigned char s6_addr[16]; };

struct sockaddr {
	sa_family_t sa_family;
	char        sa_data[14];
};
struct sockaddr_in {
	sa_family_t    sin_family;
	in_port_t      sin_port;
	struct in_addr sin_addr;
	char           sin_zero[8];
};
struct sockaddr_in6 {
	sa_family_t     sin6_family;
	in_port_t       sin6_port;
	uint32_t        sin6_flowinfo;
	struct in6_addr sin6_addr;
	uint32_t        sin6_scope_id;
};
struct sockaddr_storage {
	sa_family_t ss_family;
	char        __ss_padding[126];
};
struct addrinfo {
	int              ai_flags;
	int              ai_family;
	int              ai_socktype;
	int              ai_protocol;
	socklen_t        ai_addrlen;
	struct sockaddr *ai_addr;
	char            *ai_canonname;
	struct addrinfo *ai_next;
};

struct pollfd { int fd; short events; short revents; };

#ifdef __cplusplus
extern "C" {
#endif

int     socket(int domain, int type, int protocol);
int     bind(int s, const struct sockaddr *addr, socklen_t len);
int     connect(int s, const struct sockaddr *addr, socklen_t len);
int     listen(int s, int backlog);
int     accept(int s, struct sockaddr *addr, socklen_t *len);
ssize_t send(int s, const void *buf, size_t len, int flags);
ssize_t recv(int s, void *buf, size_t len, int flags);
ssize_t sendto(int s, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen);
ssize_t recvfrom(int s, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen);
int     setsockopt(int s, int level, int name, const void *val, socklen_t len);
int     getsockopt(int s, int level, int name, void *val, socklen_t *len);
int     getsockname(int s, struct sockaddr *addr, socklen_t *len);
int     getpeername(int s, struct sockaddr *addr, socklen_t *len);
int     shutdown(int s, int how);
int     ioctl(int s, unsigned long request, ...);
int     select(int nfds, fd_set *rd, fd_set *wr, fd_set *ex, struct timeval *timeout);
int     getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
void    freeaddrinfo(struct addrinfo *res);
const char *gai_strerror(int ecode);
int     getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen, char *serv, socklen_t servlen, int flags);

uint16_t htons(uint16_t x);
uint16_t ntohs(uint16_t x);
uint32_t htonl(uint32_t x);
uint32_t ntohl(uint32_t x);

#ifdef __cplusplus
}
#endif

#endif // OTTD_COMPAT_MAC_SOCKETS_H
