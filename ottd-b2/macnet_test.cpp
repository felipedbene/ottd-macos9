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

/* macnet_test.c — sockets seam (Open Transport) milestone 1: prove the BSD socket
 * API works over OT by opening a UDP socket and sending a datagram to the k8s
 * log-sink via socket()+sendto(). The sink logs any UDP it receives, so if the
 * distinct marker payload shows up there, the OT-backed BSD UDP path is live.
 *
 * Uses the real compat/mac_sockets.h declarations; the bodies come from
 * mac_sockets_ot.c (the Open Transport backend). Neutral C — no Toolbox, no OT
 * headers here, just the BSD API. */
#include "mac_sockets.h"
#include <string.h>
#include <cerrno>

/* C linkage: ottd_log/mac_closesocket are C symbols, and b2main.c (C) calls
 * macnet_selftest — all must be unmangled to match. */
extern "C" void ottd_log(const char *fmt, ...);
extern "C" int  mac_closesocket(int fd);   /* os_abstraction maps closesocket() -> this */
extern "C" void ottd_cooperative_sleep_us(unsigned long us);  /* WNE-pumping yield */

/* build 26 connect-wait probes (mac_sockets_ot.c) */
extern "C" int ms_dbg_epstate(int fd);  /* OTGetEndpointState: 2=T_IDLE 3=T_OUTCON 5=T_DATAXFER */
extern "C" int ms_dbg_look(int fd);     /* OTLook: 0=none 2=T_CONNECT 0x10=T_DISCONNECT */
extern "C" int ms_dbg_notify(int fd);   /* notifier fire count */
extern "C" int ms_dbg_lastev(int fd);   /* last notifier event code */
extern "C" int ms_dbg_state(int fd);    /* our ms_state: 4=CONNECTING 5=CONNECTED */

extern "C" void macnet_selftest(void)
{
    int fd;
    struct sockaddr_in dst;
    const char *msg = "MACNET-OT-SOCKET: BSD sendto() over Open Transport works!";
    ssize_t n;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { ottd_log("macnet: socket(SOCK_DGRAM) FAILED"); return; }
    ottd_log("macnet: socket(SOCK_DGRAM) -> fd=%d", fd);

    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_port        = htons(5514);
    dst.sin_addr.s_addr = htonl(0x0A006472u);   /* 10.0.100.114 (the log-sink) */

    n = sendto(fd, msg, strlen(msg), 0, (struct sockaddr *)&dst, sizeof(dst));
    ottd_log("macnet: sendto(sink) -> %ld (want %d)", (long)n, (int)strlen(msg));

    mac_closesocket(fd);
    ottd_log("REGRESS UDP: %s (sendto %ld/%d bytes)",
             (n == (ssize_t)strlen(msg)) ? "PASS" : "FAIL", (long)n, (int)strlen(msg));
}

/* TCP client validation against a REAL external peer: the full non-blocking
 * connect contract OpenTTD's TCPConnecter uses (connect -> EINPROGRESS ->
 * select-writable -> getsockopt(SO_ERROR) -> send). Optional in the regression
 * run: needs `nc -l 9999` on 10.0.10.69. With no peer it SKIPs fast (~2s) rather
 * than stalling boot -- the loopback test below is the always-on TCP core. */
