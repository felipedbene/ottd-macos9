// OpenTTD game logic on Mac OS 9: run the REAL map subsystem.
//
// Links OpenTTD's genuine map.cpp (AllocateMap, the _m/_me tile arrays) and
// tile_map.cpp (GetTileSlope — the real height->slope math), plus inline tile
// accessors (SetTileHeight, MakeClear). We allocate a map, sculpt a heightmap
// with real accessors, then ask OpenTTD to compute every tile's slope. The
// slope array this produces is exactly what the isometric renderer consumes —
// this is the simulation half that feeds the graphics half.
#include "stdafx.h"
#include "map_func.h"
#include "tile_map.h"
#include "clear_map.h"
#include <cstring>

extern "C" void ottd_log(const char *fmt, ...);

// A gentle pyramidal hill so the map has flats, inclined slopes and a steep peak.
static uint HillHeight(uint x, uint y, uint size)
{
    int cx = (int)size / 2, cy = (int)size / 2;
    int d = (x > (uint)cx ? (int)x - cx : cx - (int)x)
          + (y > (uint)cy ? (int)y - cy : cy - (int)y);
    int h = 10 - d / 3;
    if (h < 0) h = 0;
    return (uint)h;
}

// Fills summary (>=256 bytes) with a human-readable report; returns tiles processed.
extern "C" int run_map_logic(char *summary, int cap)
{
    const uint SIZE = 64;   // 64x64 map (must be power of two)

    ottd_log("=== game logic: AllocateMap(%u, %u) ===", SIZE, SIZE);
    AllocateMap(SIZE, SIZE);
    ottd_log("map allocated: MapSize=%u MapSizeX=%u MapMaxX=%u", (uint)MapSize(), (uint)MapSizeX(), (uint)MapMaxX());

    // 1) sculpt the heightmap + lay clear grass, all via OpenTTD's real accessors.
    uint maxh = 0;
    for (uint y = 0; y < SIZE; y++) {
        for (uint x = 0; x < SIZE; x++) {
            TileIndex t = TileXY(x, y);
            uint h = HillHeight(x, y, SIZE);
            if (h > maxh) maxh = h;
            SetTileHeight(t, h);
            MakeClear(t, CLEAR_GRASS, 3);
        }
    }
    ottd_log("sculpted heightmap (peak height=%u) and laid clear grass", maxh);

    // 2) ask OpenTTD to compute the slope of every tile: the real simulation output.
    int hist[32]; std::memset(hist, 0, sizeof(hist));
    uint flat = 0, sloped = 0, steep = 0;
    for (uint y = 0; y < SIZE; y++) {
        for (uint x = 0; x < SIZE; x++) {
            int z;
            Slope s = GetTileSlope(TileXY(x, y), &z);
            hist[s & 31]++;
            if (s == SLOPE_FLAT) flat++; else sloped++;
            if (s & SLOPE_STEEP) steep++;
        }
    }
    ottd_log("slopes computed: flat=%u sloped=%u steep=%u", flat, sloped, steep);

    // Log a few representative tiles (peak + a hillside) with their real slope value.
    for (uint i = 0; i < SIZE; i += 12) {
        int z; Slope s = GetTileSlope(TileXY(i, 32), &z);
        ottd_log("tile(%u,32): height=%d slope=0x%02X", i, z, (int)s);
    }

    // Distinct slope shapes actually present (a proxy for terrain variety).
    int distinct = 0;
    for (int i = 0; i < 32; i++) if (hist[i]) distinct++;
    ottd_log("distinct slope shapes present: %d", distinct);

    int n = 0;
    n += std::snprintf(summary + n, cap - n, "OpenTTD map subsystem on Mac OS 9:\n");
    n += std::snprintf(summary + n, cap - n, "  map %ux%u = %u tiles\n", SIZE, SIZE, (uint)MapSize());
    n += std::snprintf(summary + n, cap - n, "  peak height: %u\n", maxh);
    n += std::snprintf(summary + n, cap - n, "  flat=%u sloped=%u steep=%u\n", flat, sloped, steep);
    n += std::snprintf(summary + n, cap - n, "  distinct slope shapes: %d\n", distinct);
    return (int)MapSize();
}
