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

// R1 minimal station draw (companion to m1_station.cpp's INVISIBLE Station pool):
// a custom _tile_type_station_procs so the real viewport can DRAW an MP_STATION
// bus-stop tile WITHOUT pulling station_cmd.cpp's NewGRF/catchment/link-graph
// machinery. Our world is flat at height 0 and every MP_STATION tile we write is a
// standard (non-drive-through) bus stop, so the draw is just the classic base-set
// bus-stop ground sprite (2692..2695) + the 3 building sprites (2696..2707) for the
// tile's DiagDirection — ONLY classic ogfx1_base.grf sprites, never SPR_OPENTTD_BASE.
// The zeroed common `_tile_type_station_procs` in m1_deadpools.c merges into this
// strong definition, exactly like m1_water_draw.cpp. Also exposes r1_place_bus_stop()
// which writes the real MP_STATION road-stop tile (via MakeRoadStop). (R1 render-merge.)
#include "stdafx.h"
#include "landscape.h"
#include "command_func.h"
#include "viewport_func.h"
#include "slope_func.h"
#include "tile_map.h"
#include "station_map.h"       /* MakeRoadStop, GetStationGfx, MP_STATION accessors */
#include "company_func.h"      /* _local_company */

#include "table/strings.h"
#include "table/sprites.h"

#include "safeguards.h"

/* One building line of a bus-stop layout (subset of DrawTileSeqStruct: the fields we use).
 * Values transcribed from table/station_land.h _station_display_datas_71..74 (bus stop,
 * DiagDirections NE/SE/SW/NW). PAL_NONE — the classic sprites render fine without recolour. */
struct R1BusSeq { int8 dx, dy, dz; uint8 sx, sy, sz; SpriteID img; };

static const R1BusSeq _r1_bus_stop_seq[DIAGDIR_END][3] = {
	/* NE (_station_display_datas_71) */ {
		{  2,  0, 0, 11,  1, 10, SPR_BUS_STOP_NE_BUILD_A },
		{ 13,  0, 0,  3, 16, 10, SPR_BUS_STOP_NE_BUILD_B },
		{  0, 13, 0, 13,  3, 10, SPR_BUS_STOP_NE_BUILD_C },
	},
	/* SE (_station_display_datas_72) */ {
		{  0,  3, 0,  1, 11, 10, SPR_BUS_STOP_SE_BUILD_A },
		{  0,  0, 0, 16,  3, 10, SPR_BUS_STOP_SE_BUILD_B },
		{ 13,  3, 0,  3, 13, 10, SPR_BUS_STOP_SE_BUILD_C },
	},
	/* SW (_station_display_datas_73) */ {
		{  3, 15, 0, 11,  1, 10, SPR_BUS_STOP_SW_BUILD_A },
		{  0,  0, 0,  3, 16, 10, SPR_BUS_STOP_SW_BUILD_B },
		{  3,  0, 0, 13,  3, 10, SPR_BUS_STOP_SW_BUILD_C },
	},
	/* NW (_station_display_datas_74) */ {
		{ 15,  2, 0,  1, 11, 10, SPR_BUS_STOP_NW_BUILD_A },
		{  0, 13, 0, 16,  3, 10, SPR_BUS_STOP_NW_BUILD_B },
		{  0,  0, 0,  3, 13, 10, SPR_BUS_STOP_NW_BUILD_C },
	},
};

static void DrawTile_Station(TileInfo *ti)
{
	/* Standard bus stop: StationGfx (m5) is the DiagDirection 0..3 (see MakeRoadStop/
	 * GetRoadStopDir). Draw the paved ground sprite, then the 3 building sprites. */
	DiagDirection dir = (DiagDirection)(GetStationGfx(ti->tile) & 3);

	DrawGroundSprite(SPR_BUS_STOP_NE_GROUND + dir, PAL_NONE);

	const R1BusSeq *seq = _r1_bus_stop_seq[dir];
	for (int i = 0; i < 3; i++) {
		AddSortableSpriteToDraw(seq[i].img, PAL_NONE,
			ti->x + seq[i].dx, ti->y + seq[i].dy,
			seq[i].sx, seq[i].sy, seq[i].sz, ti->z + seq[i].dz);
	}
}

static int GetSlopePixelZ_Station(TileIndex tile, uint x, uint y)
{
	return 0;   // our world is flat at height 0
}

static Foundation GetFoundation_Station(TileIndex tile, Slope tileh)
{
	return FOUNDATION_NONE;
}

static CommandCost ClearTile_Station(TileIndex tile, DoCommandFlag flags)
{
	return_cmd_error(STR_EMPTY);   // never on the live path (we never clear stations)
}

static void GetTileDesc_Station(TileIndex tile, TileDesc *td)
{
	td->str = STR_EMPTY;
	td->owner[0] = GetTileOwner(tile);
}

static void TileLoop_Station(TileIndex tile) {}
static void ChangeTileOwner_Station(TileIndex tile, Owner old_owner, Owner new_owner) {}

static TrackStatus GetTileTrackStatus_Station(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	return 0;
}

static CommandCost TerraformTile_Station(TileIndex tile, DoCommandFlag flags, int z_new, Slope tileh_new)
{
	return_cmd_error(STR_EMPTY);
}

extern const TileTypeProcs _tile_type_station_procs = {
	DrawTile_Station,           // draw_tile_proc
	GetSlopePixelZ_Station,     // get_slope_z_proc
	ClearTile_Station,          // clear_tile_proc
	nullptr,                    // add_accepted_cargo_proc
	GetTileDesc_Station,        // get_tile_desc_proc
	GetTileTrackStatus_Station, // get_tile_track_status_proc
	nullptr,                    // click_tile_proc
	nullptr,                    // animate_tile_proc
	TileLoop_Station,           // tile_loop_proc
	ChangeTileOwner_Station,    // change_tile_owner_proc
	nullptr,                    // add_produced_cargo_proc
	nullptr,                    // vehicle_enter_tile_proc
	GetFoundation_Station,      // get_foundation_proc
	TerraformTile_Station,      // terraform_tile_proc
};

/* ============================================================================
 * R1 driver glue: write a REAL MP_STATION road-stop (bus) tile so DrawTile_Station
 * above actually fires in the viewport. Call at the end of a bus route.
 *   tile          — MP_ROAD (or clear) tile to convert; typically the route's last tile.
 *   station_index — the owning Station::index (StationID). See r1_town_station_index.
 *   axis          — orientation: taken as the bus-stop DiagDirection (0=NE,1=SE,2=SW,3=NW).
 * Owner is _local_company to match the Station object (so the viewport uses the company
 * palette). tram roadtype is INVALID_ROADTYPE (no tram). Defensive: bounds-checks the tile.
 * ============================================================================ */
extern "C" void r1_place_bus_stop(unsigned tile, unsigned station_index, int axis)
{
	TileIndex t = (TileIndex)tile;
	if (t >= MapSize()) return;              // out of range
	if (IsTileType(t, MP_VOID)) return;      // never write over the map border

	DiagDirection d = (DiagDirection)(((unsigned)axis) & 3);
	MakeRoadStop(t, _local_company, (StationID)station_index,
	             ROADSTOP_BUS, ROADTYPE_ROAD, INVALID_ROADTYPE, d);
}