extern "C" void macnet_tcp_test(void)
{
    int fd, r, i;
    struct sockaddr_in dst;
    fd_set wr;
    struct timeval tv;
    int so_err; socklen_t sl;
    const char *msg = "MACNET-TCP: OpenTransport BSD connect+send works!\n";

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { ottd_log("REGRESS TCP-connect: FAIL (socket)"); return; }

    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_port        = htons(9999);
    dst.sin_addr.s_addr = htonl(0x0A000A45u);   /* 10.0.10.69 (peer running nc -l 9999) */

    errno = 0;
    r = connect(fd, (struct sockaddr *)&dst, sizeof(dst));
    /* Non-blocking connect must report in-progress, never a hard failure. */
    if (r == 0) {
        /* connected inline -- unusual for a remote peer but valid */
    } else if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
        ottd_log("REGRESS TCP-connect: FAIL (connect r=%d errno=%d, wanted EINPROGRESS)", r, errno);
        mac_closesocket(fd); return;
    }

    /* Poll writability ~2s (40 x 50ms); short so no-peer SKIPs quickly. */
    for (i = 0; i < 40; i++) {
        FD_ZERO(&wr); FD_SET(fd, &wr);
        tv.tv_sec = 0; tv.tv_usec = 0;
        r = select(FD_SETSIZE, (fd_set *)0, &wr, (fd_set *)0, &tv);
        if (r > 0 && FD_ISSET(fd, &wr)) break;
        ottd_cooperative_sleep_us(50000);
    }

    so_err = -1; sl = sizeof(so_err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &sl);

    if (i >= 40 && so_err == 0 && ms_dbg_epstate(fd) != 5) {
        /* Never became writable and no error: no cooperating peer answered. */
        ottd_log("REGRESS TCP-connect: SKIP (no peer at 10.0.10.69:9999 -- run nc -l 9999 to test)");
        mac_closesocket(fd); return;
    }
    if (so_err != 0) {
        /* Actively refused/reset: peer up but not listening -> also just SKIP. */
        ottd_log("REGRESS TCP-connect: SKIP (SO_ERROR=%d, no listener at 10.0.10.69:9999)", so_err);
        mac_closesocket(fd); return;
    }

    errno = 0;
    r = (int)send(fd, msg, strlen(msg), 0);
    mac_closesocket(fd);
    ottd_log("REGRESS TCP-connect: %s (connect+send %d/%d bytes to 10.0.10.69:9999)",
             (r == (int)strlen(msg)) ? "PASS" : "FAIL", r, (int)strlen(msg));
}

/* TCP server validation, self-contained LOOPBACK. The Mac is both server and
 * client: it listen()s on 0.0.0.0:9999 and connect()s to 127.0.0.1:9999, then
 * drives accept + connect-completion + a two-way data exchange in ONE
 * cooperative loop. This exercises listen/accept/connect/select/send/recv
 * entirely inside the Mac's own OT stack -- no external host, no network
 * reachability, and it completes in well under a second (no window delay).
 *
 * The mirror of macnet_tcp_test's client path, proving the SERVER contract
 * OpenTTD's tcp_listen.h uses: accept() returns INVALID_SOCKET/-1 while none
 * pending, then a connected worker socket usable for recv()/send(). */
