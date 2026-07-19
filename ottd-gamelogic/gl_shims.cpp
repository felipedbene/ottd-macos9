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

// Minimal environment for OpenTTD's map subsystem to link standalone on Mac OS 9.
// map.cpp / tile_map.cpp reference a few globals + one error function; provide them.
#include "stdafx.h"
#include "settings_type.h"
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <string>

extern "C" void ottd_log(const char *fmt, ...);

int _debug_map_level = 0;
GameSettings _settings_game;   // zero-init; map allocation doesn't depend on real values

// Route OpenTTD's Debug() output to the trace sink.
void DebugPrint(const char *level, const std::string &msg) { ottd_log("dbg %s: %s", level ? level : "?", msg.c_str()); }

// Out-of-memory path (NORETURN); only fires if AllocateMap can't get memory.
void MallocError(size_t) { abort(); }

// OpenTTD's fatal-error path (NORETURN). Only fires on impossible states.
void NORETURN CDECL error(const char *str, ...)
{
    va_list ap; va_start(ap, str); vfprintf(stderr, str, ap); va_end(ap);
    abort();
}
