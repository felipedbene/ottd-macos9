// THE INTEGRATION: real OpenTTD map -> real slopes -> real OpenGFX sprites -> screen.
//
// Pulls together every proven brick:
//   * map.cpp / tile_map.cpp    - real map: AllocateMap + sculpt heights (this TU)
//   * landscape_render.cpp (iso)- faithful port of OpenTTD's projection + slope math
//   * groundsprite.cpp          - real (slope->sprite) atlas from OpenGFX
//   * grf.cpp / sprite_file.cpp - real container-v2 decode of ogfx1_base.grf
//   * blitter/8bpp_simple       - real sprite Encode + Draw
// Result: a real isometric OpenTTD landscape, generated and rendered by OpenTTD's
// own code on Mac OS 9.
#include "stdafx.h"
#include "map_func.h"
#include "tile_map.h"
#include "clear_map.h"
#include "blitter/8bpp_simple.hpp"
#include "spriteloader/spriteloader.hpp"
#include "spriteloader/grf.hpp"
#include "spriteloader/sprite_file_type.hpp"
#include "fileio_type.h"
#include "gfx_type.h"
#include "zoom_type.h"
#include "landscape_render.h"   // iso:: namespace, no OpenTTD-header clash
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" void ottd_log(const char *fmt, ...);

// --- blitter environment (same as the sprite bricks) ---
DrawPixelInfo _screen;
int _debug_driver_level;
void *GetRawSprite(SpriteID, SpriteType, AllocatorProc *, SpriteEncoder *) { return nullptr; }
void DebugPrint(const char *level, const std::string &msg) { ottd_log("dbg %s: %s", level ? level : "?", msg.c_str()); }
static void *sprite_alloc(size_t n) { return std::malloc(n); }

// --- OpenTTD's real DOS palette handed to the Mac front-end ---
#include "table/palettes.h"
extern "C" void ottd_get_palette(unsigned char *rgb)
{
    for (int i = 0; i < 256; i++) {
        rgb[i * 3 + 0] = _palette.palette[i].r;
        rgb[i * 3 + 1] = _palette.palette[i].g;
        rgb[i * 3 + 2] = _palette.palette[i].b;
    }
}

// --- container-v2 sprite index (mirror of ReadGRFSpriteOffsets) ---
static std::map<uint32, size_t> BuildSpriteOffsets(SpriteFile &file)
{
    std::map<uint32, size_t> offsets;
    file.SeekToBegin();
    size_t data_offset = file.ReadDword();
    size_t old_pos = file.GetPos();
    file.SeekTo(data_offset, SEEK_CUR);
    uint32 id, prev_id = 0; size_t prev_pos = 0;
    while ((id = file.ReadDword()) != 0) {
        if (id != prev_id) { if (prev_id != 0) offsets[prev_id] = prev_pos; prev_pos = file.GetPos() - 4; }
        prev_id = id;
        file.SkipBytes((int)file.ReadDword());
    }
    if (prev_id != 0) offsets[prev_id] = prev_pos;
    file.SeekTo(old_pos, SEEK_SET);
    return offsets;
}

static Sprite *LoadRealSprite(SpriteFile &file, const std::map<uint32, size_t> &offsets,
                              uint32 id, Blitter_8bppSimple &blit)
{
    auto it = offsets.find(id);
    if (it == offsets.end()) return nullptr;
    SpriteLoader::Sprite spr[ZOOM_LVL_COUNT];
    SpriteLoaderGrf loader(2);
    uint8 avail = loader.LoadSprite(spr, file, it->second, ST_NORMAL, false, 0);
    if (avail == 0) return nullptr;
    for (int zl = 0; zl < ZOOM_LVL_COUNT; zl++) if (avail & (1 << zl)) return blit.Encode(&spr[zl], sprite_alloc);
    return nullptr;
}

#define MAPN  64          // OpenTTD map (min size 64)
#define GRID  9           // corner-height patch -> (GRID-1)^2 = 64 tiles rendered
#define PEAKX 32
#define PEAKY 32