extern "C" void macnet_tcp_accept_test(void)
{
    int lfd, sfd = -1, cfd, i, j;
    struct sockaddr_in srv, lo, cli;
    socklen_t clilen;
    fd_set wr;
    struct timeval tv;
    int on = 1, connected = 0, so_err; socklen_t sl;
    char buf[160];
    ssize_t n;
    const char *ping = "LOOPBACK-PING: client->server\n";
    const char *pong = "MACSRV: OpenTransport BSD listen/accept/recv works!\n";

    /* ---- 1. Listener: bind(ANY:9999) + listen ---- */
    lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) { ottd_log("macsrv: listen socket FAILED"); return; }
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET; srv.sin_port = htons(9999); srv.sin_addr.s_addr = htonl(0x00000000u);
    if (bind(lfd, (struct sockaddr *)&srv, sizeof(srv)) != 0) {
        ottd_log("macsrv: bind FAILED errno=%d", errno); mac_closesocket(lfd); return;
    }
    if (listen(lfd, 5) != 0) {
        ottd_log("macsrv: listen FAILED errno=%d", errno); mac_closesocket(lfd); return;
    }
    ottd_log("macsrv: listening 0.0.0.0:9999 (loopback self-connect)");

    /* ---- 2. Client: connect to 127.0.0.1:9999 (non-blocking) ---- */
    cfd = socket(AF_INET, SOCK_STREAM, 0);
    if (cfd < 0) { ottd_log("macsrv: client socket FAILED"); mac_closesocket(lfd); return; }
    memset(&lo, 0, sizeof(lo));
    lo.sin_family = AF_INET; lo.sin_port = htons(9999); lo.sin_addr.s_addr = htonl(0x7F000001u); /* 127.0.0.1 */
    errno = 0;
    i = connect(cfd, (struct sockaddr *)&lo, sizeof(lo));
    ottd_log("macsrv: client connect(127.0.0.1:9999) -> r=%d errno=%d", i, errno);

    /* ---- 3. Drive accept + client-writable together, ~10s cap ---- */
    for (i = 0; i < 200; i++) {
        if (sfd < 0) {
            clilen = sizeof(cli); memset(&cli, 0, sizeof(cli));
            sfd = accept(lfd, (struct sockaddr *)&cli, &clilen);
            if (sfd >= 0) {
                unsigned long h = (unsigned long)cli.sin_addr.s_addr;
                ottd_log("macsrv: ACCEPTED sfd=%d from %lu.%lu.%lu.%lu:%u worker_epstate=%d(5=DATAXFER)",
                         sfd, (h>>24)&0xff, (h>>16)&0xff, (h>>8)&0xff, h&0xff,
                         (unsigned)cli.sin_port, ms_dbg_epstate(sfd));
            }
        }
        if (!connected) {
            FD_ZERO(&wr); FD_SET(cfd, &wr); tv.tv_sec = 0; tv.tv_usec = 0;
            if (select(FD_SETSIZE, (fd_set *)0, &wr, (fd_set *)0, &tv) > 0 && FD_ISSET(cfd, &wr)) {
                so_err = -1; sl = sizeof(so_err); getsockopt(cfd, SOL_SOCKET, SO_ERROR, &so_err, &sl);
                connected = 1;
                ottd_log("macsrv: client writable SO_ERROR=%d client_epstate=%d", so_err, ms_dbg_epstate(cfd));
            }
        }
        if (sfd >= 0 && connected) break;
        if ((i % 20) == 0)
            ottd_log("macsrv: setup i=%d sfd=%d connected=%d listener_look=0x%x", i, sfd, connected, ms_dbg_look(lfd));
        ottd_cooperative_sleep_us(50000);
    }
    if (sfd < 0 || !connected) {
        ottd_log("REGRESS TCP-loopback: FAIL (setup: accept sfd=%d connected=%d)", sfd, connected);
        if (sfd >= 0) mac_closesocket(sfd); mac_closesocket(cfd); mac_closesocket(lfd); return;
    }

    /* ---- 4. client -> server ---- */
    int c2s_ok = 0, s2c_ok = 0;
    errno = 0; n = send(cfd, ping, strlen(ping), 0);
    ottd_log("macsrv: client send -> %ld errno=%d (want %d)", (long)n, errno, (int)strlen(ping));
    for (j = 0; j < 100; j++) {
        n = recv(sfd, buf, sizeof(buf) - 1, 0);
        if (n > 0)  { buf[n] = 0; ottd_log("macsrv: server recv %ld: %s", (long)n, buf); c2s_ok = (n == (ssize_t)strlen(ping)); break; }
        if (n == 0) { ottd_log("macsrv: server saw EOF"); break; }
        ottd_cooperative_sleep_us(50000);
    }
    if (j == 100) ottd_log("macsrv: server got NO data (recv timeout)");

    /* ---- 5. server -> client ---- */
    errno = 0; n = send(sfd, pong, strlen(pong), 0);
    ottd_log("macsrv: server send -> %ld errno=%d (want %d)", (long)n, errno, (int)strlen(pong));
    for (j = 0; j < 100; j++) {
        n = recv(cfd, buf, sizeof(buf) - 1, 0);
        if (n > 0)  { buf[n] = 0; ottd_log("macsrv: client recv %ld: %s", (long)n, buf); s2c_ok = (n == (ssize_t)strlen(pong)); break; }
        if (n == 0) { ottd_log("macsrv: client saw EOF"); break; }
        ottd_cooperative_sleep_us(50000);
    }
    if (j == 100) ottd_log("macsrv: client got NO reply (recv timeout)");

    mac_closesocket(sfd); mac_closesocket(cfd); mac_closesocket(lfd);
    ottd_log("REGRESS TCP-loopback: %s (listen/accept/connect/recv/send: c2s=%d s2c=%d)",
             (c2s_ok && s2c_ok) ? "PASS" : "FAIL", c2s_ok, s2c_ok);
}
