// B2 demo scene: B1's real-pipeline landscape, now drawn into the driver's
// _screen with a scroll offset. Init (blitter + sprite cache + GRF) happens
// BEFORE the video driver starts, mirroring the real game's order
// (SelectBlitter -> GfxInitSpriteMem -> load sprites -> driver Start).
#include "stdafx.h"
#include "gfx_func.h"
#include "gfx_type.h"
#include "spritecache.h"
#include "blitter/factory.hpp"
#include "landscape.h"
#include "slope_func.h"
#include "table/sprites.h"
#include "zoom_type.h"
#include "fileio_type.h"
#include <cstring>

extern "C" void ottd_log(const char *fmt, ...);
extern DrawPixelInfo *_cur_dpi;
extern uint _sprite_cache_size;
void DoPaletteAnimations(); // gfx.cpp — fills the animated palette slots (water/fire/...)

// OpenTTD's real DOS palette -> _cur_palette (the full game's GfxInitPalettes
// does the equivalent; the driver picks it up via CopyPalette at Start).
#include "table/palettes.h"
static void init_cur_palette()
{
    _cur_palette = _palette;
    _cur_palette.first_dirty = 0;
    _cur_palette.count_dirty = 256;
}

// Copy of gfxinit.cpp's LoadGrfFile (static there); public spritecache APIs only.
static uint LoadGrfFileB2(const char *filename, uint load_index, bool needs_palette_remap)
{
    uint sprite_id = 0;
    SpriteFile &file = OpenCachedSpriteFile(filename, BASESET_DIR, needs_palette_remap);
    byte cv = file.GetContainerVersion();
    ottd_log("B2: grf '%s' container=%d", filename, (int)cv);
    if (cv == 0) return 0;
    ReadGRFSpriteOffsets(file);
    if (cv >= 2) { byte comp = file.ReadByte(); if (comp != 0) { ottd_log("B2: bad compression"); return 0; } }
    while (true) {
        bool more = LoadNextSprite((int)load_index, file, sprite_id);
        if (!more) break;
        load_index++; sprite_id++;
    }
    ottd_log("B2: registered up to sprite %u", load_index);
    return load_index;
}

// gentle smooth heightmap -> mostly flat + single-corner slopes
static int slope_from(int hn, int hw, int he, int hs, int *base)
{
    int hmin = hn; if (hw<hmin)hmin=hw; if (he<hmin)hmin=he; if (hs<hmin)hmin=hs;
    int hmax = hn; if (hw>hmax)hmax=hw; if (he>hmax)hmax=he; if (hs>hmax)hmax=hs;
    *base = hmin; int r = 0;
    if (hn!=hmin) r|=SLOPE_N; if (hw!=hmin) r|=SLOPE_W;
    if (he!=hmin) r|=SLOPE_E; if (hs!=hmin) r|=SLOPE_S;
    if (hmax-hmin==2) r|=SLOPE_STEEP;
    return r;
}

static const int N = 20; // 19x19 tiles: big enough to scroll around in
static unsigned char H[N * N];

extern "C" int b2_scene_init(void)
{
    _sprite_cache_size = 2;
    init_cur_palette();

    Blitter *bl = BlitterFactory::SelectBlitter("8bpp-simple");
    if (bl == nullptr) { ottd_log("B2: SelectBlitter failed"); return -1; }
    GfxInitSpriteMem(); // CRITICAL before any GRF load (B1 lesson: NULL pool writes low memory)

    if (LoadGrfFileB2("ogfx1_base.grf", 0, false) == 0) { ottd_log("B2: grf load failed"); return -2; }

    // smooth two-hill field
    for (int j = 0; j < N; j++) {
        for (int i = 0; i < N; i++) {
            int d1 = (i-6>0?i-6:6-i) + (j-6>0?j-6:6-j);
            int d2 = (i-14>0?i-14:14-i) + (j-13>0?j-13:13-j);
            int d = d1 < d2 ? d1 : d2;
            int th = 4 - d/3; if (th < 0) th = 0;
            H[j*N+i] = (unsigned char)th;
        }
    }
    // Carve a SMOOTH bowl down to the lake: cap each corner to its Chebyshev
    // distance from the lake box (corners i:1..6, j:13..19). So water corners are
    // 0 and the terrain ramps up by <=1 level per tile — no steep (>1) shore slopes,
    // whose sprites don't cover the drop and leaked water over the grass (the tongue).
    for (int j = 0; j < N; j++) {
        for (int i = 0; i < N; i++) {
            int di = (i < 1) ? (1 - i) : (i > 6 ? i - 6 : 0);
            int dj = (j < 13) ? (13 - j) : (j > 19 ? j - 19 : 0);
            int dist = di > dj ? di : dj;
            if ((int)H[j*N+i] > dist) H[j*N+i] = (unsigned char)dist;
        }
    }

    // Fill the animated palette slots. The water sprite's pixels index into this
    // range; without this call those entries are black and the lake renders as a
    // black hole (build-15 screenshot). Blitter is already selected above.
    DoPaletteAnimations();
    ottd_log("B2: scene init ok");
    return 0;
}

