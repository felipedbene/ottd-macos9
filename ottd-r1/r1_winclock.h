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

/* R1 window-system clock shim. Retro68/newlib's std::chrono::steady_clock is broken
 * on Mac OS 9 (HW-verified in video_driver.hpp) — so window.cpp's UpdateWindows would
 * see delta_ms==0 and never draw (black screen). We build window.cpp with every
 * `std::chrono::steady_clock` sed-replaced by this R1SteadyClock, which is paced off
 * the 60.15 Hz Toolbox tick (TickCount()*16625 us) — the SAME source as the driver's
 * working TickClock. r1_now_us() is provided Mac-side. */
#ifndef R1_WINCLOCK_H
#define R1_WINCLOCK_H
#include <chrono>
extern "C" long long r1_now_us(void);   /* Mac-side: TickCount() * 16625 (microseconds) */
struct R1SteadyClock {
	using duration   = std::chrono::microseconds;
	using rep        = duration::rep;
	using period     = duration::period;
	using time_point = std::chrono::time_point<R1SteadyClock>;
	static const bool is_steady = true;
	static time_point now() { return time_point(duration(r1_now_us())); }
};
#endif
