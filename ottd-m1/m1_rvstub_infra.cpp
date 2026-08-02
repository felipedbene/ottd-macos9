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
 * m1_rvstub_infra.cpp — the infrastructure half of the link surface that the REAL
 * roadveh_cmd.cpp / vehicle.cpp / engine.cpp / order_cmd.cpp / roadstop.cpp / rail.cpp
 * open up. Those TUs are honest OpenTTD code, so they reach for whole subsystems R1
 * simply does not have: the AI/script VM, the gamelog, the link graph, order backups,
 * timetables and the signal solver. Compiling those subsystems would drag the network
 * stack, the savegame chunk registry and squirrel onto a 300 MHz G3 — so instead each
 * absent subsystem gets one small, deliberate answer here.
 *
 * A stub is only honest if it says "nothing happened" in a way the caller can survive:
 * no AI to notify, no gamelog to append to, no link graph to refresh, no backed-up
 * orders to clear, no timetable to advance, and — because R1 lays plain rail and never
 * places a signal — every segment is permanently free. All of those genuinely are no-ops
 * for this port, not shortcuts we are hiding.
 *
 * THREE of the sixteen are NOT no-ops, because a zero here would silently break the game:
 *
 *   1. GroundVehicle<RoadVehicle, VEH_ROAD>::GetAcceleration() — this sits on the real
 *      road-vehicle movement path. Returning 0 would freeze every bus forever.
 *   2. _bridge — a real (zeroed) table, because GetBridgeSpec() indexes it unconditionally.
 *   3. The three roadtype queries — a 0 mask reads as "roads are unavailable", which makes
 *      the real code refuse to build or run anything on the one road type R1 has.
 */
#include "stdafx.h"

#include "bridge.h"              /* BridgeSpec, MAX_BRIDGES */
#include "rail.h"                /* _railtypes_hidden_mask, RailTypes */
#include "road.h"                /* RoadTramType, HasAnyRoadTypesAvail */
#include "road_func.h"           /* GetCompanyRoadTypes, AddDateIntroducedRoadTypes */
#include "roadveh.h"             /* RoadVehicle */
#include "ground_vehicle.hpp"    /* GroundVehicle<> */
#include "effectvehicle_base.h"  /* EffectVehicle */
#include "vehiclelist.h"         /* VehicleListIdentifier */
#include "order_backup.h"        /* OrderBackup */
#include "timetable.h"           /* UpdateVehicleTimetable */
#include "signal_func.h"         /* SigSegState, UpdateSignalsOnSegment */
#include "gamelog.h"             /* GamelogGRFBugReverse */
#include "newgrf.h"              /* ReloadNewGRFData */
#include "linkgraph/refresh.h"   /* LinkRefresher */
#include "ai/ai.hpp"             /* AI */

#include "safeguards.h"

/* ------------------------------------------------------------------------------------ *
 * REAL #1 — the bridge table.
 *
 * No bridges exist in R1; if bridges are ever added this table must come from
 * tunnelbridge_cmd.cpp instead. There is no tunnelbridge draw proc in the R1 scene, so a
 * bridge tile would render as a "?" placeholder — which is exactly why we do not build
 * them. The table still has to EXIST and be indexable, because GetBridgeSpec() in bridge.h
 * asserts only on the index bound and then dereferences: an all-zero spec means "avail_year
 * 0, length 0, price 0, no sprite", i.e. a bridge type nothing can ever select.
 * ------------------------------------------------------------------------------------ */
BridgeSpec _bridge[MAX_BRIDGES] = {};

/* No NewGRF rail types are loaded, so no rail type is hidden from the build toolbars.
 * The real value is computed by rail_cmd.cpp's InitRailTypes() from RTF_HIDDEN flags. */
RailTypes _railtypes_hidden_mask = RAILTYPES_NONE;

/* ------------------------------------------------------------------------------------ *
 * REAL #2 — the road type availability trio.
 *
 * R1 has exactly one road type, ROADTYPE_ROAD, and it is available from the first tick to
 * everybody. The important part is what we must NOT return: an empty mask. The real code
 * reads an empty mask as "this company owns no road technology", which makes it refuse to
 * lay road, refuse to build a stop and refuse to accept a bus — so the honest answer for a
 * port that unconditionally has plain road is a mask that actually contains it.
 * ------------------------------------------------------------------------------------ */

/* Every company always has plain road; introduction dates are meaningless without a
 * NewGRF road type table, so the `introduces` distinction collapses to the same answer. */
RoadTypes GetCompanyRoadTypes(CompanyID company, bool introduces)
{
	return ROADTYPES_ROAD;
}

/* Roads: yes, always. Trams: no — R1 has no tram type, and claiming otherwise would let
 * the GUI offer a tram build tool that can never produce a valid tile. */
