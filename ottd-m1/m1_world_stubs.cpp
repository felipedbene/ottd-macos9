// R1 world-tiles seam: the handful of off-path symbols the real tree_cmd.cpp (and
// later water_cmd.cpp) reference that we don't compile. All no-ops — never hit on
// the draw/grow path for plain grass-ground trees. (R1 render-merge only.)
#include "stdafx.h"
#include "water.h"

// Referenced by tree_cmd for shore/water-ground trees, which we never place.
void DrawShoreTile(Slope) {}
void TileLoop_Water(TileIndex) {}
