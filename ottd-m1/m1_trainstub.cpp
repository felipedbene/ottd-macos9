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
 * m1_trainstub.cpp — first honest link surface for the REAL train_cmd.cpp.
 *
 * train_cmd.cpp compiles clean under the R1 flags. Measured 2026-08-02: after
 * subtracting C++/libc/libgcc runtime AND symbols already provided as weak
 * definitions by vehicle.o / ground_vehicle.o (Pool<Vehicle>::GetNew/FreeItem,
 * GroundVehicle<Train>::GetAcceleration), it needs exactly 27 game symbols.
 *
 * This file defines those 27 and nothing else. It is NOT wired into build.sh
 * yet: linking train_cmd.cpp also collides with the cosmetic Train vtable in
 * m1_train.cpp, ConsistChanged/CmdReverseTrainDirection in m1_rvstub_airsea.cpp,
 * and four level-crossing stubs in m1_road_stubs.cpp. Those need
 * #ifndef R1_REAL_TRAIN_STACK handover first — see harness/handoff-train-stack.md.
 *
 * Pattern mirror: m1_realveh_stubs.cpp for the road vehicle stack. YAPF/NPF train
 * entry points are honest "no pathfinder" stubs (R1's cosmetic train is driven
 * by r1_scene.cpp, not by train_cmd::Tick). PBS reservation, NewGRF wagon
 * overrides, groups and order-backup are likewise absent subsystems.
 */
#include "stdafx.h"

#include "train.h"
#include "vehicle_func.h"
#include "rail.h"
#include "pbs.h"
#include "signal_func.h"
#include "station_func.h"
#include "group.h"
#include "order_backup.h"
#include "base_consist.h"
#include "newgrf_engine.h"
#include "newgrf_debug.h"
#include "newgrf_callbacks.h"
#include "newgrf_spritegroup.h"
#include "pathfinder/yapf/yapf.h"
#include "pathfinder/npf/npf_func.h"
#include "pathfinder/pathfinder_type.h"
#include "track_func.h"

#include "safeguards.h"

/* ---------------------------------------------------------------------------
 * Pathfinder (YAPF entry points). RUNG 7: no longer "no path" stubs — they
 * are shims onto the trackdir-aware BFS in m1_pathfind.cpp, the exact posture
 * road took with r1_road_path. NPF stays inert (VPF_YAPF is the configured
 * pathfinder).
 * ------------------------------------------------------------------------- */
extern "C" int r1_rail_dist(unsigned tile, int td, unsigned dest_tile);
extern "C" int r1_rail_choose_trackdir(unsigned tile, unsigned candidates, unsigned dest_tile, int *best_td);

Track YapfTrainChooseTrack(const Train *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks,
		bool &path_found, bool, PBSTileInfo *, TileIndex *dest)
{
	path_found = false;
	if (dest != nullptr) *dest = INVALID_TILE;
	if (tracks == TRACK_BIT_NONE) return INVALID_TRACK;

	TrackdirBits candidates = TrackBitsToTrackdirBits(tracks) & DiagdirReachesTrackdirs(enterdir);
	if (candidates == TRACKDIR_BIT_NONE) return FindFirstTrack(tracks);

	TileIndex target = v->dest_tile;
	if (target.value == 0 || target.value >= MapSize()) return FindFirstTrack(tracks);

	int best_td = 0;
	if (r1_rail_choose_trackdir(tile.value, (unsigned)candidates, target.value, &best_td)) {
		path_found = true;
		if (dest != nullptr) *dest = target;
	}
	return TrackdirToTrack((Trackdir)best_td);
}

FindDepotData YapfTrainFindNearestDepot(const Train *, int) { return FindDepotData(); }

/* Reverse iff driving on is strictly worse than turning around — this is what
 * retires the dead-end runaround: after loading mid-line the train now flips
 * in place when the next platform lies behind it. */