extern "C" int ottd_landscape_render(unsigned char *fb, int pitch, int w, int h)
{
    _screen.pitch = pitch; _screen.width = w; _screen.height = h;
    Blitter_8bppSimple blit;
    blit.DrawRect(fb, w, h, 0xC9);                       // sky

    // 1) REAL OpenTTD map: allocate + sculpt a cone hill with genuine accessors.
    ottd_log("AllocateMap(%d,%d) + sculpt hill", MAPN, MAPN);
    AllocateMap(MAPN, MAPN);
    for (uint y = 0; y < MAPN; y++) {
        for (uint x = 0; x < MAPN; x++) {
            int d = (x > PEAKX ? (int)x - PEAKX : PEAKX - (int)x)
                  + (y > PEAKY ? (int)y - PEAKY : PEAKY - (int)y);
            int th = 12 - d; if (th < 0) th = 0;
            SetTileHeight(TileXY(x, y), (uint)th);
            MakeClear(TileXY(x, y), CLEAR_GRASS, 3);
        }
    }

    // 2) extract a GRID x GRID corner-height patch centred on the peak (real heights).
    static unsigned char heights[GRID * GRID];
    uint x0 = PEAKX - GRID / 2, y0 = PEAKY - GRID / 2;
    for (int j = 0; j < GRID; j++)
        for (int i = 0; i < GRID; i++)
            heights[j * GRID + i] = (unsigned char)TileHeight(TileXY(x0 + i, y0 + j));
    ottd_log("extracted %dx%d height patch at (%u,%u)", GRID, GRID, x0, y0);

    // 3) compositor: heights -> ordered draw list (slope + sprite id + screen pos).
    static iso::DrawTile tiles[(GRID - 1) * (GRID - 1)];
    int nt = iso::BuildScene(heights, GRID, tiles, /*use_atlas*/ true);
    ottd_log("BuildScene -> %d tiles", nt);

    // centre the iso field: find min screen pos (compositor units /ZOOM_LVL_BASE).
    int minx = 1 << 30, miny = 1 << 30;
    for (int i = 0; i < nt; i++) {
        int sx = tiles[i].screen_x / iso::ZOOM_LVL_BASE, sy = tiles[i].screen_y / iso::ZOOM_LVL_BASE;
        if (sx < minx) minx = sx; if (sy < miny) miny = sy;
    }
    int originX = 40 - minx, originY = 30 - miny;

    // 4) load each needed sprite off the real GRF, then blit in paint order.
    // Pre-check so a missing GRF returns an error code instead of usererror()->abort().
    ottd_log("opening ogfx1_base.grf (cwd) ...");
    FILE *probe = std::fopen("ogfx1_base.grf", "rb");
    if (probe == nullptr) { ottd_log("ERROR: ogfx1_base.grf not found next to app"); return -3; }
    std::fclose(probe);

    SpriteFile file("ogfx1_base.grf", NO_DIRECTORY, false);
    ottd_log("GRF opened; container version = %d", (int)file.GetContainerVersion());
    if (file.GetContainerVersion() != 2) { ottd_log("ERROR: GRF not v2"); return -1; }
    std::map<uint32, size_t> offsets = BuildSpriteOffsets(file);
    ottd_log("indexed %d sprites; blitting %d tiles ...", (int)offsets.size(), nt);
    std::map<int, Sprite *> cache;

    int drawn = 0, skipped = 0;
    for (int i = 0; i < nt; i++) {
        iso::DrawTile &t = tiles[i];
        Sprite *spr;
        auto ci = cache.find(t.sprite);
        if (ci != cache.end()) spr = ci->second;
        else { spr = LoadRealSprite(file, offsets, (uint32)t.sprite, blit); cache[t.sprite] = spr; }
        if (spr == nullptr) { skipped++; continue; }

        int px = originX + t.screen_x / iso::ZOOM_LVL_BASE + spr->x_offs;
        int py = originY + t.screen_y / iso::ZOOM_LVL_BASE + spr->y_offs;
        if (px < 0 || py < 0 || px + spr->width > w || py + spr->height > h) { skipped++; continue; }

        Blitter::BlitterParams bp; std::memset(&bp, 0, sizeof(bp));
        bp.sprite = spr->data;
        bp.sprite_width = spr->width; bp.sprite_height = spr->height;
        bp.width = spr->width; bp.height = spr->height;
        bp.left = px; bp.top = py; bp.dst = fb; bp.pitch = pitch;
        blit.Draw(&bp, BM_NORMAL, ZOOM_LVL_NORMAL);
        drawn++;
    }
    ottd_log("landscape drawn: %d tiles (%d off-screen/failed), %d distinct sprites",
             drawn, skipped, (int)cache.size());
    return drawn;
}
