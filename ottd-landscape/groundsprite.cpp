// Authoritative ground-sprite atlas function (from agent-tiles/tile_sprites.h).
// Kept in its own TU: tile_sprites.h's global SLOPE_* enum would collide with
// OpenTTD's slope_type.h, so this must NOT see any OpenTTD header.
#include "tile_sprites.h"

// grass ground sprite for a slope: base + SlopeToSpriteOffset(slope).
// (src/landscape.cpp:79 _slope_to_sprite_offset; verified against the real GRF.)
extern "C" int GroundSpriteForSlope(int slope)
{
    return SPR_FLAT_GRASS_TILE + SLOPE_TO_SPRITE_OFFSET[slope & 0x1F];
}
