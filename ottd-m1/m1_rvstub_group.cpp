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
 * m1_rvstub_group.cpp — the group / autoreplace / engine-preview corner of the link
 * surface opened by compiling the REAL vehicle TUs (roadveh_cmd.cpp, vehicle.cpp,
 * engine.cpp, order_cmd.cpp).
 *
 * Those TUs assume a full game: every vehicle belongs to a *group*, whose statistics
 * are recounted on every build/sell/age event; every company may own *autoreplace*
 * rules that swap old engines for new; and every newly invented engine offers itself
 * to a human through an *engine-preview* window. R1 has none of the three. There is
 * no group pool (Vehicle::group_id never leaves DEFAULT_GROUP — see m1_vehicle.cpp),
 * no autoreplace UI to invalidate, and no exclusive-preview dialog to show, because
 * R1's fleet is created directly by r1main.c rather than bought from a build window.
 *
 * So these are honest absences, not shortcuts: the counters count for a group system
 * that is never created, the commands report failure because the player can never
 * reach them, and EngineReplacement always answers "no replacement configured" —
 * which is the truthful answer when no company owns an engine_renew_list. Linking
 * the real autoreplace_cmd.cpp / group_cmd.cpp / engine_gui.cpp instead would drag
 * the whole GUI + savegame + NewGRF-callback cascade for behaviour nobody can see.
 *
 * Keep this file symbol-disjoint from the other m1_*stub*.cpp files — each one owns
 * its slice of the missing subsystem, and duplicate definitions break the link.
 */
#include "stdafx.h"

#include "command_func.h"
#include "autoreplace_cmd.h"
#include "autoreplace_func.h"
#include "autoreplace_gui.h"
#include "vehicle_cmd.h"
#include "engine_gui.h"
#include "engine_type.h"
#include "group.h"
#include "group_gui.h"
#include "depot_func.h"
#include "table/strings.h"

#include "safeguards.h"

/* Declared privately in engine.cpp (no header) — matched verbatim. */
void ShowEnginePreviewWindow(EngineID engine);

/* --- Group statistics -------------------------------------------------------
 * Bookkeeping for per-company/per-group caches that R1 never allocates. Nothing
 * reads them, so counting is pure cost. */
void GroupStatistics::CountVehicle(const Vehicle *, int) {}
void GroupStatistics::CountEngine(const Vehicle *, int) {}
void GroupStatistics::VehicleReachedMinAge(const Vehicle *) {}
void GroupStatistics::UpdateProfits() {}
void GroupStatistics::UpdateAutoreplace(CompanyID) {}

/* --- Autoreplace ------------------------------------------------------------
 * No company can own an engine_renew_list without the group/autoreplace GUI, so
 * "nothing to replace" is the correct answer rather than a lie. */
EngineID EngineReplacement(EngineRenewList, EngineID, GroupID, bool *replace_when_old)
{
	if (replace_when_old != nullptr) *replace_when_old = false;
	return INVALID_ENGINE;
}

CommandCost CmdAutoreplaceVehicle(DoCommandFlag, VehicleID)
{
	return CMD_ERROR;
}

/* Refitting needs the cargo/subtype capability machinery from vehicle_cmd.cpp's
 * RefitVehicle(); R1 buses carry one fixed cargo, so the command simply fails. */
#ifndef R1_REAL_DEPOT_GUI  /* vehicle_cmd.cpp owns the real one */
std::tuple<CommandCost, uint, uint16, CargoArray> CmdRefitVehicle(DoCommandFlag, VehicleID, CargoID, byte, bool, bool, uint8)
{
	return { CMD_ERROR, 0, 0, CargoArray() };
}
#endif

/* --- GUI notifications ------------------------------------------------------
 * Windows that do not exist cannot be invalidated, opened, or cleared of a
 * highlight for a vehicle that is going away. */
void AddRemoveEngineFromAutoreplaceAndBuildWindows(VehicleType) {}
void InvalidateAutoreplaceWindow(EngineID, GroupID) {}
void ShowEnginePreviewWindow(EngineID) {}
void DeleteGroupHighlightOfVehicle(const Vehicle *) {}
#ifndef R1_REAL_DEPOT_GUI  /* depot_gui.cpp owns the real one */
void DeleteDepotHighlightOfVehicle(const Vehicle *) {}
#endif

/* Only ever shown in the preview/build dialogs R1 omits. */
StringID GetEngineCategoryName(EngineID)
{
	return STR_NULL;
}
