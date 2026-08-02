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
 * m1_realveh_stubs.cpp — the link surface for the REAL vehicle stack.
 *
 * R1 now compiles the game's own roadveh_cmd.cpp + vehicle.cpp + engine.cpp +
 * order_cmd.cpp instead of the hand-rolled RoadVehicle/Order/Engine stubs that
 * stood in for them. Those four TUs reference ~90 symbols we do not want to drag
 * in: aircraft, ships, trains, autoreplace/groups, news, disasters, NewGRF
 * callbacks, timetables and the link graph. This file is that boundary.
 *
 * THE PATHFINDER IS NOT A STUB. roadveh_cmd.cpp reaches the pathfinder through
 * exactly two functions, so YAPF never has to be linked at all -- which is what
 * made the real road-vehicle stack look impossible (the plan called YAPF "the
 * true XCOFF-ld gamble", 4x nested CYapfT at yapf_road.cpp:525-529). Both entry
 * points are backed here by the BFS already proven in m1_pathfind.cpp.
 *
 * Everything else returns the value that means "this subsystem is not present":
 * no sound, no news, no articulated parts, no NewGRF override. They are reached
 * only by code paths R1 does not exercise, and each one that later DOES matter
 * should be replaced by the real TU, not fleshed out here.
 */
#include "stdafx.h"
#include "vehicle_base.h"
#include "vehicle_func.h"
#include "roadveh.h"
#include "ship.h"
#include "aircraft.h"
#include "train.h"
#include "engine_base.h"
#include "engine_func.h"
#include "station_base.h"
#include "roadstop_base.h"
#include "economy_func.h"
#include "company_base.h"
#include "town.h"
#include "news_func.h"
#include "newgrf_sound.h"
#include "effectvehicle_func.h"
#include "order_base.h"
#include "track_func.h"
#include "road_map.h"
#include "tile_map.h"
#include "pathfinder/yapf/yapf.h"
#include "pathfinder/npf/npf_func.h"

#include "safeguards.h"

/* ---------------------------------------------------------------------------
 * Pathfinder: the real reason this file exists.
 *
 * m1_pathfind.cpp's r1_road_path is a BFS over MP_ROAD tiles that already drives
 * every bus in the game. Here it answers the one question roadveh_cmd asks: from
 * `tile`, having entered from `enterdir`, which trackdir heads toward the
 * vehicle's destination? We path from `tile` to v->dest_tile, take the first hop,
 * and convert that step into a trackdir.
 * ------------------------------------------------------------------------- */
extern "C" int r1_road_path(uint from, uint to, unsigned short *out, int max_len);

/* Pick, out of the trackdirs the caller is OFFERING, the one whose exit leads to
 * `next`. Searching the offered mask rather than constructing a trackdir from a
 * direction means we can never hand back a trackdir the vehicle cannot take --
 * roadveh_cmd trusts this result blindly and would otherwise drive the bus into
 * a tile it has no connection to. */
static Trackdir R1TrackdirTowards(TileIndex tile, TileIndex next, TrackdirBits trackdirs)
{
	for (Trackdir td = TRACKDIR_BEGIN; td < TRACKDIR_END; td = (Trackdir)(td + 1)) {
		if (!HasBit(trackdirs, td)) continue;
		if (TileAddByDiagDir(tile, TrackdirToExitdir(td)) == next) return td;
	}
	return INVALID_TRACKDIR;
}

Trackdir YapfRoadVehicleChooseTrack(const RoadVehicle *v, TileIndex tile, DiagDirection,
		TrackdirBits trackdirs, bool &path_found, RoadVehPathCache &)
{
	unsigned short route[96];
	path_found = false;
	if (v != nullptr && v->dest_tile != INVALID_TILE) {
		int len = r1_road_path((uint)tile, (uint)v->dest_tile, route, 96);
		if (len >= 2) {
			Trackdir td = R1TrackdirTowards(tile, (TileIndex)route[1], trackdirs);
			if (td != INVALID_TRACKDIR) { path_found = true; return td; }
		}
	}
	/* No path (or already at the destination): keep moving rather than stall, and
	 * report path_found=false so the caller's own "lost vehicle" handling runs. */
	return (trackdirs != TRACKDIR_BIT_NONE) ? FindFirstTrackdir(trackdirs) : INVALID_TRACKDIR;
}

/* NPF is not reachable: _settings_game.pf.pathfinder_for_roadvehs is YAPF and R1
 * never changes it. Defined only to close the link. */
Trackdir NPFRoadVehicleChooseTrack(const RoadVehicle *, TileIndex, DiagDirection, bool &path_found)
{
	path_found = false;
	return INVALID_TRACKDIR;
}

/* No depots in R1 (m1_depot.cpp is a later rung), so "no depot found". */
FindDepotData YapfRoadVehicleFindNearestDepot(const RoadVehicle *, int) { return FindDepotData(); }
FindDepotData NPFRoadVehicleFindNearestDepot(const RoadVehicle *, int) { return FindDepotData(); }

/* ---------------------------------------------------------------------------
 * Engine / property lookups. No NewGRF is loaded beyond the base graphics, so
 * every property resolves to the original value the caller already computed.
 * ------------------------------------------------------------------------- */
int GetVehicleProperty(const Vehicle *, PropertyID, int orig_value, bool) { return orig_value; }
int GetEngineProperty(EngineID, PropertyID, int orig_value, const Vehicle *, bool) { return orig_value; }
bool UsesWagonOverride(const Vehicle *) { return false; }
void GetCustomEngineSprite(EngineID, const Vehicle *, Direction, EngineImageType, VehicleSpriteSeq *) {}
uint GetGrfSpecFeature(VehicleType) { return 0; }
void TriggerVehicle(Vehicle *, VehicleTrigger) {}

/* ---------------------------------------------------------------------------
 * Articulated vehicles: our buses are single units.
 * ------------------------------------------------------------------------- */
void AddArticulatedParts(Vehicle *) {}
void CheckConsistencyOfArticulatedVehicle(const Vehicle *) {}
bool IsArticulatedVehicleCarryingDifferentCargoes(const Vehicle *, CargoID *) { return false; }
void GetArticulatedRefitMasks(EngineID, bool, CargoTypes *union_mask, CargoTypes *intersection_mask)
{
	if (union_mask != nullptr) *union_mask = 0;
	if (intersection_mask != nullptr) *intersection_mask = 0;
}
CargoTypes GetUnionOfArticulatedRefitMasks(EngineID, bool) { return 0; }

/* ---------------------------------------------------------------------------
 * Sound, news, animation, disasters — all absent on this port.
 * ------------------------------------------------------------------------- */
bool PlayVehicleSound(const Vehicle *, VehicleSoundEvent, bool) { return false; }
void SndPlayVehicleFx(SoundID, const Vehicle *) {}
void DeleteVehicleNews(VehicleID, StringID) {}
void ReleaseDisastersTargetingVehicle(VehicleID) {}
EffectVehicle *CreateEffectVehicleRel(const Vehicle *, int, int, int, EffectVehicleType) { return nullptr; }
void StopGlobalFollowVehicle(const Vehicle *) {}
void HideFillingPercent(TextEffectID *) {}
