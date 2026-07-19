/*
 * landscape_render.h -- Isometric landscape compositor core.
 *
 * HOST-ONLY / PPC-portable core. Plain C++ (C++98-compatible), NO Mac Toolbox,
 * NO OpenTTD headers. All projection + slope math is reimplemented here from
 * the OpenTTD 13.4 source so the same object file can later be compiled for
 * classic Mac OS 9 / PPC and fed a real OpenTTD map array.
 *
 * Source authority: OpenTTD 13.4  (/Users/felipe/ottd-macos9/openttd-13.4/src/)
 *   - Projection:      src/landscape.h:82-89   RemapCoords()
 *   - Constants:       src/tile_type.h:15-18   TILE_SIZE/TILE_PIXELS/TILE_HEIGHT
 *                      src/zoom_type.h:15-16   ZOOM_LVL_SHIFT=2, ZOOM_LVL_BASE=4
 *   - Slope-from-corners: src/tile_map.cpp:24-50  GetTileSlopeGivenHeight()
 *   - Corner->tile map:   src/tile_map.cpp:59-72  GetTileSlope()
 *   - Slope enum:      src/slope_type.h:49-69
 *   - Grass sprite:    src/table/sprites.h:581  SPR_FLAT_GRASS_TILE=3981
 *   - Slope->offset:   src/landscape.cpp:79-82  _slope_to_sprite_offset[32]
 *                      src/slope_func.h:415-419 SlopeToSpriteOffset()
 */
#ifndef LANDSCAPE_RENDER_H
#define LANDSCAPE_RENDER_H

namespace iso {

/* ---- OpenTTD constants (verbatim values, cited above) ---- */
enum {
	TILE_SIZE      = 16, /* world units per tile edge      (tile_type.h:15)  */
	TILE_PIXELS    = 32, /* screen px per tile column/row  (tile_type.h:17)  */
	TILE_HEIGHT    =  8, /* world units AND px per z-level (tile_type.h:18)  */
	ZOOM_LVL_SHIFT =  2, /* (zoom_type.h:15)                                 */
	ZOOM_LVL_BASE  =  4  /* 1<<ZOOM_LVL_SHIFT (zoom_type.h:16)               */
};

/* ---- Slope enum, verbatim from src/slope_type.h:49-69 ---- */
enum Slope {
	SLOPE_FLAT    = 0x00,
	SLOPE_W       = 0x01, /* west  corner raised */
	SLOPE_S       = 0x02, /* south corner raised */
	SLOPE_E       = 0x04, /* east  corner raised */
	SLOPE_N       = 0x08, /* north corner raised */
	SLOPE_STEEP   = 0x10, /* steep flag          */
	SLOPE_NW = SLOPE_N | SLOPE_W,
	SLOPE_SW = SLOPE_S | SLOPE_W,
	SLOPE_SE = SLOPE_S | SLOPE_E,
	SLOPE_NE = SLOPE_N | SLOPE_E,
	SLOPE_EW = SLOPE_E | SLOPE_W,
	SLOPE_NS = SLOPE_N | SLOPE_S,
	SLOPE_ELEVATED = SLOPE_N | SLOPE_E | SLOPE_S | SLOPE_W,
	SLOPE_NWS = SLOPE_N | SLOPE_W | SLOPE_S,
	SLOPE_WSE = SLOPE_W | SLOPE_S | SLOPE_E,
	SLOPE_SEN = SLOPE_S | SLOPE_E | SLOPE_N,
	SLOPE_ENW = SLOPE_E | SLOPE_N | SLOPE_W,
	SLOPE_STEEP_W = SLOPE_STEEP | SLOPE_NWS,
	SLOPE_STEEP_S = SLOPE_STEEP | SLOPE_WSE,
	SLOPE_STEEP_E = SLOPE_STEEP | SLOPE_SEN,
	SLOPE_STEEP_N = SLOPE_STEEP | SLOPE_ENW
};

/* A single tile ready to blit, in draw order. */
struct DrawTile {
	int map_x, map_y;   /* tile coords in the heightmap                */
	int slope;          /* Slope enum value                            */
	int base_h;         /* lowest of the 4 corner heights (z-levels)   */
	int sprite;         /* absolute OpenGFX sprite id to draw          */
	int screen_x;       /* screen x of the tile's north-corner anchor  */
	int screen_y;       /* screen y of the tile's north-corner anchor  */
};

/*
 * Slope-from-4-corners.  Faithful port of GetTileSlopeGivenHeight()
 * (src/tile_map.cpp:24-50).  Corner heights are in z-levels (uint8).
 * Writes the tile's base height (lowest corner) to *base_h when non-null.
 */
int SlopeFromCorners(int hnorth, int hwest, int heast, int hsouth, int *base_h);

/*
 * World->screen projection.  Faithful port of RemapCoords() (src/landscape.h:82).
 * Inputs are WORLD coordinates (tile_x*TILE_SIZE, tile_y*TILE_SIZE, z*TILE_HEIGHT).
 *   sx = (wy - wx) * 2 * ZOOM_LVL_BASE
 *   sy = (wy + wx - wz) * ZOOM_LVL_BASE
 */
void RemapCoords(int wx, int wy, int wz, int *sx, int *sy);

/*
 * Grass ground sprite for a slope.  Returns SPR_FLAT_GRASS_TILE + offset.
 * The authoritative atlas is produced by a parallel agent; this is the
 * derived fallback (see _slope_to_sprite_offset in the .cpp, cited).
 */
int GroundSpriteForSlopeFallback(int slope);

/* Provided elsewhere (the authoritative atlas). Declared extern per task. */
extern "C" int GroundSpriteForSlope(int slope);

/*
 * Build the ordered draw list for an NxN grid of CORNER heights.
 *
 *   heights : row-major (size*size) uint8 corner-height grid.
 *   size    : grid dimension (number of corner samples per side).
 *
 * There are (size-1) x (size-1) tiles; tile (x,y) reads corners
 * N=(x,y) W=(x+1,y) E=(x,y+1) S=(x+1,y+1)  (per src/tile_map.cpp:65-70).
 *
 * out      : caller buffer, must hold (size-1)*(size-1) DrawTile.
 * use_atlas: if true, calls extern GroundSpriteForSlope(); else the fallback.
 * Returns the number of tiles written, already in back-to-front paint order.
 */
int BuildScene(const unsigned char *heights, int size,
               DrawTile *out, bool use_atlas);

} /* namespace iso */

#endif /* LANDSCAPE_RENDER_H */
