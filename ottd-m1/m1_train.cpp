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
 * m1_train.cpp — a REAL Train in the REAL shared _vehicle_pool, WITHOUT compiling
 * train_cmd.cpp (which drags the pathfinder/order/signal/station cascade). Same trick
 * as m1_vehicle.cpp (its RoadVehicle sibling): own the Train ctor/vtable here; the heavy
 * command TUs stay out. The Vehicle base surface (_vehicle_pool, INSTANTIATE_POOL_METHODS,
 * Vehicle::Vehicle(type), Vehicle::PreDestructor, ~Vehicle, Vehicle::Crash, the CargoList
 * dtor) is ALREADY provided by m1_vehicle.cpp + m1_road_stubs.cpp — so this is a small
 * delta: just the Train vtable/typeinfo + the Train instantiation of GroundVehicle<> +
 * the factory. The train is driven/rendered by ottd-r1/r1_scene.cpp the same way the R1
 * bus is. See [[r1-render-smooth]].
 */
#include "stdafx.h"
#include "vehicle_base.h"
#include "train.h"
#include "ground_vehicle.hpp"
#include "track_func.h"        /* Trackdir, INVALID_TRACKDIR, TrackBits, TRACK_BIT_X */
#include "company_func.h"      /* _local_company */
#include "core/pool_func.hpp"

#include "safeguards.h"

/* Classic base-set steam engine (Kirby Paul Tank) sprite base, from
 * table/train_sprites.h _engine_sprite_base[0] == 0x0B59 (< 4896, so it lives in the
 * ogfx1_base.grf classic range). Mirror the road vehicle's "base + direction" style. */
static const SpriteID R1_TRAIN_SPRITE_BASE = 0x0B59;

/* DO NOT define _vehicle_pool / INSTANTIATE_POOL_METHODS(Vehicle) — m1_vehicle.cpp owns
 * the real pool. DO NOT define Vehicle::Vehicle(type) / Vehicle::PreDestructor /
 * ~Vehicle / Vehicle::Crash — those are shared and already provided. Duplicates make the
 * XCOFF ld SEGFAULT. */

/* Train vtable: MarkDirty (first out-of-line virtual) is the key function, so defining
 * these here emits the Train vtable + typeinfo. Every out-of-line override in train.h gets
 * a minimal body; train_cmd.cpp (their real home) is NOT compiled, so no ODR clash.
 * Movement/rendering are hand-driven in r1_scene.cpp, so none of these run on the hot path. */
void      Train::MarkDirty() {}
void      Train::UpdateDeltaXY() {}
void      Train::PlayLeaveStationSound(bool) const {}
void      Train::GetImage(Direction direction, EngineImageType, VehicleSpriteSeq *result) const { result->Set((SpriteID)(R1_TRAIN_SPRITE_BASE + direction)); }
Money     Train::GetRunningCost() const { return 0; }
uint16    Train::GetMaxWeight() const { return 0; }
bool      Train::Tick() { return true; }
void      Train::OnNewDay() {}
uint      Train::Crash(bool) { return 0; }
Trackdir  Train::GetVehicleTrackdir() const { return INVALID_TRACKDIR; }
TileIndex Train::GetOrderStationLocation(StationID) { return INVALID_TILE; }
bool      Train::FindClosestDepot(TileIndex *, DestinationID *, bool *) { return false; }
int       Train::GetCurrentMaxSpeed() const { return 0; }

/* GroundVehicle virtual override present in the Train vtable (out-of-line; real one at
 * ground_vehicle.cpp, explicit-instantiated there — that TU is not compiled). This is the
 * Train instantiation of the template, distinct from m1_road_stubs.cpp's RoadVehicle one,
 * so no duplicate symbol. */
template <> bool GroundVehicle<Train, VEH_TRAIN>::IsChainInDepot() const { return false; }

/* Ground-vehicle cache recompute for the Train instantiation (mirrors the RoadVehicle
 * one in m1_road_stubs.cpp; again a distinct template instantiation). */
template <> void GroundVehicle<Train, VEH_TRAIN>::CargoChanged() {}

/* Stand up ONE real train in the pool. Fields set for a valid, VISIBLE, renderable
 * vehicle; the pool is zeroed, so everything else (hashes, orders, caches) stays zeroed
 * and unused. `track` gets a straight bit (NOT TRACK_BIT_DEPOT) so IsInDepot() is false
 * and the train renders on the map; railtype 0 == RAILTYPE_RAIL. Returns the new Train*
 * (as void* to keep C linkage), or nullptr if the pool is full — mirrors r1_make_roadvehicle. */
extern "C" void *r1_make_train(uint tile, int x, int y, int z, int dir)
{
	if (!Train::CanAllocateItem()) return nullptr;
	Train *v = new Train();
	v->owner     = _local_company;
	v->tile      = (TileIndex)tile;
	v->x_pos     = x; v->y_pos = y; v->z_pos = z;
	v->direction = (Direction)dir;
	v->spritenum = 0;                 /* _engine_sprite_base[0] == 0x0B59 */
	v->vehstatus = 0;                 /* visible: no VS_HIDDEN */
	v->track     = TRACK_BIT_X;       /* on straight track, NOT TRACK_BIT_DEPOT */
	v->railtype  = (RailType)0;       /* RAILTYPE_RAIL */
	v->SetFrontEngine();              /* subtype |= GVSF_FRONT -> IsPrimaryVehicle() */
	v->sprite_cache.sprite_seq.Set((SpriteID)(R1_TRAIN_SPRITE_BASE + dir));
	static UnitID s_next_unit = 1;    /* distinct 1..N so vehicle-list rows aren't all "0" */
	v->unitnumber = s_next_unit++;
	return v;
}
