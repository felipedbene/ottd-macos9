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
 * m1_vehicle.cpp — a REAL RoadVehicle in the REAL shared _vehicle_pool, WITHOUT compiling
 * vehicle.cpp / roadveh_cmd.cpp (which drag the network/pathfinder/order/station cascade).
 * Same trick as m1_company.cpp: own the pool + the object's ctor/vtable here; the heavy
 * command TUs stay out. The Vehicle teardown surface (~Vehicle, Crash, Order::~Order,
 * the CargoList dtor, GroundVehicle::CargoChanged) is ALREADY provided by m1_road_stubs.cpp
 * (because road_cmd.cpp is compiled) — so this is a small delta. The bus is driven/rendered
 * by ottd-r1/r1_scene.cpp (its ViewportAddVehicles iterates this pool). See [[r1-render-smooth]].
 */
#include "stdafx.h"
#include "vehicle_base.h"
#include "roadveh.h"
#include "ground_vehicle.hpp"
#include "track_func.h"        /* Trackdir, INVALID_TRACKDIR */
#include "company_func.h"      /* _local_company */
#include "core/pool_func.hpp"

#include "safeguards.h"

/* The REAL Vehicle pool — replaces the fake `char _vehicle_pool[4096]` deadpool (guarded
 * out of m1_deadpools.c under R1_MERGE). PoolBase (compiled in core/pool_func.cpp) registers
 * it. FreeItem was removed from m1_road_stubs.cpp so this full macro doesn't duplicate it. */
VehiclePool _vehicle_pool("Vehicle");
INSTANTIATE_POOL_METHODS(Vehicle)

/* Minimal Vehicle base ctor — verbatim from vehicle.cpp:345-357 (self-contained: only
 * touches members + constants, no allocation, no subsystem calls). */
Vehicle::Vehicle(VehicleType type)
{
	this->type                 = type;
	this->coord.left           = INVALID_COORD;
	this->sprite_cache.old_coord.left = INVALID_COORD;
	this->group_id             = DEFAULT_GROUP;
	this->fill_percent_te_id   = INVALID_TE_ID;
	this->first                = this;
	this->colourmap            = PAL_NONE;
	this->cargo_age_counter    = 1;
	this->last_station_visited  = INVALID_STATION;
	this->last_loading_station  = INVALID_STATION;
}

/* Called by the inline ~RoadVehicle (roadveh.h). The real one (vehicle.cpp) drags
 * stations/groups/orders/news — none apply to our single cosmetic bus. */
void Vehicle::PreDestructor() {}

/* RoadVehicle vtable: MarkDirty (first out-of-line virtual) is the key function, so
 * defining these here emits the RoadVehicle vtable + typeinfo. All 13 out-of-line
 * overrides get minimal bodies; roadveh_cmd.cpp (their real home) is NOT compiled, so
 * no ODR clash. Movement/rendering are hand-driven in r1_scene.cpp, so none of these
 * run on the hot path. */
void      RoadVehicle::MarkDirty() {}
void      RoadVehicle::UpdateDeltaXY() {}
void      RoadVehicle::GetImage(Direction direction, EngineImageType, VehicleSpriteSeq *result) const { result->Set((SpriteID)(0xCD4 + direction)); }
Money     RoadVehicle::GetRunningCost() const { return 0; }
uint16    RoadVehicle::GetMaxWeight() const { return 0; }
bool      RoadVehicle::Tick() { return true; }
void      RoadVehicle::OnNewDay() {}
uint      RoadVehicle::Crash(bool) { return 0; }
Trackdir  RoadVehicle::GetVehicleTrackdir() const { return INVALID_TRACKDIR; }
TileIndex RoadVehicle::GetOrderStationLocation(StationID) { return INVALID_TILE; }
bool      RoadVehicle::FindClosestDepot(TileIndex *, DestinationID *, bool *) { return false; }
int       RoadVehicle::GetCurrentMaxSpeed() const { return 0; }
void      RoadVehicle::SetDestTile(TileIndex tile) { this->dest_tile = tile; }

/* GroundVehicle virtual override present in the RoadVehicle vtable (out-of-line; real one
 * at ground_vehicle.cpp, explicit-instantiated there — that TU is not compiled). */
template <> bool GroundVehicle<RoadVehicle, VEH_ROAD>::IsChainInDepot() const { return false; }

/* Stand up ONE real road vehicle in the pool. Fields set for a valid, VISIBLE, renderable
 * vehicle; the pool is Tzero, so everything else (hashes, orders, caches) stays zeroed and
 * unused. Returns the new Vehicle* (as void* to keep C linkage) so a FLEET caller can give
 * each bus its OWN pooled puppet (R1-78) instead of sharing "the first" pool element; returns
 * nullptr if the pool is full. */
extern "C" void *r1_make_roadvehicle(uint tile, int x, int y, int z, int dir)
{
	if (!RoadVehicle::CanAllocateItem()) return nullptr;
	RoadVehicle *v = new RoadVehicle();
	v->owner     = _local_company;
	v->tile      = (TileIndex)tile;
	v->x_pos     = x; v->y_pos = y; v->z_pos = z;
	v->direction = (Direction)dir;
	v->spritenum = 0;                 /* _roadveh_images[0] == 0xCD4 */
	v->vehstatus = 0;                 /* visible: no VS_HIDDEN */
	v->roadtype  = ROADTYPE_ROAD;
	v->SetFrontEngine();              /* subtype |= GVSF_FRONT -> IsPrimaryVehicle() */
	v->sprite_cache.sprite_seq.Set((SpriteID)(0xCD4 + dir));
	return v;
}