bool YapfTrainCheckReverse(const Train *v)
{
	TileIndex target = v->dest_tile;
	if (target.value == 0 || target.value >= MapSize()) return false;
	Trackdir td = v->GetVehicleTrackdir();
	if (td == INVALID_TRACKDIR) return false;
	int fwd = r1_rail_dist(v->tile.value, (int)td, target.value);
	int rev = r1_rail_dist(v->tile.value, (int)ReverseTrackdir(td), target.value);
	if (rev < 0) return false;
	return fwd < 0 || rev < fwd;
}
bool YapfTrainFindNearestSafeTile(const Train *, TileIndex, Trackdir, bool) { return false; }

Track NPFTrainChooseTrack(const Train *, bool &path_found, bool, PBSTileInfo *)
{
	path_found = false;
	return INVALID_TRACK;
}
FindDepotData NPFTrainFindNearestDepot(const Train *, int) { return FindDepotData(); }
bool NPFTrainCheckReverse(const Train *) { return false; }
bool NPFTrainFindNearestSafeTile(const Train *, TileIndex, Trackdir, bool) { return false; }

/* ---------------------------------------------------------------------------
 * PBS / reservation. No path-based signalling: nothing is reserved, nothing
 * blocks, TryReserve always "succeeds" without storing state (GetReserved*
 * stays empty). Matches "PBS subsystem absent".
 * ------------------------------------------------------------------------- */

TrackBits GetReservedTrackbits(TileIndex) { return TRACK_BIT_NONE; }
void SetRailStationPlatformReservation(TileIndex, DiagDirection, bool) {}
bool TryReserveRailTrack(TileIndex, Track, bool) { return true; }
void UnreserveRailTrack(TileIndex, Track) {}

PBSTileInfo FollowTrainReservation(const Train *, Vehicle **train_on_res)
{
	if (train_on_res != nullptr) *train_on_res = nullptr;
	return PBSTileInfo();
}
bool IsSafeWaitingPosition(const Train *, TileIndex, Trackdir, bool, bool) { return true; }
bool IsWaitingPositionFree(const Train *, TileIndex, Trackdir, bool) { return true; }

/* ---------------------------------------------------------------------------
 * Signals / station / depot timing — rail_cmd.cpp and signal.cpp not linked.
 * ------------------------------------------------------------------------- */

void SetSignalsOnBothDir(TileIndex, Track, Owner) {}
bool IsStationTileBlocked(TileIndex) { return false; }
/* TicksToLeaveDepot moved to m1_rail_draw.cpp as the REAL rail_cmd body
 * (rung 6 wagons: the INT_MAX stub stranded followers in the shed forever —
 * the leave-depot pull uses it to pace each wagon out). */

/* ---------------------------------------------------------------------------
 * Groups. m1_rvstub_group.cpp owns GroupStatistics::* and autoreplace; these
 * three Train-specific hooks live in group_cmd.cpp and are train_cmd-only.
 * ------------------------------------------------------------------------- */

void SetTrainGroupID(Train *, GroupID) {}
void UpdateTrainGroupID(Train *) {}
void RemoveVehicleFromGroup(const Vehicle *) {}

/* ---------------------------------------------------------------------------
 * NewGRF / autoreplace helpers. No wagon overrides, no inspect window, no
 * cargo-capacity recount after a consist shuffle R1 never performs.
 * ------------------------------------------------------------------------- */

const SpriteGroup *GetWagonOverrideSpriteSet(EngineID, CargoID, EngineID) { return nullptr; }
uint16 GetVehicleCallbackParent(CallbackID, uint32, uint32, EngineID, const Vehicle *, const Vehicle *)
{
	return CALLBACK_FAILED;
}
void InvalidateNewGRFInspectWindow(GrfSpecFeature, uint) {}
void CheckCargoCapacity(Vehicle *) {}

/* ---------------------------------------------------------------------------
 * Consist / order-backup. base_consist.cpp and order_backup.cpp are not
 * linked; Copy is a no-op (R1 trains have no name/timetable to preserve) and
 * Backup drops the request (no depot rebuild restore path).
 * ------------------------------------------------------------------------- */

void BaseConsist::CopyConsistPropertiesFrom(const BaseConsist *) {}
void OrderBackup::Backup(const Vehicle *, uint32) {}
