// R1 minimal water: a custom _tile_type_water_procs so the real viewport can draw
// MP_WATER (sea) tiles WITHOUT pulling water_cmd.cpp's airport/canal/dock/flood
// dependencies. Our sea is always flat at height 0, so the draw is just the flat
// water sprite (which animates via the palette DoPaletteAnimations already fills).
// The zeroed common `_tile_type_water_procs` in m1_deadpools.c merges into this
// strong definition, exactly like void_cmd's proc table. (R1 render-merge only.)
#include "stdafx.h"
#include "landscape.h"
#include "command_func.h"
#include "viewport_func.h"
#include "slope_func.h"
#include "tile_map.h"

#include "table/strings.h"
#include "table/sprites.h"

static void DrawTile_Water(TileInfo *ti)
{
	DrawGroundSprite(SPR_FLAT_WATER_TILE, PAL_NONE);
}

static int GetSlopePixelZ_Water(TileIndex tile, uint x, uint y)
{
	return 0;   // our sea is flat at height 0
}

static Foundation GetFoundation_Water(TileIndex tile, Slope tileh)
{
	return FOUNDATION_NONE;
}

static CommandCost ClearTile_Water(TileIndex tile, DoCommandFlag flags)
{
	return_cmd_error(STR_ERROR_CAN_T_BUILD_ON_WATER);
}

static void GetTileDesc_Water(TileIndex tile, TileDesc *td)
{
	td->str = STR_EMPTY;
	td->owner[0] = OWNER_WATER;
}

static void TileLoop_Water(TileIndex tile) {}
static void ChangeTileOwner_Water(TileIndex tile, Owner old_owner, Owner new_owner) {}

static TrackStatus GetTileTrackStatus_Water(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	return 0;
}

static CommandCost TerraformTile_Water(TileIndex tile, DoCommandFlag flags, int z_new, Slope tileh_new)
{
	return_cmd_error(STR_ERROR_CAN_T_BUILD_ON_WATER);
}

extern const TileTypeProcs _tile_type_water_procs = {
	DrawTile_Water,           // draw_tile_proc
	GetSlopePixelZ_Water,     // get_slope_z_proc
	ClearTile_Water,          // clear_tile_proc
	nullptr,                  // add_accepted_cargo_proc
	GetTileDesc_Water,        // get_tile_desc_proc
	GetTileTrackStatus_Water, // get_tile_track_status_proc
	nullptr,                  // click_tile_proc
	nullptr,                  // animate_tile_proc
	TileLoop_Water,           // tile_loop_proc
	ChangeTileOwner_Water,    // change_tile_owner_proc
	nullptr,                  // add_produced_cargo_proc
	nullptr,                  // vehicle_enter_tile_proc
	GetFoundation_Water,      // get_foundation_proc
	TerraformTile_Water,      // terraform_tile_proc
};
