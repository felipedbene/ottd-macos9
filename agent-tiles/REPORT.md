# OpenGFX Ground/Landscape Tile Sprite Atlas

Mapping of (terrain, slope) -> OpenGFX sprite ID, verified by decoding real
ogfx1_base.grf (OpenGFX 8.0) bytes to PNG (see previews/).

For a base-set GRF the container internal sprite ID == OpenTTD global sprite
number, so OpenTTD SPR_* constants are the IDs to decode.
Source tree: /Users/felipe/ottd-macos9/openttd-13.4/. Decoder: gen_tiles.py.

## 1. Slope -> sprite-offset rule (core finding)

    sprite_id = terrain_base + SlopeToSpriteOffset(tileh)

SlopeToSpriteOffset (src/slope_func.h:415) indexes _slope_to_sprite_offset[32]
at src/landscape.cpp:79:

    0, 1, 2, 3, 4, 5, 6,  7, 8, 9, 10, 11, 12, 13, 14, 0,
    0, 0, 0, 0, 0, 0, 0, 16, 0, 0,  0, 17,  0, 15, 18, 0,

Indexed by raw Slope (0..31, src/slope_type.h:49). Each terrain block is 19
sprites wide (offsets 0..18). 15 non-steep slopes -> 0..14; 4 steep -> 15..18.

Slope value -> offset -> grass id (all 19, visually confirmed):

  0  SLOPE_FLAT     0   3981
  1  SLOPE_W        1   3982
  2  SLOPE_S        2   3983
  3  SLOPE_SW       3   3984
  4  SLOPE_E        4   3985
  5  SLOPE_EW       5   3986
  6  SLOPE_SE       6   3987
  7  SLOPE_WSE      7   3988
  8  SLOPE_N        8   3989
  9  SLOPE_NW       9   3990
  10 SLOPE_NS       10  3991
  11 SLOPE_NWS      11  3992
  12 SLOPE_NE       12  3993
  13 SLOPE_ENW      13  3994
  14 SLOPE_SEN      14  3995
  29 SLOPE_STEEP_N  15  3996
  23 SLOPE_STEEP_W  16  3997
  27 SLOPE_STEEP_S  17  3998
  30 SLOPE_STEEP_E  18  3999

Steep order in the block is N,W,S,E -> offsets 15,16,17,18 (ids 3996..3999).

## 2. Terrain base IDs (src/table/sprites.h:578-593)

  Grass density 0 (bare)      3924  SPR_FLAT_BARE_LAND
  Grass density 1 (1/3)       3943  = 3924 + 1*19
  Grass density 2 (2/3)       3962  = 3924 + 2*19
  Grass density 3 (full)      3981  = SPR_FLAT_GRASS_TILE
  Rough / scrub               4000  SPR_FLAT_ROUGH_LAND
  Rocks set 1                 4023  SPR_FLAT_ROCKY_LAND_1
  Rocks set 2                 4042  SPR_FLAT_ROCKY_LAND_2
  Water                       4061  SPR_FLAT_WATER_TILE
  Snow/desert density 0 (1/4) 4493  SPR_FLAT_1_QUART_...  (see quirk)
  Snow/desert density 1 (2/4) 4512  SPR_FLAT_2_QUART_...
  Snow/desert density 2 (3/4) 4531  SPR_FLAT_3_QUART_...
  Snow/desert density 3 (full)4550  SPR_FLAT_SNOW_DESERT_TILE

Grass growth (clear_cmd.cpp:50):
  id = 3924 + density*19 + SlopeToSpriteOffset(slope)   (4x19=76-sprite block)

Rough/rocks (clear_cmd.cpp:56/114): same base+offset rule.
Water (water_cmd): flat + 14 simple slopes only; steep water -> shore
substitution (SPR_SHORE_BASE aliased from grass slopes, newgrf.cpp:9578-9590).
Snow/desert (clear_cmd.cpp:124 + clear_land.h:71):
  _clear_land_sprites_snow_desert = {4493,4512,4531,4550} (stride 19), + offset.
Snow and desert share the same base sprites (climate recolour).

## 3. Trees (optional)

Not a slope formula. src/table/tree_land.h picks 4 raw sprites per tile from a
layout table, each a 7-frame growth sequence. Temperate bases near 0x628 (1576)
and 0x652 (1618). Rendered as proof: tree_0628/0652/0660/066e.png.

## 4. Confirmed vs uncertain

Confirmed (decoded to PNG): all 19 grass slopes; grass 1/3, 2/3; rough; rocks
set1/set2; water flat + simple slope; snow/desert densities 0/2/3 flat; 1/4 flat
at OpenGFX 4494; 4 trees.

QUIRK: OpenGFX 1/4 snow-desert FLAT tile is at 4494, not stock constant 4493
(4493 decodes to a building). 0/2/3-quarter tiles match constants exactly, and
the slope block still starts at 4493+offset, so only the flat (offset 0) entry
is shifted +1. OpenGFX packing artefact, not a rule change.

## Decoder notes

Two encodings (ported from src/spriteloader/grf.cpp:DecodeSingleSprite):
- type&0x08==0: plain RLE -> flat w*h palette buffer.
- type&0x08!=0: RLE -> buffer whose first h*2 (or h*4) bytes are a per-row
  offset table; each row is (length,skip,pixels) chunks with last-item flag;
  unwritten pixels default transparent (index 0).
Palette index 0 = transparent (teal in previews). DOS palette from
src/table/palettes.h.
