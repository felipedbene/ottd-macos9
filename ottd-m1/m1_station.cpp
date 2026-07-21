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
 * m1_station.cpp — REAL pooled Station + RoadStop (R1-83, "Rung 1", INVISIBLE), the m1_vehicle
 * way: own the real StationPool + Station + RoadStopPool + RoadStop WITHOUT compiling
 * station_cmd.cpp (the DrawTile_Station / catchment / newgrf / link-graph / command cascade —
 * "the station seam"). A real Station now participates in the economy: the bus records its
 * passenger pickup on the destination town's real Station goods[CT_PASSENGERS] instead of only
 * reading Town::supplied.
 *
 * Key moves (all mirror proven rungs):
 *  - own the two pools + INSTANTIATE_POOL_METHODS (like m1_vehicle.cpp:36-37).
 *  - define Station's out-of-line virtuals with stub bodies → emits the Station vtable+typeinfo
 *    (their real homes station.cpp / station_cmd.cpp / newgrf_station.cpp are NOT compiled).
 *  - the LOAD-BEARING stub: template<> CargoList<StationCargoList,StationCargoPacketMap>::
 *    ~CargoList(){} — an empty ~Station still destroys goods[64], each a StationCargoList whose
 *    base CargoList dtor is explicit-instantiated in the uncompiled cargopacket.cpp. This one
 *    line closes the whole cargo cascade (exact analog of m1_road_stubs.cpp's VehicleCargoList).
 *  - keep UpdateVirtCoord empty → no sign/kdtree entry, so nothing is drawn and no MP_STATION
 *    tile exists (a visible station tile would null-call _tile_type_station_procs — deferred).
 */
#include "stdafx.h"
#include "cargopacket.h"      /* CargoList template + StationCargoList/StationCargoPacketMap */
/* The load-bearing cargo cascade closer. StationCargoList's base CargoList dtor is
 * explicit-instantiated in the uncompiled cargopacket.cpp; Station's goods[64] members
 * implicitly instantiate it. DECLARE the specialization here — after cargopacket.h defines the
 * template, but BEFORE station_base.h's inline uses trigger implicit instantiation — so the
 * compiler defers to our definition (below) instead of generating its own (which would dup at
 * the XCOFF link). Same intent as m1_road_stubs.cpp:60 for VehicleCargoList. */
template <> CargoList<StationCargoList, StationCargoPacketMap>::~CargoList();

/* R1 rung-b: r1_station_add_cargo (below) makes the StationCargoPacketMap NON-empty. The new
 * StationCargoList::Append odr-uses these two base-class members. DECLARE the explicit
 * specializations HERE — before station_base.h triggers implicit instantiation of the
 * CargoList<StationCargoList,...> members — so the compiler defers to OUR definitions instead of
 * emitting its own from the (uncompiled) cargopacket.cpp (which would dup at the XCOFF link).
 * Same deferral trick as the ~CargoList declaration above. RemoveFromCache is intentionally NOT
 * specialized: nothing in this TU removes station cargo, so it is never instantiated. */
template <> void CargoList<StationCargoList, StationCargoPacketMap>::AddToCache(const CargoPacket *cp);
template <> bool CargoList<StationCargoList, StationCargoPacketMap>::TryMerge(CargoPacket *icp, CargoPacket *cp);

#include "station_base.h"
#include "roadstop_base.h"
#include "core/pool_func.hpp"
#include "town.h"
#include "company_func.h"     /* _local_company */
#include "industrytype.h"     /* IT_INVALID */

#include "safeguards.h"

/* Definition of the specialization declared above (empty: we never allocate CargoPackets, so the
 * MultiMap is always empty at destruction). */
template <>
CargoList<StationCargoList, StationCargoPacketMap>::~CargoList() {}

/* ------- R1 rung-b: minimal real cargo machinery (from cargopacket.cpp, NOT compiled) -------
 * Only the members that r1_station_add_cargo / StationCargoList::Append actually reach are given
 * local bodies (explicit specialization), mirroring the ~CargoList trick. */

/* Base-list cache update on Append (cargopacket.cpp:191-196). Uses the public inline accessors so
 * it needs no friendship. */
template <>
void CargoList<StationCargoList, StationCargoPacketMap>::AddToCache(const CargoPacket *cp)
{
	this->count                 += cp->Count();
	this->cargo_days_in_transit += (uint)cp->DaysInTransit() * cp->Count();
}

/* Base-list static merge helper (cargopacket.cpp:217-227). AreMergable is a public inline on
 * StationCargoList; Merge is public on CargoPacket (defined below). */
template <>
/* static */ bool CargoList<StationCargoList, StationCargoPacketMap>::TryMerge(CargoPacket *icp, CargoPacket *cp)
{
	if (StationCargoList::AreMergable(icp, cp) &&
			icp->Count() + cp->Count() <= CargoPacket::MAX_COUNT) {
		icp->Merge(cp);
		return true;
	}
	return false;
}

/* CargoPacket ctor used by r1_station_add_cargo (cargopacket.cpp:44-55, minus the count!=0 assert
 * which is compiled out under NDEBUG anyway). Init-list order matches the header's field order. */
CargoPacket::CargoPacket(StationID source, TileIndex source_xy, uint16 count, SourceType source_type, SourceID source_id) :
	feeder_share(0),
	count(count),
	days_in_transit(0),
	source_id(source_id),
	source(source),
	source_xy(source_xy),
	loaded_at_xy(0)
{
	this->source_type = source_type;
}

/* CargoPacket::Merge (cargopacket.cpp:104-109) — referenced by TryMerge above. `delete cp` returns
 * the packet to _cargopacket_pool (its ~CargoPacket is the inline empty one in the header). */
void CargoPacket::Merge(CargoPacket *cp)
{
	this->count += cp->count;
	this->feeder_share += cp->feeder_share;
	delete cp;
}

/* StationCargoList::Append (cargopacket.cpp:690-703) — the one path that grows the map. */
void StationCargoList::Append(CargoPacket *cp, StationID next)
{
	this->AddToCache(cp);

	StationCargoPacketMap::List &list = this->packets[next];
	for (StationCargoPacketMap::List::reverse_iterator it(list.rbegin());
			it != list.rend(); it++) {
		if (StationCargoList::TryMerge(*it, cp)) return;
	}
	list.push_back(cp);
}

/* Real pools — replace the fake `char _station_pool[8192]` deadpool (guarded out of
 * m1_deadpools.c under R1_MERGE). _roadstop_pool is brand-new (no compiled TU referenced it). */
StationPool _station_pool("Station");
INSTANTIATE_POOL_METHODS(Station)
RoadStopPool _roadstop_pool("RoadStop");
INSTANTIATE_POOL_METHODS(RoadStop)
/* R1 rung-b: real CargoPacket pool (brand-new symbol; cargopacket.cpp is not compiled, so no ODR
 * clash). ~CargoPacket is inline-empty in the header, so CleanPool is safe to instantiate. */
CargoPacketPool _cargopacket_pool("CargoPacket");
INSTANTIATE_POOL_METHODS(CargoPacket)

/* ------- Station ctor (verbatim from station.cpp:64-75, self-contained) ------- */
Station::Station(TileIndex tile) :
	SpecializedStation<Station, false>(tile),
	bus_station(INVALID_TILE, 0, 0),
	truck_station(INVALID_TILE, 0, 0),
	ship_station(INVALID_TILE, 0, 0),
	indtype(IT_INVALID),
	time_since_load(255),
	time_since_unload(255),
	last_vehicle_type(VEH_INVALID)
{
}

/* ------- Minimal dtors / PostDestructor / FillCachedName (real homes not compiled) -------
 * Empty ~Station is safe: we never add a sign/kdtree entry (UpdateVirtCoord is empty) and never
 * allocate real CargoPackets, so there is nothing to unwind — member dtors (goods[64] -> the
 * cargo-list stub below) are all this needs. */
/* Station has a `StationRect rect` member; its ctor (station.cpp:506) just MakeEmpty()s the rect
 * (zeroes the 4 Rect fields). We never add tiles, so the rect is never queried — zero is fine. */
StationRect::StationRect() { this->left = this->top = this->right = this->bottom = 0; }

Station::~Station() {}
BaseStation::~BaseStation() {}
/* static */ void BaseStation::PostDestructor(size_t) {}
void BaseStation::FillCachedName() const {}
RoadStop::~RoadStop() {}

/* ------- Station out-of-line virtuals -> emits the vtable + typeinfo -------
 * Stub bodies (never on the live path: no MP_STATION tile, no rail platform, no NewGRF). */
void Station::UpdateVirtCoord() {}
void Station::MoveSign(TileIndex) {}
void Station::GetTileArea(TileArea *ta, StationType) const { if (ta != nullptr) *ta = TileArea(this->xy, 1, 1); }
uint Station::GetPlatformLength(TileIndex) const { return 0; }
uint Station::GetPlatformLength(TileIndex, DiagDirection) const { return 0; }
uint32 Station::GetNewGRFVariable(const ResolverObject &, byte, byte, bool *available) const
{
	if (available != nullptr) *available = false;
	return 0;
}

/* ============================================================================
 * R1 driver glue: one real Station per town, keyed by TownID, so the bus can
 * record its passenger pickup on a REAL Station object.
 * ============================================================================ */
#define R1_MAX_TOWN_STATIONS 16
static Station *g_town_station[R1_MAX_TOWN_STATIONS];

/* Create a real Station (+ a RoadStop) for a town and remember it by TownID. INVISIBLE: no
 * MP_STATION tile is written, UpdateVirtCoord stays empty, so nothing draws and no viewport
 * draw-proc is hit. Sets FACIL_BUS_STOP so it's a genuine bus station facility. */
extern "C" void r1_make_town_station(unsigned townid, unsigned tile)
{
	if (townid >= R1_MAX_TOWN_STATIONS) return;
	if (!Station::CanAllocateItem()) return;
	Station *st = new Station((TileIndex)tile);
	st->owner      = _local_company;
	st->town       = Town::GetIfValid((TownID)townid);
	st->facilities = FACIL_BUS_STOP;
	st->build_date = 0;
	/* R1-86: give the station a real name so the station-list window's {STATION} renders the town
	 * name instead of "(undefined string)". STR_SV_STNAME = "{STRING1}" resolves via the handler's
	 * args {STR_TOWN_NAME, town->index, st->index} → the town's name (the {TOWN} shim is wired).
	 * No UpdateVirtCoord / sign / kdtree needed — the draw path reads string_id + town only. */
	st->string_id  = STR_SV_STNAME;
	if (RoadStop::CanAllocateItem()) {
		RoadStop *rs = new RoadStop((TileIndex)tile);
		st->bus_stops = rs;
	}
	g_town_station[townid] = st;
}

/* Record a passenger pickup on the town's real Station: accumulate the real max_waiting_cargo
 * counter and nudge the real rating up (service present). goods[CT_PASSENGERS] is a real field on
 * a real pooled object — the point of Rung 1 — without needing the CargoPacket subsystem. */
extern "C" void r1_station_pickup(unsigned townid, unsigned pax)
{
	if (townid >= R1_MAX_TOWN_STATIONS) return;
	Station *st = g_town_station[townid];
	if (st == nullptr) return;
	GoodsEntry &ge = st->goods[CT_PASSENGERS];
	ge.max_waiting_cargo += pax;
	unsigned r = (unsigned)ge.rating + 8;
	ge.rating = (byte)(r > 255 ? 255 : r);
}

/* R1 rung-b: the UPGRADED pickup — allocate a REAL CargoPacket of `pax` passengers and Append it to
 * the town Station's goods[CT_PASSENGERS].cargo (a StationCargoList). This makes
 * goods[CT_PASSENGERS].cargo.TotalCount() > 0, which is what lights up the station-list window's
 * cargo-rating bars. Also nudges the rating up like r1_station_pickup. Source is tagged ST_TOWN /
 * town index; the packet's source fields don't affect the fare (r1_deliver_cargo computes that from
 * an explicit dist), they just make the packet well-formed. */
extern "C" void r1_station_add_cargo(unsigned townid, unsigned pax)
{
	if (townid >= R1_MAX_TOWN_STATIONS || pax == 0) return;
	Station *st = g_town_station[townid];
	if (st == nullptr) return;
	if (!CargoPacket::CanAllocateItem()) return;

	if (pax > CargoPacket::MAX_COUNT) pax = CargoPacket::MAX_COUNT;
	GoodsEntry &ge = st->goods[CT_PASSENGERS];
	SourceID src = (st->town != nullptr) ? (SourceID)st->town->index : INVALID_SOURCE;
	CargoPacket *cp = new CargoPacket((StationID)st->index, st->xy, (uint16)pax, ST_TOWN, src);
	ge.cargo.Append(cp, INVALID_STATION);

	unsigned r = (unsigned)ge.rating + 8;
	ge.rating = (byte)(r > 255 ? 255 : r);
}

/* Number of real Station objects in the pool (for the HUD readout). */
extern "C" unsigned r1_station_count(void)
{
	return (unsigned)Station::GetNumItems();
}

/* R1-89: StationID for a town's real Station, so r1_scene can (a) write the town's MP_STATION
 * bus-stop tile (m1_station_draw.cpp) with the correct index and (b) point the bus's order chain
 * at real Stations. Returns 0xFFFF (INVALID_STATION) if the town has no station yet. */
extern "C" unsigned r1_town_station_index(unsigned townid)
{
	if (townid >= R1_MAX_TOWN_STATIONS || g_town_station[townid] == nullptr) return 0xFFFF;
	return (unsigned)g_town_station[townid]->index;
}