bool HasAnyRoadTypesAvail(CompanyID company, RoadTramType rtt)
{
	return rtt == RTT_ROAD;
}

/* Nothing is introduced over time when the only road type is there from the start; the
 * caller's set is returned untouched apart from guaranteeing plain road is in it. */
RoadTypes AddDateIntroducedRoadTypes(RoadTypes current, Date date)
{
	return current | ROADTYPES_ROAD;
}

/* ------------------------------------------------------------------------------------ *
 * REAL #3 — road vehicle acceleration.
 *
 * This is the one stub on the actual movement path: RoadVehicle::UpdateSpeed() feeds the
 * return value straight into DoUpdateSpeed(), so this number is the only thing that lets a
 * bus leave a standstill. The real implementation (ground_vehicle.cpp) solves power vs.
 * mass vs. rolling friction vs. air drag vs. slope resistance and returns 0 when force
 * exactly balances resistance. R1 has none of the inputs that make that meaningful: no
 * realistic-acceleration physics, no per-vehicle slope model, no breakdowns and no NewGRF
 * power/weight properties — every cache field it would read is zero, so the honest
 * physics answer would come out 0 and every bus on the map would sit frozen forever.
 *
 * So we return a small POSITIVE constant instead: the bus accelerates gently, reaches its
 * max speed (DoUpdateSpeed clamps there) and then holds it, which is precisely the
 * behaviour R1 wants from a vehicle with no physics behind it. This must be an explicit
 * specialisation rather than a plain function because the member belongs to the
 * GroundVehicle<> class template.
 * ------------------------------------------------------------------------------------ */
template <>
int GroundVehicle<RoadVehicle, VEH_ROAD>::GetAcceleration() const
{
	return 4;
}

/* ------------------------------------------------------------------------------------ *
 * The honest no-ops: subsystems that do not exist in this port.
 * ------------------------------------------------------------------------------------ */

/* No AI, no script VM, nobody subscribed. The event object is dropped on the floor: we
 * cannot Release() it without pulling in the whole squirrel-backed ScriptEvent hierarchy,
 * and R1 never constructs one in the first place — this only exists so the real command
 * code that *would* announce a crash or a new vehicle still links. */
void AI::NewEvent(CompanyID company, ScriptEvent *event) {}

/* No gamelog: R1 never loads a savegame and never reverses a NewGRF bug workaround, so
 * there is nothing to record and nothing changed. */
bool GamelogGRFBugReverse(uint32 grfid, uint16 internal_id)
{
	return false;
}

/* No NewGRFs are loaded at all (the base GRF is read directly), so there is nothing to
 * reload; the real one lives in saveload/afterload.cpp, which this port does not compile. */
void ReloadNewGRFData() {}

/* No link graph and no cargo distribution: R1 moves passengers with a single hand-driven
 * bus, so there are no capacity/usage edges to refresh along a vehicle's order list. */
/* static */ void LinkRefresher::Run(Vehicle *v, bool allow_merge, bool is_full_loading) {}

/* No order backups. Those only matter when a depot/station is rebuilt and the game wants
 * to restore the orders a vehicle had before — R1 has no such editing flow. */
/* static */ void OrderBackup::ClearVehicle(const Vehicle *v) {}
/* static */ void OrderBackup::RemoveOrder(OrderType type, DestinationID destination, bool hangar) {}

/* No timetables: R1 vehicles have no scheduled arrival/departure to reconcile, so a
 * vehicle finishing a leg of its journey has nothing to update or report as late. */
void UpdateVehicleTimetable(Vehicle *v, bool travelling) {}

/* No signals. R1 lays plain rail and never places a signal, so every segment is
 * permanently and unconditionally free — SIGSEG_FULL would falsely brake trains and
 * SIGSEG_PBS would imply a reservation system that is not there. */
SigSegState UpdateSignalsOnSegment(TileIndex tile, DiagDirection side, Owner owner)
{
	return SIGSEG_FREE;
}

/* Effect vehicles (smoke, sparks, bulldozer dust) are not spawned by R1, and the real
 * lookup indexes a NewGRF-influenced transparency table we do not build. TO_INVALID means
 * "no transparency option governs me", i.e. never hidden by a transparency toggle. */
TransparencyOption EffectVehicle::GetTransparencyOption() const
{
	return TO_INVALID;
}

/* Vehicle lists are only packed into a window number so that two list windows can be told
 * apart. R1 opens its own single vehicle list (m1_vehicle_list_gui.cpp) and never packs an
 * identifier, so a constant 0 is enough; if several real list windows are ever opened at
 * once they would all collapse onto the same window number and this must become the real
 * bit-packing from vehiclelist.cpp. */
uint32 VehicleListIdentifier::Pack() const
{
	return 0;
}
