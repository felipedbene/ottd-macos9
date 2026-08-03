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

/*
 * netlog.c — OpenTransport UDP log sink for the OpenTTD/Mac OS 9 port.
 *
 * Same extern "C" interface as maclog.cpp (ottd_log_init / ottd_log /
 * ottd_log_close), so no caller changes. Ships each trace line as a UDP
 * datagram to the k8s log-sink via MiniVNC's proven statsd-over-Open-Transport
 * emitter (statsd.c) — exactly the pattern casquinha/MiniVNC use.
 *
 * BRING-UP: also tees every line to the file next to the app, so if OT is down
 * or packets don't arrive we still have the local trace. Once the UDP path is
 * confirmed at the sink, the file tee can be dropped for pure fire-and-forget.
 *
 * Compiled with Apple's Universal Interfaces (otsdk/CIncludes) for <OpenTransport.h>,
 * standard C headers resolve to newlib — NOT via cmake add_application (which uses
 * multiversal and lacks OT).
 */
#include <OpenTransport.h>          /* InitOpenTransport / CloseOpenTransport */
#include <Events.h>                 /* TickCount (debug throttle) */
#include <stdio.h>                  /* newlib: fopen, vsnprintf */
#include <stdarg.h>
#include <string.h>

/* Debug throttle: OT non-blocking UDP used to drop under burst, so bring-up
 * yielded ~1 tick after each send. That cost ~0.5 s of busy-wait per beat at
 * ~35 lines/beat and starved the cooperative event loop. With sd_drain_looks()
 * clearing T_UDERR/T_GODATA/T_DATA, the spin is no longer needed. */
#define NETLOG_THROTTLE 0

/* statsd.c (compiled alongside, also against the OT headers) */
extern OSErr statsd_open(const char *ip, unsigned short port);
extern OSErr statsd_log_open(const char *ip, unsigned short port);
extern void  statsd_log(const char *buf, int len);
extern void  statsd_stats(long *ok, long *fail, long *err);
extern void  statsd_close(void);

/* k8s log-sink (MetalLB VIP). This vintage Mac already reaches it via MiniVNC. */
#define LOG_HOST "10.0.100.114"
#define LOG_PORT 5514

static char g_path[256];
static int  g_net = 0;   /* 1 once the UDP endpoint is open */
static unsigned long g_lines = 0;  /* lines through ottd_log (for NETSTAT cadence) */

/* Run tag stamped on every line as "[<tag>] ".
 *
 * The pool runs N guests at once and they all report to the ONE k8s sink, which
 * NATs every sender to the same address -- so the datagram's source tells you
 * nothing about which VM sent it. Without a per-line tag the only marker is the
 * boot banner, and a reader slicing "banner to next banner" silently mixes three
 * runs together: a 3-slot fan-out produced ticks [1, 384, 128] and "houses
 * decreased" for a slot that was in fact healthy. The tag is a compile-time
 * constant (each slot is built with its own), so this costs nothing at runtime. */
static char g_tag[32];

void ottd_log_set_tag(const char *tag)
{
    if (tag == 0) { g_tag[0] = '\0'; return; }
    strncpy(g_tag, tag, sizeof(g_tag) - 1);
    g_tag[sizeof(g_tag) - 1] = '\0';
}

static void file_line(const char *s, int len)
{
    FILE *f;
    if (g_path[0] == '\0') return;
    f = fopen(g_path, "a");
    if (!f) return;
    fputs("[ottd] ", f);
    if (len > 0) fwrite(s, 1, (size_t)len, f);
    fputc('\n', f);
    fclose(f);
}

void ottd_log_init(const char *path)
{
    FILE *f;
    OSErr err;

    strncpy(g_path, path ? path : "ottd-net.txt", sizeof(g_path) - 1);
    g_path[sizeof(g_path) - 1] = '\0';
    f = fopen(g_path, "w");
    if (f) { fputs("[ottd] log open\n", f); fclose(f); }

    /* Bring Open Transport up (application context) and open the UDP endpoint. */
    err = (OSErr)InitOpenTransport();
    if (err == 0) {
        if (statsd_open(LOG_HOST, LOG_PORT) == 0) {
            statsd_log_open(LOG_HOST, LOG_PORT);
            g_net = 1;
        }
    }
    {
        char m[64];
        int n = (int)sprintf(m, "net sink %s -> %s:%d (OT err=%d)",
                             g_net ? "UP" : "DOWN", LOG_HOST, LOG_PORT, (int)err);
        file_line(m, n);
        if (g_net) statsd_log(m, n);
    }
}

void ottd_log(const char *fmt, ...)
{
    char buf[512];
    int n;
    va_list ap;

    if (g_path[0] == '\0' && !g_net) return;

    /* "[tag] " first so every line is attributable to one run even when several
     * guests interleave into the shared sink. */
    n = 0;
    if (g_tag[0] != '\0')
        n = (int)sprintf(buf, "[%s] ", g_tag);

    va_start(ap, fmt);
    {
        int m = vsnprintf(buf + n, sizeof(buf) - (size_t)n, fmt, ap);
        if (m < 0) m = 0;
        n += m;
    }
    va_end(ap);
    if (n > (int)sizeof(buf)) n = (int)sizeof(buf);

    /* Always tee to the guest-disk file so a dead UDP sink still leaves a
     * postmortem. UDP alone is how we lost visibility into the ~tick-512 freeze. */
    file_line(buf, n);
    if (g_net) {
        statsd_log(buf, n);
#if NETLOG_THROTTLE
        { unsigned long t = TickCount(); while (TickCount() == t) { } }  /* yield ~1 tick so OT drains */
#endif
    }

    /* Every 32 lines, file-only counters — survives when the sink is the dying channel. */
    g_lines++;
    if ((g_lines & 31ul) == 0) {
        long ok = 0, fail = 0, err = 0;
        char m[96];
        int mn;
        statsd_stats(&ok, &fail, &err);
        mn = (int)sprintf(m, "NETSTAT: ok=%ld fail=%ld lasterr=%ld", ok, fail, err);
        file_line(m, mn);
    }
}

void ottd_log_close(void)
{
    file_line("log close", 9);
    if (g_net) {
        statsd_log("log close: statsd_close then CloseOpenTransport", 46);
        statsd_close();     /* OTUnbind + OTCloseProvider */
        g_net = 0;
    }
    CloseOpenTransport();   /* if the exit crash is here, the line above is the last at the sink */
    g_path[0] = '\0';
}
