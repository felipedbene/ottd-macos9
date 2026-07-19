// M1 link shims: satisfy the GUI / window / network / sound / rendering surface
// that the REAL simulation loops reference but that must NOT run headless.
// Everything here is a no-op stub or a zero-initialised global. Real simulation
// code (date.cpp + the daily/monthly/yearly loops) is compiled, not stubbed.
//
// NORETURN stubs log to the UDP sink BEFORE aborting (stderr goes nowhere on
// classic Mac) — a lesson from b1_shims.cpp.
#include "stdafx.h"
#include "openttd.h"
#include "settings_type.h"
#include "window_type.h"
#include "window_func.h"
#include "currency.h"
#include "rail_gui.h"
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <string>

extern "C" void ottd_log(const char *fmt, ...);

// ---------------------------------------------------------------------------
// Core game-state globals (normally in openttd.cpp / saveload.cpp).
// ---------------------------------------------------------------------------
#ifndef R1_MERGE  /* real gfx.o owns these game-state globals in the render-merge build */
GameMode   _game_mode;
SwitchMode _switch_mode;
PauseMode  _pause_mode;
std::atomic<bool> _exit_game;
#endif
bool _do_autosave = false;
bool _networking = false;
bool _network_server = false;

ClientSettings _settings_client;
GameSettings   _settings_game;

// ---------------------------------------------------------------------------
// Fatal stubs (should never fire headless; log first, then abort).
// ---------------------------------------------------------------------------
#ifndef R1_MERGE  /* real b1_shims.o owns these in the render-merge build */
void NORETURN CDECL usererror(const char *s, ...)
{
	char buf[256]; va_list a; va_start(a, s); vsnprintf(buf, sizeof(buf), s, a); va_end(a);
	ottd_log("USERERROR: %s", buf);
	abort();
}
void NORETURN CDECL error(const char *s, ...)
{
	char buf[256]; va_list a; va_start(a, s); vsnprintf(buf, sizeof(buf), s, a); va_end(a);
	ottd_log("ERROR: %s", buf);
	abort();
}
void MallocError(size_t)  { abort(); }
void ReallocError(size_t) { abort(); }
#endif

// saveload corrupt-file abort (referenced by core/pool_func.cpp; never reached headless).
void NORETURN CDECL SlErrorCorruptFmt(const char *s, ...)
{
	char buf[256]; va_list a; va_start(a, s); vsnprintf(buf, sizeof(buf), s, a); va_end(a);
	ottd_log("SLERROR: %s", buf);
	abort();
}

// ---------------------------------------------------------------------------
// Debug output (real impl lives in debug.cpp; route to the sink).
// ---------------------------------------------------------------------------
int _debug_map_level = 0;
#ifndef R1_MERGE  /* real b1_shims.o owns DebugPrint in the render-merge build */
void DebugPrint(const char *level, const std::string &msg) { ottd_log("dbg %s: %s", level ? level : "?", msg.c_str()); }
#endif

// ---------------------------------------------------------------------------
// Window / GUI dirty + invalidate calls (no windows exist headless -> no-ops).
// ---------------------------------------------------------------------------
#ifndef R1_MERGE  /* real window.cpp provides these when the window system is linked */
void SetWindowDirty(WindowClass, WindowNumber) {}
void SetWindowClassesDirty(WindowClass) {}
void SetWindowWidgetDirty(WindowClass, WindowNumber, byte) {}
void InvalidateWindowClassesData(WindowClass, int, bool) {}
#endif
void ShowEndGameChart() {}                 // locally-declared in date.cpp; GUI, never reached (ending_year=0)
void ResetSignalVariant(int32) {}          // rail_gui; no signals headless

// ---------------------------------------------------------------------------
// Currency (auto_euro off by default -> never reached).
// ---------------------------------------------------------------------------
void CheckSwitchToEuro() {}

// ---------------------------------------------------------------------------
// Network server loops (headless single-player -> _network_server is false, so
// these are never called; stubbed to bound the network cascade).
// ---------------------------------------------------------------------------
void NetworkInitChatMessage() {}
void NetworkServerDailyLoop() {}
void NetworkServerMonthlyLoop() {}
void NetworkServerYearlyLoop() {}

// ---------------------------------------------------------------------------
// Simulation daily/monthly/yearly loops.
//
// These live in the big command TUs (town_cmd, industry_cmd, station_cmd,
// vehicle, company_cmd, disaster_vehicle, engine, subsidy). Compiling any of
// them drags an unbounded cascade: NewGRF callbacks, tile drawing/viewport
// sprites, pathfinding (YAPF), orders, cargo packets, news, sound, GUI windows
// (first link attempt surfaced 357 such symbols). M1's goal is a headless
// calendar tick through the REAL IncreaseDate/OnNewDay/OnNewMonth/OnNewYear
// control flow (date.cpp is compiled for real) — not the full simulation — so
// every loop body is stubbed to bound the link. Each is a genuine no-op: with
// no towns/industries/vehicles/stations created, the real bodies would iterate
// empty pools anyway.
// ---------------------------------------------------------------------------
void CompaniesMonthlyLoop() {}   // company_cmd.cpp (also a network cascade)
void CompaniesYearlyLoop() {}
// TownsMonthlyLoop / TownsYearlyLoop: REAL now (town_cmd.cpp compiled, build 6).
void IndustryDailyLoop() {}       // industry_cmd.cpp
void IndustryMonthlyLoop() {}
void StationMonthlyLoop() {}      // station_cmd.cpp
void SubsidyMonthlyLoop() {}      // subsidy.cpp
void DisasterDailyLoop() {}       // disaster_vehicle.cpp
void EnginesDailyLoop() {}        // engine.cpp
void EnginesMonthlyLoop() {}
void VehiclesYearlyLoop() {}      // vehicle.cpp

// ---------------------------------------------------------------------------
// (further stubs appended as the iterative link surfaces them)
// ---------------------------------------------------------------------------
