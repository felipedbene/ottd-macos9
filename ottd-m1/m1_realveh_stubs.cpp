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

extern "C" void ottd_log(const char *fmt, ...);   /* netlog tee (sink + file) */

/* ---------------------------------------------------------------------------
 * Pathfinder: the real reason this file exists.
 *
 * m1_pathfind.cpp's r1_road_path is a BFS over MP_ROAD tiles that already drives
 * every bus in the game. Here it answers the one question roadveh_cmd asks: from
 * `tile`, having entered from `enterdir`, which trackdir heads toward the
 * vehicle's destination? We path from `tile` to v->dest_tile, take the first hop,
 * and convert that step into a trackdir.
 * ------------------------------------------------------------------------- */
/* R1-166: the out array is uint32 (m1_pathfind writes 4-byte tile ids). This TU
 * declared it `unsigned short *` since R1-140 — on big-endian PPC route[1] then
 * read the LOW HALF of route[0] (the start tile itself), R1TrackdirTowards never
 * matched, and EVERY junction choice silently fell back to an arbitrary first
 * trackdir. The real-controller bus wandered instead of pathing (R1-165 trace:
 * hop==tile, td=255 at every single choice). C has no cross-TU signature check
 * for extern "C" — this cost three debugging builds. */
extern "C" int r1_road_path(uint from, uint to, unsigned *out, int max_len);

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
	unsigned route[96];
	path_found = false;
	if (v != nullptr && v->dest_tile != INVALID_TILE) {
		/* R1-150: the order machinery aims road vehicles at the station SIGN
		 * (GetOrderStationLocation returns st->xy — usually not even a road tile)
		 * and leaves "any road stop of that station" to the pathfinder's goal set.
		 * Real YAPF knows that; a BFS needs ONE concrete road tile. Resolve the
		 * sign to the station's nearest bus stop or the bus orbits the sign
		 * forever (R1-149 sink evidence: drove past dest, circling at spd 60). */
		TileIndex dest = v->dest_tile;
		if (v->current_order.IsType(OT_GOTO_STATION)) {
			const Station *st = Station::GetIfValid(v->current_order.GetDestination());
			if (st != nullptr) {
				uint best = UINT_MAX;
				for (const RoadStop *rs = st->bus_stops; rs != nullptr; rs = rs->next) {
					/* Skip the Rung-1 sign-tile placeholder (not a real stop tile). */
					if (!IsTileType(rs->xy, MP_STATION)) continue;
					uint d = DistanceManhattan(tile, rs->xy);
					if (d < best) { best = d; dest = rs->xy; }
				}
			}
		}
		int len = r1_road_path((uint)tile, (uint)dest, route, 96);
		/* Diagnostic (near-destination only): the R1-164 trace showed the bus
		 * turning WEST at the junction one tile from the resolved stop — either
		 * the offered trackdirs mask lacks the east exit or the BFS hop is not
		 * what we think. Log the full decision inputs to settle it. */
		{
			static int s_pf_logs = 0;
			if (s_pf_logs < 12 && DistanceManhattan(tile, dest) <= 4) {
				s_pf_logs++;
				Trackdir dbg_td = (len >= 2)
					? R1TrackdirTowards(tile, (TileIndex)route[1], trackdirs) : INVALID_TRACKDIR;
				ottd_log("R1 PF@: tile=%u dest=%u len=%d hop=%u mask=0x%x td=%d",
				         (uint)tile, (uint)dest, len,
				         (len >= 2) ? (uint)route[1] : 0u,
				         (uint)trackdirs, (int)dbg_td);
			}
		}
		{
			static int s_nopath_logs = 0;
			if (len < 2 && s_nopath_logs < 10) {
				s_nopath_logs++;
				ottd_log("R1 PF: NO PATH %u -> %u (len=%d, fallback trackdir)",
				         (uint)tile, (uint)dest, len);
			}
		}
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

/* Depot rung: m1_depot.cpp owns the pool + r1_find_nearest_road_depot (Manhattan
 * over Depot::Iterate). Real YAPF/NPF are not linked; FindClosestDepot in
 * roadveh_cmd.cpp reaches us through these two names. */
FindDepotData r1_find_nearest_road_depot(const Vehicle *v, int max_distance);
FindDepotData YapfRoadVehicleFindNearestDepot(const RoadVehicle *v, int max_distance)
{
	return r1_find_nearest_road_depot(v, max_distance);
}
FindDepotData NPFRoadVehicleFindNearestDepot(const RoadVehicle *v, int max_distance)
{
	return r1_find_nearest_road_depot(v, max_distance);
}

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
