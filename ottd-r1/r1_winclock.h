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
