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

/* B2 Mac-side main: Toolbox init, then hand off to the game side (b2_run). */
#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Events.h>
#include <MacMemory.h>

/* Bumped every deploy so the sink trace unambiguously identifies which binary ran
 * (a stale previously-decoded app on the Mac produces confusingly identical traces). */
#define B2_BUILD_TAG "R1-92"

/* ROOT-CAUSE FIX ATTEMPT for the intermittent static-init anomaly:
 * C++ static constructors (blitter/driver registries, factory std::strings)
 * run BEFORE main() and ALLOCATE — but the app heap is ~544 bytes until
 * MaxApplZone() (first line of main) grows it. Pre-main allocations spill
 * into temporary memory / movable handles and get trampled later = corrupt
 * or empty registries, garbage statics, moving Type 2s. Grow the heap in a
 * priority constructor so it precedes every other static ctor. */
void MaxApplZone(void);
long b2_mb_early_before = 1, b2_mb_early_after = 1; /* ctor-order telemetry (.data on purpose) */

/* .bss corruption scanner: scan A runs in the priority ctor (before every other
 * static ctor and before Open Transport init); scan B runs in main right after
 * ottd_log_init. Garbage at A = loader/startup problem; garbage only at B = OT
 * (or something in early main) tramples game .bss. Results parked in .data. */
/* No _sbss/_ebss on the XCOFF link. Instead: &main = its transition vector,
 * which lives in the DATA section at a link-time VMA we bake in via a two-pass
 * build (-DB2_ANCHOR_VMA=... etc). runtime data base = (char*)&main - ANCHOR. */
#ifndef B2_ANCHOR_VMA
#define B2_ANCHOR_VMA 0 /* pass 1: scanner disabled */
#define B2_BSS_START_VMA 0
#define B2_BSS_END_VMA 0
#endif
#define SCAN_KEEP 8
static long g_scanA_nonzero = -1;                    /* .data: init'd */
static unsigned long g_scanA_off[SCAN_KEEP] = {1};
static unsigned long g_scanA_val[SCAN_KEEP] = {1};

/* Data-section anchor: storage VMA identified in the pass-1 binary by its magic
 * content, then baked in as B2_ANCHOR_VMA for pass 2. */
unsigned long b2_anchor_var = 0xB2ACB2ACul;

static long bss_scan(unsigned long *off, unsigned long *val)
{
    char *data_base;
    unsigned long *p, *e;
    long nz = 0;
    if (B2_ANCHOR_VMA == 0) return -2; /* pass-1 binary: scanner off */
    data_base = (char *)&b2_anchor_var - B2_ANCHOR_VMA;
    p = (unsigned long *)(data_base + B2_BSS_START_VMA);
    e = (unsigned long *)(data_base + B2_BSS_END_VMA);
    while (p < e) {
        if (*p != 0) {
            if (nz < SCAN_KEEP) { off[nz] = (unsigned long)((char *)p - data_base); val[nz] = *p; }
            nz++;
        }
        p++;
    }
    return nz;
}

__attribute__((constructor(101)))
static void b2_early_heap(void)
{
    g_scanA_nonzero = bss_scan(g_scanA_off, g_scanA_val); /* before anything else touches .bss */
    b2_mb_early_before = (long)MaxBlock();
    MaxApplZone();
    b2_mb_early_after = (long)MaxBlock();
}

/* Exposed so game-side TUs can sample the heap without Mac headers. */
long b2_maxblock(void) { return (long)MaxBlock(); }

extern void ottd_log_init(const char *path);
extern void ottd_log(const char *fmt, ...);
extern void ottd_log_close(void);
extern int  b2_run(void);
extern void macdir_selftest(void);
extern void macnet_selftest(void);
extern void macnet_tcp_test(void);
extern void macnet_tcp_accept_test(void);

int main(void)
{
    MaxApplZone(); /* FIRST: Retro68 never grows the app heap into the SIZE partition */
    ottd_log_init("ottd-r1.txt");
    ottd_log("=== B2 build %s: R1 LIVE town (engine ticks each frame) ===", B2_BUILD_TAG);
    {
        unsigned long boff[SCAN_KEEP], bval[SCAN_KEEP];
        long bnz = bss_scan(boff, bval);
        int i;
        ottd_log("bss scan: A(pre-ctor) nz=%ld, B(post-otinit) nz=%ld, bss 0x%lx-0x%lx",
                 g_scanA_nonzero, bnz, (unsigned long)B2_BSS_START_VMA, (unsigned long)B2_BSS_END_VMA);
        for (i = 0; i < SCAN_KEEP && i < g_scanA_nonzero; i++)
            ottd_log("bss A[%d]: +0x%lx = 0x%lx", i, g_scanA_off[i], g_scanA_val[i]);
        for (i = 0; i < SCAN_KEEP && i < bnz; i++)
            ottd_log("bss B[%d]: +0x%lx = 0x%lx", i, boff[i], bval[i]);
    }
    InitGraf(&qd.thePort); InitFonts(); InitWindows(); InitMenus(); InitCursor();
    ottd_log("Toolbox inited; FreeMem=%ld MaxBlock=%ld", (long)FreeMem(), (long)MaxBlock());

    macdir_selftest(); /* Seam #3: prove File Manager opendir/readdir works */
    /* Seam #2 sockets regression (grep the sink for "REGRESS"):
     *   UDP           -- sendto to the log-sink
     *   TCP-loopback  -- listen/accept/connect/recv/send on 127.0.0.1 (always self-contained)
     *   TCP-connect   -- client connect+send to 10.0.10.69:9999 (SKIP unless `nc -l 9999` there) */
    ottd_log("REGRESS: sockets seam begin");
    macnet_selftest();         /* UDP */
    macnet_tcp_accept_test();  /* TCP-loopback (core, always runs) */
    macnet_tcp_test();         /* TCP-connect (optional external peer) */
    ottd_log("REGRESS: sockets seam end");

    int rc = b2_run();

    ottd_log("=== B2 done rc=%d; ExitToShell (skip static dtors) ===", rc);
    ottd_log_close();
    ExitToShell();
    return 0;
}