// Tap-to-zoom cycles this; b2_scene_draw reads it. ZoomLevel: NORMAL=0..OUT_8X=3.
static ZoomLevel g_zoom = ZOOM_LVL_OUT_4X;
extern "C" void b2_scene_set_zoom(int z)
{
    if (z < ZOOM_LVL_NORMAL) z = ZOOM_LVL_NORMAL;
    if (z > ZOOM_LVL_OUT_8X) z = ZOOM_LVL_OUT_8X;
    g_zoom = (ZoomLevel)z;
    ottd_log("B2: zoom set to %d", z);
}
extern "C" int b2_scene_get_zoom(void) { return (int)g_zoom; }

// A small flat lake in the SW corner (well clear of the two hills at 6,6 / 14,13).
static inline bool b2_is_water(int tx, int ty) { return tx >= 1 && tx <= 5 && ty >= 13 && ty <= 18; }

// Currently-selected tile, drawn as a white tile cursor on top of the landscape.
static bool g_sel_valid = false;
static int  g_sel_tx = 0, g_sel_ty = 0, g_sel_base = 0, g_sel_slope = 0;

// Tile geometry: water tiles are flat at sea level; land tiles take their heightmap slope.
// Returns true for water. base/slope come back for both the draw sprite and the pick projection.
static bool b2_tile_geom(int tx, int ty, int *base, int *slope)
{
    if (b2_is_water(tx, ty)) { *base = 0; *slope = 0; return true; }
    *slope = slope_from(H[ty*N+tx], H[ty*N+tx+1], H[(ty+1)*N+tx], H[(ty+1)*N+tx+1], base);
    return false;
}

// Find the tile whose projected centre is nearest a screen pixel. Forward-projects
// every tile with the SAME math b2_scene_draw uses (immune to inversion bugs), so it
// stays correct at any zoom/scroll by construction.
static void b2_find_tile(int screen_x, int screen_y, int scroll_x, int scroll_y,
                         int *otx, int *oty, int *obase, int *oslope)
{
    int z = (int)g_zoom;
    int dleft = (-(_screen.width / 2) + scroll_x) << z;
    int dtop  = (-40 + scroll_y) << z;
    int btx = -1, bty = -1, bbase = 0, bslope = 0, bestd = 0x7fffffff;
    for (int ty = 0; ty < N - 1; ty++) {
        for (int tx = 0; tx < N - 1; tx++) {
            int base, slope; b2_tile_geom(tx, ty, &base, &slope);
            Point pt = RemapCoords(tx * TILE_SIZE + TILE_SIZE / 2, ty * TILE_SIZE + TILE_SIZE / 2, base * TILE_HEIGHT);
            int spx = (int)((pt.x - dleft) >> z);
            int spy = (int)((pt.y - dtop) >> z);
            int ex = spx - screen_x, ey = spy - screen_y;
            int d = ex * ex + ey * ey;
            if (d < bestd) { bestd = d; btx = tx; bty = ty; bbase = base; bslope = slope; }
        }
    }
    *otx = btx; *oty = bty; *obase = bbase; *oslope = bslope;
}

// Pick the tile under a screen pixel: remember it (for the highlight) and log it.
extern "C" void b2_scene_pick(int screen_x, int screen_y, int scroll_x, int scroll_y)
{
    int tx, ty, base, slope;
    b2_find_tile(screen_x, screen_y, scroll_x, scroll_y, &tx, &ty, &base, &slope);
    g_sel_valid = (tx >= 0);
    g_sel_tx = tx; g_sel_ty = ty; g_sel_base = base; g_sel_slope = slope;
    ottd_log("B2: pick screen(%d,%d) -> tile(%d,%d) h=%d slope=%d %s",
             screen_x, screen_y, tx, ty, base, slope, b2_is_water(tx, ty) ? "water" : "land");
}

