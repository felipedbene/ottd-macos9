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

/* Debug throttle: OT non-blocking UDP drops under burst, so yield ~1 tick after
 * each send to let the stack drain -> every line reliably reaches the sink.
 * Bring-up only; drop for production fire-and-forget. */
#define NETLOG_THROTTLE 1

/* statsd.c (compiled alongside, also against the OT headers) */
extern OSErr statsd_open(const char *ip, unsigned short port);
extern OSErr statsd_log_open(const char *ip, unsigned short port);
extern void  statsd_log(const char *buf, int len);
extern void  statsd_close(void);

/* k8s log-sink (MetalLB VIP). This vintage Mac already reaches it via MiniVNC. */
#define LOG_HOST "10.0.100.114"
#define LOG_PORT 5514

static char g_path[256];
static int  g_net = 0;   /* 1 once the UDP endpoint is open */

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
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    if (n > (int)sizeof(buf)) n = (int)sizeof(buf);

    /* UDP confirmed working -> pure fire-and-forget (no per-line File Manager churn). */
    if (g_net) {
        statsd_log(buf, n);
#if NETLOG_THROTTLE
        { unsigned long t = TickCount(); while (TickCount() == t) { } }  /* yield ~1 tick so OT drains */
#endif
    } else {
        file_line(buf, n);          /* only fall back to file if OT never came up */
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
