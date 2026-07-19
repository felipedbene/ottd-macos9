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
 * m1_cmd_stubs.cpp — the 3 symbols the header-only Command<>::Do dispatch
 * template (command_func.h) needs from command.cpp.
 *
 * command.cpp itself is NOT compiled: its giant constexpr _command_proc_table
 * (~200 entries) segfaults this Retro68 ld. But Command<Tcmd>::Do is entirely
 * in the header, so build 7 executes a REAL command (CMD_TOWN_GROWTH_RATE, whose
 * proc is already compiled in town_cmd.o) through it, needing only:
 *   - RecursiveCommandCounter::_counter : real storage (the recursion guard).
 *   - InternalDoBefore / InternalDoAfter : peripheral hooks. Headless they are
 *     correctly no-ops — DoBefore clears the (empty) _cleared_object_areas and
 *     toggles town-rating test mode (no companies); DoAfter subtracts company
 *     money (no real company / OWNER_DEITY). The real command proc still runs.
 */
#include "stdafx.h"
#include "command_type.h"
#include "command_func.h"

int RecursiveCommandCounter::_counter = 0;
void CommandHelperBase::InternalDoBefore(bool, bool) {}
void CommandHelperBase::InternalDoAfter(CommandCost &, DoCommandFlag, bool, bool) {}