// Cycle zoom one step while keeping the world point under (cx,cy) fixed on screen.
// screen_x = (pt.x >> z) + w/2 - sx  =>  sx = (pt.x >> z) + w/2 - cx  (and similarly sy).
extern "C" void b2_scene_zoom_at(int cx, int cy, int old_sx, int old_sy, int *new_sx, int *new_sy)
{
    int tx, ty, base, slope;
    b2_find_tile(cx, cy, old_sx, old_sy, &tx, &ty, &base, &slope);
    int nz = (int)g_zoom + 1; if (nz > ZOOM_LVL_OUT_8X) nz = ZOOM_LVL_NORMAL;
    b2_scene_set_zoom(nz);
    if (tx < 0) { *new_sx = old_sx; *new_sy = old_sy; return; }
    Point pt = RemapCoords(tx * TILE_SIZE + TILE_SIZE / 2, ty * TILE_SIZE + TILE_SIZE / 2, base * TILE_HEIGHT);
    *new_sx = (int)(pt.x >> nz) + _screen.width / 2 - cx;
    *new_sy = (int)(pt.y >> nz) + 40 - cy;
}

// Draw the landscape into _screen shifted by (scroll_x, scroll_y) screen pixels.
extern "C" void b2_scene_draw(int scroll_x, int scroll_y)
{
    DrawPixelInfo dpi;
    dpi.dst_ptr = _screen.dst_ptr;
    dpi.pitch = _screen.pitch;
    // EVERY geometric field of a viewport dpi is in virtual (ZOOM_LVL_BASE) units,
    // width/height included — GfxBlitter clips with
    //   x + ScaleByZoom(bp.width, zoom) - dpi->width
    // where x is virtual. Raw screen pixels here shrink the clip window to
    // width/ZOOM_LVL_BASE: the 640x480 demo drew only a 160x120 corner sliver
    // (the B2 build-9 screenshot; B1 had the same latent bug at 520x392).
    // Virtual units = screen pixels << zoom (ScaleByZoom). At ZOOM_LVL_OUT_4X this
    // equals *ZOOM_LVL_BASE (=1<<2); using << g_zoom generalizes it to every level
    // so zoom stays correctly clipped/centered as the tap-to-zoom cycles it.
    int z = (int)g_zoom;
    dpi.width  = _screen.width  << z;
    dpi.height = _screen.height << z;
    dpi.zoom = g_zoom;
    dpi.left = (-(_screen.width / 2) + scroll_x) << z;
    dpi.top  = (-40 + scroll_y) << z;
    _cur_dpi = &dpi;

    static bool logged_dpi = false;
    if (!logged_dpi) {
        logged_dpi = true;
        ottd_log("B2: dpi l=%d t=%d w=%d h=%d zoom=%d (virtual units)",
                 dpi.left, dpi.top, dpi.width, dpi.height, (int)dpi.zoom);
    }

    std::memset(_screen.dst_ptr, 0xC9, (size_t)_screen.pitch * _screen.height); // sky

    int drawn = 0;
    // Painter's order by TRUE screen depth. RemapCoords gives pt.y=(x+y-z)*ZB with
    // x,y in world units (tile*TILE_SIZE) and z in height*TILE_HEIGHT, so a tile's
    // depth is proportional to 2*(tx+ty) - base. Plain tx+ty (build 18) ignored the
    // height term and mis-ordered raised shore tiles vs flat water. Draw ascending.
    for (int key = -8; key <= 4 * (N - 2); key++) {
        for (int ty = 0; ty < N - 1; ty++) {
            for (int tx = 0; tx < N - 1; tx++) {
                int base, slope;
                bool water = b2_tile_geom(tx, ty, &base, &slope);
                if (2 * (tx + ty) - base != key) continue;
                // terrain by elevation: grass lowland, rough scrub mid, rocky peaks
                SpriteID gbase = SPR_FLAT_GRASS_TILE;
                if (base >= 4)      gbase = SPR_FLAT_ROCKY_LAND_1;
                else if (base >= 3) gbase = SPR_FLAT_ROUGH_LAND;
                SpriteID sid = water ? SPR_FLAT_WATER_TILE
                                     : (gbase + SlopeToSpriteOffset((Slope)slope));
                Point pt = RemapCoords(tx * TILE_SIZE, ty * TILE_SIZE, base * TILE_HEIGHT);
                DrawSpriteViewport(sid, PAL_NONE, pt.x, pt.y, nullptr);
                drawn++;
            }
        }
    }

    // White tile cursor over the picked tile (SPR_SELECT_TILE has the same 19 slope
    // variants as ground, so index it the same way).
    if (g_sel_valid && g_sel_tx >= 0 && g_sel_tx < N - 1 && g_sel_ty >= 0 && g_sel_ty < N - 1) {
        SpriteID sel = SPR_SELECT_TILE + SlopeToSpriteOffset((Slope)g_sel_slope);
        Point pt = RemapCoords(g_sel_tx * TILE_SIZE, g_sel_ty * TILE_SIZE, g_sel_base * TILE_HEIGHT);
        DrawSpriteViewport(sel, PAL_NONE, pt.x, pt.y, nullptr);
    }

    _cur_dpi = nullptr;
}
