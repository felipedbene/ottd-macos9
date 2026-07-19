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

// B2.0 — clock probe for the video driver.
// VideoDriver::Tick()/SleepTillNextTick() pace the whole game off std::chrono::steady_clock.
// Nobody has verified that clock on Retro68/newlib. Measure:
//   1. does steady_clock::now() advance at all, and at what granularity?
//   2. does it agree with TickCount (60.15 Hz) over real time?
//   3. what does clock() do, as a fallback candidate?
// Result decides whether video_driver.cpp needs a #if defined(macintosh) TickCount branch.
#include <Events.h>      // TickCount
#include <MacMemory.h>   // MaxApplZone
#include <chrono>

extern "C" void ottd_log_init(const char *path);
extern "C" void ottd_log(const char *fmt, ...);
extern "C" void ottd_log_close(void);

using steady = std::chrono::steady_clock;
static long us_between(steady::time_point a, steady::time_point b)
{
    return (long)std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
}

int main()
{
    MaxApplZone();
    ottd_log_init("ottd-b2probe.txt");
    ottd_log("=== B2.0 clock probe ===");

    // 1. granularity: spin until now() changes (bounded in case the clock is dead)
    for (int round = 0; round < 3; round++) {
        steady::time_point t0 = steady::now();
        steady::time_point t1 = t0;
        long spins = 0;
        while ((t1 = steady::now()) == t0 && spins < 20000000L) spins++;
        if (t1 == t0) {
            ottd_log("steady_clock: DEAD (no change after %ld spins)", spins);
        } else {
            ottd_log("steady_clock: min step %ld us (after %ld spins)", us_between(t0, t1), spins);
        }
    }

    // 2. agreement with TickCount: 8 rounds of 30 ticks (~499 ms each) busy-waited
    for (int round = 0; round < 8; round++) {
        unsigned long k0 = TickCount();
        steady::time_point c0 = steady::now();
        while (TickCount() < k0 + 30) { }
        unsigned long k1 = TickCount();
        steady::time_point c1 = steady::now();
        ottd_log("round %d: %lu ticks (expect ~499ms) = steady %ld us",
                 round, (unsigned long)(k1 - k0), us_between(c0, c1));
    }

    // 3. clock() fallback candidate: same 30-tick window
    {
        unsigned long k0 = TickCount();
        long c0 = (long)clock();
        while (TickCount() < k0 + 30) { }
        long c1 = (long)clock();
        ottd_log("clock(): %ld -> %ld over 30 ticks (CLOCKS_PER_SEC=%ld)", c0, c1, (long)CLOCKS_PER_SEC);
    }

    ottd_log("=== probe done ===");
    ottd_log_close();
    ExitToShell();
    return 0;
}
