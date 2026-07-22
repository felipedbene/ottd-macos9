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
// DRIVE-THROUGH bus stop: the road runs straight through the tile along its axis
// (so the scripted bus drives ONTO it — correct) and we set a small shelter beside
// the carriageway. The real drive-through sprites (SPR_BUS_STOP_DT_*) live in the
// OpenTTD-EXTRA GRF, which is NOT on this disk (would render "?"), so we FAKE the
// look with base ogfx1_base.grf sprites only: the classic straight-road ground
// sprite (SPR_ROAD_X 1333 / SPR_ROAD_Y 1332) plus ONE base bus-stop BUILD sprite
// (2696/2697) offset to the roadside as the shelter — every SpriteID < 4896.
// The zeroed common `_tile_type_station_procs` in m1_deadpools.c merges into this
// strong definition, exactly like m1_water_draw.cpp. Also exposes r1_place_bus_stop()
// which writes the real MP_STATION drive-through road-stop tile (via
// MakeDriveThroughRoadStop). (R1 render-merge.)
#include "stdafx.h"
#include "landscape.h"
#include "command_func.h"
#include "viewport_func.h"
#include "slope_func.h"
#include "tile_map.h"
#include "station_map.h"       /* MakeDriveThroughRoadStop, GetStationGfx, MP_STATION accessors */
#include "road_map.h"          /* GetRoadOwner, RTT_ROAD */
#include "company_func.h"      /* _local_company */

#include "sprite.h"            /* GENERAL_SPRITE_COLOUR (recolour palette for the building) */
#include "table/strings.h"
#include "table/sprites.h"

#include "safeguards.h"

/* One building line of the classic bus-stop shelter (subset of DrawTileSeqStruct: the fields we
 * use). We draw the FULL 3-part bay-stop BUILDING (A+B+C) over a through-road ground, so it reads
 * as a real, VISIBLE shelter (a single thin part was near-invisible). Offsets/dims transcribed
 * VERBATIM from table/station_land.h (_station_display_datas_71 = NE bay, _72 = SE bay). All sprites
 * 2696-2705 are classic ogfx1_base.grf (< 4896). They are PALETTE_MODIFIER_COLOUR recolour sprites,
 * so they MUST be drawn with a recolour palette (drawing them PAL_NONE renders index garbage — the
 * cause of the "invisible stop" + on-scroll smearing). Index by Axis: [0]=AXIS_X, [1]=AXIS_Y. */
struct R1SeqLine { int8 dx, dy, dz; uint8 sx, sy, sz; SpriteID img; };

static const R1SeqLine _r1_stop_build[2][3] = {
	{	/* AXIS_X (road runs SW<->NE, ground = SPR_ROAD_X): the NE bay building. */
		{  2,  0, 0, 11,  1, 10, SPR_BUS_STOP_NE_BUILD_A },
		{ 13,  0, 0,  3, 16, 10, SPR_BUS_STOP_NE_BUILD_B },
		{  0, 13, 0, 13,  3, 10, SPR_BUS_STOP_NE_BUILD_C },
	},
	{	/* AXIS_Y (road runs SE<->NW, ground = SPR_ROAD_Y): the SE bay building. */
		{  0,  3, 0,  1, 11, 10, SPR_BUS_STOP_SE_BUILD_A },
		{  0,  0, 0, 16,  3, 10, SPR_BUS_STOP_SE_BUILD_B },
		{ 13,  3, 0,  3, 13, 10, SPR_BUS_STOP_SE_BUILD_C },
	},
};

static void DrawTile_Station(TileInfo *ti)
{
	/* Drive-through bus stop: StationGfx (m5) is GFX_TRUCK_BUS_DRIVETHROUGH_OFFSET (4) + Axis,
	 * so bit 0 of the gfx is the road Axis (0 = AXIS_X, 1 = AXIS_Y). See MakeDriveThroughRoadStop. */
	Axis axis = (Axis)(GetStationGfx(ti->tile) & 1);

	/* 1) Straight-road GROUND so the road visibly runs THROUGH the tile (drive-through: the bus
	 *    drives ONTO it, not blocked). AXIS_X -> SPR_ROAD_X (1333), AXIS_Y -> SPR_ROAD_Y (1332). */
	DrawGroundSprite(axis == AXIS_X ? SPR_ROAD_X : SPR_ROAD_Y, PAL_NONE);

	/* 2) The full 3-part shelter building, RECOLOURED (orange = clearly visible on any terrain). */
	PaletteID pal = GENERAL_SPRITE_COLOUR(COLOUR_ORANGE);
	for (int i = 0; i < 3; i++) {
		const R1SeqLine &s = _r1_stop_build[axis][i];
		AddSortableSpriteToDraw(s.img, pal, ti->x + s.dx, ti->y + s.dy, s.sx, s.sy, s.sz, ti->z + s.dz);
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
 * R1 driver glue: write a REAL MP_STATION DRIVE-THROUGH road-stop (bus) tile so
 * DrawTile_Station above actually fires in the viewport. Call at the end of a bus route.
 *   tile          — MP_ROAD (or clear) tile to convert; typically the route's last tile.
 *   station_index — the owning Station::index (StationID). See r1_town_station_index.
 *   axis          — the ROAD AXIS the stop sits on: 0 = AXIS_X (road runs SW<->NE),
 *                   1 = AXIS_Y (road runs SE<->NW). A drive-through stop PRESERVES the
 *                   road along this axis in BOTH directions, so it does NOT block the
 *                   street — pass the same axis the underlying road runs.
 * Station owner is _local_company (so the viewport uses the company palette); the ROAD
 * keeps its existing owner (read BEFORE converting) so through-traffic ownership is intact,
 * falling back to OWNER_TOWN for our town-associated inter-town roads. tram roadtype is
 * INVALID_ROADTYPE (no tram). Defensive: bounds-checks the tile.
 * ============================================================================ */
extern "C" void r1_place_bus_stop(unsigned tile, unsigned station_index, int axis)
{
	TileIndex t = (TileIndex)tile;
	if (t >= MapSize()) return;              // out of range
	if (IsTileType(t, MP_VOID)) return;      // never write over the map border

	/* Preserve the road's existing owner if this is already a normal road tile; else the
	 * road is town-associated (our inter-town network) so OWNER_TOWN is the right owner. */
	Owner road_owner = IsTileType(t, MP_ROAD) ? GetRoadOwner(t, RTT_ROAD) : OWNER_TOWN;

	Axis a = (Axis)(((unsigned)axis) & 1);
	MakeDriveThroughRoadStop(t, _local_company /*station*/, road_owner /*road*/,
	                         INVALID_OWNER /*tram*/, (StationID)station_index,
	                         ROADSTOP_BUS, ROADTYPE_ROAD, INVALID_ROADTYPE, a);
}
