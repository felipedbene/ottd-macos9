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
 * m1_depot.cpp — a REAL pooled Depot (road), the m1_industry / m1_company way:
 * own the real DepotPool + a minimal Depot WITHOUT compiling depot.cpp (order-
 * backup / vehicle-list / window cascade). depot.cpp also owns ~Depot + the pool;
 * those live here instead.
 *
 * Place one from R1 (scene not wired yet) with:
 *   unsigned id = r1_make_depot(tile, DIAGDIR_NE);  // exit faces NE; tile becomes MP_ROAD depot
 * FindClosestDepot then sees it via YapfRoadVehicleFindNearestDepot in
 * m1_realveh_stubs.cpp (Manhattan over Depot::Iterate — no YAPF).
 */
#include "stdafx.h"
#include "depot_base.h"
#include "road_map.h"                    /* MakeRoadDepot, IsRoadDepotTile */
#include "rail_map.h"                    /* MakeRailDepot (rung 6) */
#include "company_func.h"                /* _local_company */
#include "town.h"                        /* MakeDefaultName / ClosestTownFromTile */
#include "date_func.h"                   /* _date */
#include "viewport_func.h"               /* MarkTileDirtyByTile */
#include "pathfinder/pathfinder_type.h"  /* FindDepotData */
#include "vehicle_base.h"
#include "map_func.h"                    /* DistanceManhattan */
#include "core/pool_func.hpp"

#include "safeguards.h"

/* Real Depot pool — replaces the fake `char _depot_pool[8192]` deadpool (guarded
 * out of m1_deadpools.c under R1_MERGE). depot.cpp is NOT compiled, so no ODR clash.
 * MUST keep the deadpool guarded: XCOFF silently merges same-named BSS. */
DepotPool _depot_pool("Depot");
INSTANTIATE_POOL_METHODS(Depot)

/* Empty dtor (real one in depot.cpp closes depot windows + sweeps orders). Safe:
 * R1 never deletes depots at runtime, and nothing this minimal Depot allocates
 * needs teardown. Moved here from m1_road_stubs.cpp so the pool methods and the
 * dtor share one owner. */
Depot::~Depot() {}

/* Stand up ONE real road depot: pool object + MP_ROAD depot tile. Returns DepotID,
 * or 0xFFFF if the pool is full / tile/dir invalid. `dir` is the exit DiagDirection
 * (DIAGDIR_NE/SE/SW/NW). Does NOT call CmdBuildRoadDepot (no clear/cost/infra) —
 * caller should hand a clearable tile (grass or existing road is fine; MakeRoadDepot
 * overwrites the map cell). */
extern "C" unsigned r1_make_depot(unsigned tile, int dir)
{
	if (!IsValidDiagDirection((DiagDirection)dir)) return 0xFFFF;
	if (!Depot::CanAllocateItem()) return 0xFFFF;

	Depot *dep = new Depot((TileIndex)tile);
	dep->build_date = _date;

	MakeRoadDepot((TileIndex)tile, _local_company, dep->index,
	              (DiagDirection)dir, ROADTYPE_ROAD);
	MarkTileDirtyByTile((TileIndex)tile);
	MakeDefaultName(dep);

	return (unsigned)dep->index;
}

/* Rail twin of r1_make_depot (rung 6): a real Depot pool object + a raw
 * MakeRailDepot tile write (exit facing `dir`, base rail type). Same contract:
 * no CmdBuildTrainDepot (no clear/cost checks) — hand it a clearable tile. */
extern "C" unsigned r1_make_rail_depot(unsigned tile, int dir)
{
	if (!IsValidDiagDirection((DiagDirection)dir)) return 0xFFFF;
	if (!Depot::CanAllocateItem()) return 0xFFFF;

	Depot *dep = new Depot((TileIndex)tile);
	dep->build_date = _date;

	MakeRailDepot((TileIndex)tile, _local_company, dep->index,
	              (DiagDirection)dir, (RailType)0);
	MarkTileDirtyByTile((TileIndex)tile);
	MakeDefaultName(dep);

	return (unsigned)dep->index;
}

/* How many Depot objects are in the pool (diagnostic). */
extern "C" unsigned r1_depot_count(void)
{
	return (unsigned)Depot::GetNumItems();
}

/* Nearest road depot by Manhattan distance. Used by YapfRoadVehicleFindNearestDepot
 * (YAPF not linked). max_distance <= 0 means unlimited. Only considers depots whose
 * xy is still an IsRoadDepotTile (so GetDepotIndex in FindClosestDepot is safe). */
FindDepotData r1_find_nearest_road_depot(const Vehicle *v, int max_distance)
{
	if (v == nullptr) return FindDepotData();

	FindDepotData best;
	for (const Depot *d : Depot::Iterate()) {
		if (d->xy == INVALID_TILE || !IsRoadDepotTile(d->xy)) continue;
		uint dist = DistanceManhattan(v->tile, d->xy);
		if (max_distance > 0 && dist > (uint)max_distance) continue;
		if (dist < best.best_length) best = FindDepotData(d->xy, dist);
	}
	return best;
}
