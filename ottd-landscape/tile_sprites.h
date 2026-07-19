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
 * tile_sprites.h -- OpenGFX ground/landscape tile sprite atlas for the
 * OpenTTD -> classic Mac OS 9 port.
 *
 * Every ID below is the OpenTTD *global* sprite number, which for a base-set
 * GRF equals the container-v2 internal sprite ID in ogfx1_base.grf.
 * All IDs were VERIFIED by decoding the real GRF bytes to PNG (see previews/).
 *
 * Base constants are taken verbatim from OpenTTD 13.4:
 *   src/table/sprites.h        (SPR_* landscape constants)
 *   src/landscape.cpp:79       (_slope_to_sprite_offset[32])
 *   src/slope_func.h:415       (SlopeToSpriteOffset())
 *   src/slope_type.h:49        (Slope enum)
 *   src/clear_cmd.cpp:50,124   (grass density & snow/desert density selection)
 *   src/table/clear_land.h:71  (_clear_land_sprites_snow_desert)
 *
 * RULE (all ground tiles):
 *     sprite_id = base + SlopeToSpriteOffset(slope)
 * where the slope block is 19 sprites wide (one per valid slope).
 */

#ifndef TILE_SPRITES_H
#define TILE_SPRITES_H

/* ---- base sprite IDs (src/table/sprites.h:578-593) ---- */
#define SPR_FLAT_BARE_LAND                 3924  /* grass density 0 (bare dirt) */
#define SPR_FLAT_1_THIRD_GRASS_TILE        3943  /* grass density 1 (= 3924 + 1*19) */
#define SPR_FLAT_2_THIRD_GRASS_TILE        3962  /* grass density 2 (= 3924 + 2*19) */
#define SPR_FLAT_GRASS_TILE                3981  /* grass density 3, full (= 3924 + 3*19) */
#define SPR_FLAT_ROUGH_LAND                4000  /* rough / scrub ground */
#define SPR_FLAT_ROCKY_LAND_1              4023  /* rocks, set 1 */
#define SPR_FLAT_ROCKY_LAND_2              4042  /* rocks, set 2 (GMB_SECOND_ROCKY_TILE_SET) */
#define SPR_FLAT_WATER_TILE                4061  /* sea/lake water */
#define SPR_FLAT_1_QUART_SNOW_DESERT_TILE  4493  /* snow/desert density 0 -- see NOTE below */
#define SPR_FLAT_2_QUART_SNOW_DESERT_TILE  4512  /* snow/desert density 1 */
#define SPR_FLAT_3_QUART_SNOW_DESERT_TILE  4531  /* snow/desert density 2 */
#define SPR_FLAT_SNOW_DESERT_TILE          4550  /* snow/desert density 3, full */

/*
 * NOTE (OpenGFX 8.0 quirk): the *flat* 1/4 snow-desert tile is at 4494, not the
 * stock constant 4493 (which in ogfx1_base.grf decodes to a building sprite).
 * The 0/2/3-quarter tiles match their stock constants exactly. If you decode from
 * ogfx1_base.grf, use 4494 for the 1/4 flat tile. The +offset slope block still
 * starts at the constant (4493+offset), so only the flat (offset 0) entry differs.
 */
#define SPR_OPENGFX_1_QUART_SNOW_DESERT_FLAT 4494

/*
 * ---- slope -> sprite offset table (src/landscape.cpp:79) ----
 * Indexed by Slope value 0..31. Only the 19 valid slopes have meaningful
 * offsets (0..18); invalid entries map to 0. Steep slopes (0x10 bit) live at
 * offsets 15..18. This is _slope_to_sprite_offset copied verbatim.
 */
static const unsigned char SLOPE_TO_SPRITE_OFFSET[32] = {
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,  0,
	 0,  0,  0,  0,  0,  0,  0, 16,  0,  0,  0, 17,  0, 15, 18,  0,
};

/* Slope enum values (src/slope_type.h:49). The 19 that produce ground sprites. */
enum TileSlope {
	SLOPE_FLAT    = 0x00,
	SLOPE_W       = 0x01,
	SLOPE_S       = 0x02,
	SLOPE_SW      = 0x03,  /* S|W */
	SLOPE_E       = 0x04,
	SLOPE_EW      = 0x05,  /* E|W */
	SLOPE_SE      = 0x06,  /* S|E */
	SLOPE_WSE     = 0x07,  /* W|S|E */
	SLOPE_N       = 0x08,
	SLOPE_NW      = 0x09,  /* N|W */
	SLOPE_NS      = 0x0A,  /* N|S */
	SLOPE_NWS     = 0x0B,  /* N|W|S */
	SLOPE_NE      = 0x0C,  /* N|E */
	SLOPE_ENW     = 0x0D,  /* E|N|W */
	SLOPE_SEN     = 0x0E,  /* S|E|N */
	SLOPE_STEEP_W = 0x17,  /* 23 = STEEP|NWS */
	SLOPE_STEEP_S = 0x1B,  /* 27 = STEEP|WSE */
	SLOPE_STEEP_N = 0x1D,  /* 29 = STEEP|ENW */
	SLOPE_STEEP_E = 0x1E,  /* 30 = STEEP|SEN */
};

/* Mirrors SlopeToSpriteOffset() from src/slope_func.h:415. */
static inline unsigned SlopeToSpriteOffset(unsigned slope)
{
	return SLOPE_TO_SPRITE_OFFSET[slope & 0x1F];
}

/*
 * Ground-tile sprite selection. `terrain_base` is one of the SPR_* constants
 * above (for grass, pass SPR_FLAT_BARE_LAND + density*19, density 0..3).
 * Water is only valid for the flat + 14 simple (non-steep) slopes; the game
 * substitutes shore/coast sprites for steep water, so do not feed a steep
 * slope to SPR_FLAT_WATER_TILE.
 */
static inline unsigned GetGroundTileSprite(unsigned terrain_base, unsigned slope)
{
	return terrain_base + SlopeToSpriteOffset(slope);
}

/* Grass by growth density (0=bare .. 3=full grass). src/clear_cmd.cpp:50 */
static inline unsigned GetGrassGroundBase(unsigned density /*0..3*/)
{
	return SPR_FLAT_BARE_LAND + density * 19;
}

/* Snow/desert by density (0..3). src/clear_cmd.cpp:124 + clear_land.h:71.
 * Returns the stock constant base; add SlopeToSpriteOffset(slope).
 * (For OpenGFX flat density-0, prefer SPR_OPENGFX_1_QUART_SNOW_DESERT_FLAT.) */
static inline unsigned GetSnowDesertGroundBase(unsigned density /*0..3*/)
{
	static const unsigned bases[4] = {
		SPR_FLAT_1_QUART_SNOW_DESERT_TILE,
		SPR_FLAT_2_QUART_SNOW_DESERT_TILE,
		SPR_FLAT_3_QUART_SNOW_DESERT_TILE,
		SPR_FLAT_SNOW_DESERT_TILE,
	};
	return bases[density & 3];
}

#endif /* TILE_SPRITES_H */
