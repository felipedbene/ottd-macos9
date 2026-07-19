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
