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
// R1 render-merge: the real engine builds a town into the real map, then the
// scene draws that map (instead of B2's synthetic H[] heightmap).
#include "openttd.h"
#include "map_func.h"
#include "tile_map.h"
#include "clear_map.h"
#include "void_map.h"
#include "town.h"
#include "town_type.h"
#include "town_kdtree.h"
#include "town_cmd.h"
#include "landscape_cmd.h"
#include "company_type.h"
#include "company_func.h"
#include "command_func.h"
#include "road_type.h"
#include "road_map.h"
#include "road_func.h"     // DiagDirToRoadBits (bus route tracing)
#include "direction_type.h"// Direction (bus facing -> road-vehicle sprite)
#include "road_cmd.h"      // CMD_BUILD_ROAD (click-to-build interactivity)
#include "town_map.h"
#include "tree_map.h"      // MakeTree: real forests via the real DrawTile_Trees
#include "water_map.h"     // MakeSea: a real lake (drawn by our minimal water proc)
#include "industry_map.h"  // MakeIndustry: real industry tiles (minimal draw proc)
#include "sprite.h"
#include "date_func.h"
#include "settings_type.h"
// R1 real-renderer: the game's OWN viewport draws the map (replacing the bespoke
// per-tile loop below). ViewportDoDraw -> ViewportAddLandscape -> the real
// _tile_type_procs[..]->draw_tile_proc (DrawTile_Clear/Road/Town) -> draw-list -> blit.
#include "viewport_func.h"
#include "viewport_type.h"
#include "tilehighlight_func.h"   // _thd: the real tile-selection highlight the viewport draws
#include "fontcache.h"            // InitializeUnicodeGlyphMap: bind the sprite font to the loaded GRF
#include "window_gui.h"           // Window, WindowDesc (Phase 1 Step 2: real main window)
#include "widget_type.h"          // NWidgetPart, NWID_VIEWPORT, NWidgetViewport
#include "window_func.h"          // InitWindowSystem
#include "zoom_func.h"            // ScaleZoomGUI
#include "townname_func.h"        // GenerateTownNameString (R1-53 sign diagnostic)
#include "language.h"             // ReadLanguagePack, LanguageMetadata (R1-57 string system)
#include "string_func.h"          // strecpy
#include "widgets/dropdown_func.h" // ShowDropDownMenu (R1-59 real dropdown menu)
#include "widgets/dropdown_type.h" // ShowDropDownList + DropDownListStringItem (auto-width, R1-60)
#include "toolbar_gui.h"          // AllocateToolbar (R1-61 canonical toolbar)
#include "core/alloc_func.hpp"    // CallocT (manual nested-tree build, R1-32)
#include "gfx_func.h"             // _colour_gradient
#include <cstring>
#include <cstdio>

// Our own copy of the original-house sprite table (static in town_cmd.cpp; the
// header is self-contained — defines its own M macro). Lets the scene pick each
// house's real building sprite by HouseType, like DrawTile_Town does.
#include "table/town_land.h"

extern "C" void ottd_log(const char *fmt, ...);
extern "C" long long r1_heap_probe(void);   // FreeMem<<0 | MaxBlock<<32 (macclassic_sys.c)
extern GameMode _game_mode;
extern byte _display_opt;   // transparency.cpp — which names/signs/details to draw (zeroed deadpool: enable town names)
extern bool _generating_world;
void UpdateTownRadius(Town *t);
// Declared only locally in openttd.cpp/town_cmd.cpp upstream (no public header) —
// declare them here like m1_run.cpp does. IncreaseDate advances the real calendar
// (one day per DAY_TICKS calls, running the monthly/yearly town loops); OnTick_Town
// runs the per-tick TownTickHandler -> GrowTown growth for every town in the pool.
void IncreaseDate();
void OnTick_Town();
void InitializeSpriteSorter();   // viewport.cpp — sets _vp_sprite_sorter (call once before drawing)
void UpdateAllTownVirtCoords();  // town_cmd.cpp — positions each town sign + fills the viewport-sign kdtree
void r1_make_company();          // m1_company.cpp — stand up one player company (lights up the toolbar)

// R1 live sim: once the town is seeded and set to grow, the render loop ticks the
// real engine each frame so the town keeps growing on screen (see r1_tick below).
static bool g_live = false;
extern "C" int r1_is_live(void) { return g_live ? 1 : 0; }

// R1-47 toolbar state: the real-window toolbar's PAUSE/FAST-FORWARD buttons flip these;
// r1_tick reads them (pause freezes the sim, fast runs it several sub-steps per frame).
// The window system + UI stay live while paused (UpdateWindows is independent of r1_tick).
static bool g_r1_paused = false;
static bool g_r1_fast   = false;

// Redraw only when the map actually changed (a town grew) — not on a timer. A full
// real-viewport redraw of the whole rich map (trees/water/industries/HUD) is heavy on
// PPC; between growths nothing moves, so repainting is wasted work. r1_tick sets this
// when the total house count changes; UpdateWindows consumes it (plus input redraws).
// Two levels of redraw: FULL (whole map — a town grew, or input) vs BUS (only the
// small region around the moving vehicle — cheap, so it can animate every frame).
// (The wall-clock throttle from R1-23 felt worse — batched growth read as laggy — so
// back to R1-22's immediate full redraw on change; the real fix is native dirty-blocks
// via the window system, Phase 1 Step 2.)
static bool g_full_dirty = false, g_bus_dirty = false;
extern "C" int r1_take_dirty(void)
{
    if (g_full_dirty) { g_full_dirty = false; g_bus_dirty = false; return 2; }
    if (g_bus_dirty)  { g_bus_dirty = false; return 1; }
    return 0;
}

static const uint R1_MAP = 64;

// Found one growing town centred at world (cx,cy). Mirrors the build-5..10 founding:
// the Town pool isn't zero-alloc, so the caches/accumulators are memset; the town is
// inserted in the kdtree and its zone radius computed. Returns nullptr if pool full.
static Town *r1_found_town(uint cx, uint cy)
{
    if (!Town::CanAllocateItem()) return nullptr;
    TileIndex centre = TileXY(cx, cy);
    Town *t = new Town(centre);
    std::memset((void *)&t->cache, 0, sizeof(t->cache));
    std::memset(t->goal, 0, sizeof(t->goal));
    std::memset(t->supplied, 0, sizeof(t->supplied));
    std::memset(t->received, 0, sizeof(t->received));
    std::memset(t->unwanted, 0, sizeof(t->unwanted));
    t->xy = centre;
    t->cache.num_houses = 0; t->cache.population = 0; t->time_until_rebuild = 10;
    t->flags = 0; t->grow_counter = t->index % TOWN_GROWTH_TICKS;
    t->growth_rate = TownTicksToGameTicks(250); t->show_zone = false;
    t->fund_buildings_months = 0; t->road_build_months = 0; t->noise_reached = 0;
    for (uint i = 0; i != MAX_COMPANIES; i++) t->ratings[i] = RATING_INITIAL;
    t->have_ratings = 0; t->exclusivity = INVALID_COMPANY; t->exclusive_counter = 0;
    // Name the town exactly like the real DoCreateTown (town_cmd.cpp:1880): townnametype
    // must be the SPECSTR_TOWNNAME_* StringID for the built-in generator, NOT 0 — with 0
    // the real GetTownName -> GetStringWithArgs(0) returns "(undefined string)". The varied
    // per-town seed (townnameparts) then makes each name distinct.
    t->statues = 0;
    {
        TownNameParams tnp(_settings_game.game_creation.town_name);
        t->townnamegrfid = tnp.grfid;
        t->townnametype  = tnp.type;
    }
    t->townnameparts = (uint32)centre * 0x9E3779B1u + 0x51ED2701u;
    t->larger_town = false; t->layout = TL_ORIGINAL;
    _town_kdtree.Insert(t->index);
    UpdateTownRadius(t);
    return t;
}

// Town sites. Each gets a small flattened pad so it seeds cleanly; the rest of the
// map is organic noise-based hills (see r1_build_world).
struct R1XY { uint x, y; };
static const R1XY R1_TOWNS[] = { {32,32}, {16,16}, {48,16}, {16,48}, {48,48} };

// A lake in the northern wilderness (clear of every town). Carved to sea level before
// the relaxation pass so its shores slope gently, then filled with sea.
static const int R1_LAKE_X = 36, R1_LAKE_Y = 10, R1_LAKE_R = 6;
static inline bool r1_in_lake(int x, int y)
{
    int dx = x - R1_LAKE_X, dy = y - R1_LAKE_Y;
    return dx * dx + dy * dy <= R1_LAKE_R * R1_LAKE_R;
}

// Two industries on flat pads in the wilderness, drawn by our minimal industry proc
// (m1_industry_draw.cpp) from the real _industry_draw_tile_data. Tiles = {dx,dy,gfx}
// from the original coal/power/factory layouts (table/build_industry.h).
struct R1IndTile { int dx, dy; byte gfx; };
static const R1IndTile R1_POWER[] = {
    {0,0,7},{0,1,9},{1,0,7},{1,1,8},{2,0,7},{2,1,8},{3,0,10},{3,1,10} };
static const R1IndTile R1_FACTORY[] = {
    {0,0,39},{0,1,40},{1,0,41},{1,1,42},{0,2,39},{0,3,40},{1,2,41},{1,3,42},
    {2,1,39},{2,2,40},{3,1,41},{3,2,42} };
struct R1IndSite { const R1IndTile *tiles; int n, bx, by, w, h; };
static const R1IndSite R1_INDUSTRIES[] = {
    { R1_POWER,   8, 50, 30, 4, 2 },   // power station, east
    { R1_FACTORY, 12,  7, 27, 4, 4 },  // factory, west
};

static int r1_noise(int x, int y, int scale);   // defined below (value noise)
static void r1_trace_route(void);                // defined below (bus route)
static void r1_make_viewport(int scroll_x, int scroll_y, Viewport *vp);  // defined below (draw)
// Tap-to-zoom cycles this; the draw + pick + bus read it. NORMAL=0..OUT_8X=3.
static ZoomLevel g_zoom = ZOOM_LVL_OUT_4X;

// A river flowing out of the lake, drifting south-east with an organic meander so it
// threads the wilderness between towns. Returns the river-centre x for row y (-1 = no
// river on this row). Carved to height 0 (like the lake) then filled with MakeRiver.
static int r1_river_x(int y)
{
    if (y < 16 || y > 56) return -1;
    int base = 36 + (y - 16) / 6;                 // drift toward the SE
    int wob  = (r1_noise(50, y, 7) - 128) / 40;   // meander ~ +-3
    return base + wob;
}

// Value noise for an organic heightmap (no Perlin/TGP dependency): hash the integer
// lattice, bilinearly interpolate. Two octaves summed give rolling hills instead of
// the old Chebyshev cones (which terraced into pyramids).
static int r1_hash(int x, int y)
{
    unsigned int h = (unsigned int)(x * 374761393 + y * 668265263);
    h = (h ^ (h >> 13)) * 1274126177u;
    return (int)((h >> 24) & 0xFF);   // 0..255
}
static int r1_noise(int x, int y, int scale)   // interpolated lattice value, 0..255
{
    int gx = x / scale, gy = y / scale, fx = x % scale, fy = y % scale;
    int a = r1_hash(gx, gy),     b = r1_hash(gx + 1, gy);
    int c = r1_hash(gx, gy + 1), d = r1_hash(gx + 1, gy + 1);
    int top = a + (b - a) * fx / scale;
    int bot = c + (d - c) * fx / scale;
    return top + (bot - top) * fy / scale;
}
static unsigned char r1_hmap[R1_MAP * R1_MAP];

// Build a real OpenTTD world: a rolling-hills landscape with several growing towns.
// After this, GetTileType() over the map returns real MP_CLEAR/MP_ROAD/MP_HOUSE with
// real per-tile heights, which the game's own viewport renders (slopes, foundations).
extern "C" void r1_build_world(void)
{
    _game_mode = GM_NORMAL;
    _settings_game.economy.allow_town_roads = true;
    _settings_game.construction.build_on_slopes = true;
    _settings_game.game_creation.ending_year = 0;
    // Zeroed-deadpool settings default town_growth_rate to 0, which makes
    // UpdateTownGrowth early-return before setting TOWN_IS_GROWING (freezes towns).
    _settings_game.economy.town_growth_rate = 2;

    AllocateMap(R1_MAP, R1_MAP);

    // --- organic heightmap ---
    // 1) two octaves of value noise -> raw rolling terrain (0..MAXH).
    const int MAXH = 8;
    for (uint y = 0; y < MapMaxY(); y++)
        for (uint x = 0; x < MapMaxX(); x++) {
            int n = r1_noise(x, y, 16) * 2 / 3 + r1_noise(x, y, 6) / 3;   // 0..255
            r1_hmap[y * R1_MAP + x] = (unsigned char)(n * MAXH / 255);
        }
    // 2) flatten a small pad at each town centre so it seeds on level ground.
    for (uint i = 0; i < lengthof(R1_TOWNS); i++) {
        int cx = (int)R1_TOWNS[i].x, cy = (int)R1_TOWNS[i].y;
        unsigned char hc = r1_hmap[cy * R1_MAP + cx];
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++) {
                int x = cx + dx, y = cy + dy;
                if (x >= 0 && x < (int)MapMaxX() && y >= 0 && y < (int)MapMaxY())
                    r1_hmap[y * R1_MAP + x] = hc;
            }
    }
    // 2a2) flatten each industry pad to height 0 so its footprint is level.
    for (uint i = 0; i < lengthof(R1_INDUSTRIES); i++) {
        const R1IndSite &s = R1_INDUSTRIES[i];
        for (int dy = -1; dy <= s.h; dy++)
            for (int dx = -1; dx <= s.w; dx++) {
                int x = s.bx + dx, y = s.by + dy;
                if (x >= 0 && x < (int)MapMaxX() && y >= 0 && y < (int)MapMaxY())
                    r1_hmap[y * R1_MAP + x] = 0;
            }
    }
    // 2b) carve the lake basin + the river channel to sea level (before relaxation so
    //     their shores ramp down gently instead of forming cliffs).
    for (uint y = 0; y < MapMaxY(); y++)
        for (uint x = 0; x < MapMaxX(); x++)
            if (r1_in_lake((int)x, (int)y)) r1_hmap[y * R1_MAP + x] = 0;
    for (int y = 16; y <= 56; y++) {
        int rx = r1_river_x(y);
        if (rx >= 1 && rx < (int)MapMaxX()) r1_hmap[y * R1_MAP + rx] = 0;
    }
    // 3) relaxation: no tile more than 1 step above its lowest orthogonal neighbour,
    //    so every slope is gentle (single-step) -> clean slope sprites + buildable,
    //    and the pad edges smooth into the hills instead of forming cliffs.
    for (int pass = 0; pass < 8; pass++) {
        bool changed = false;
        for (int y = 0; y < (int)MapMaxY(); y++)
            for (int x = 0; x < (int)MapMaxX(); x++) {
                int mn = MAXH;
                if (x > 0)                  mn = std::min(mn, (int)r1_hmap[y * R1_MAP + x - 1]);
                if (x < (int)MapMaxX() - 1) mn = std::min(mn, (int)r1_hmap[y * R1_MAP + x + 1]);
                if (y > 0)                  mn = std::min(mn, (int)r1_hmap[(y - 1) * R1_MAP + x]);
                if (y < (int)MapMaxY() - 1) mn = std::min(mn, (int)r1_hmap[(y + 1) * R1_MAP + x]);
                if ((int)r1_hmap[y * R1_MAP + x] > mn + 1) { r1_hmap[y * R1_MAP + x] = (unsigned char)(mn + 1); changed = true; }
            }
        if (!changed) break;
    }
    // 4) apply to the real map, with varied ground: rocky peaks up high, rough scrub
    //    on some mid-slopes, grass elsewhere (the real DrawTile_Clear draws each).
    for (uint y = 0; y < MapMaxY(); y++)
        for (uint x = 0; x < MapMaxX(); x++) {
            TileIndex t = TileXY(x, y);
            int h = r1_hmap[y * R1_MAP + x];
            ClearGround g = CLEAR_GRASS;
            if (h >= 6) g = CLEAR_ROCKS;                                    // rocky mountaintops (few)
            else if (h >= 4 && r1_noise((int)x + 13, (int)y + 7, 5) > 232) g = CLEAR_ROUGH; // rare scrub (rough draws costly detail sprites)
            MakeClear(t, g, 3);
            SetTileHeight(t, (uint)h);
        }
    // 5) fill the lake with sea (its tiles are height 0 from the carve). Our minimal
    //    water proc (m1_water_draw.cpp) draws these MP_WATER tiles as flat water.
    uint nwater = 0;
    for (uint y = 1; y < MapMaxY(); y++)
        for (uint x = 1; x < MapMaxX(); x++)
            if (r1_in_lake((int)x, (int)y) && r1_hmap[y * R1_MAP + x] == 0) { MakeSea(TileXY(x, y)); nwater++; }
    // the river: MakeRiver on the height-0 channel tiles that are still bare grass.
    for (int y = 16; y <= 56; y++) {
        int rx = r1_river_x(y);
        if (rx < 1 || rx >= (int)MapMaxX()) continue;
        TileIndex t = TileXY((uint)rx, (uint)y);
        if (IsTileType(t, MP_CLEAR) && r1_hmap[y * R1_MAP + rx] == 0) { MakeRiver(t, (uint8)r1_hash(rx, y)); nwater++; }
    }
    for (uint x = 0; x < MapSizeX(); x++) MakeVoid(TileXY(x, MapMaxY()));
    for (uint y = 0; y < MapSizeY(); y++) MakeVoid(TileXY(MapMaxX(), y));
    ottd_log("R1: map %ux%u, noise hills (max h=%d), lake=%u tiles", R1_MAP, R1_MAP, MAXH, nwater);

    SetDate(ConvertYMDToDate(1950, 0, 1), 0);
    ResetHouses();

    // Found each town, seed a small village, and enable deterministic live growth
    // (build-7 CmdTownGrowthRate sets TOWN_CUSTOM_GROWTH so it keeps growing). r1_tick
    // runs OnTick_Town, which iterates ALL towns — so they all grow live together.
    _current_company = OWNER_DEITY;
    uint founded = 0;
    for (uint i = 0; i < lengthof(R1_TOWNS); i++) {
        Town *t = r1_found_town(R1_TOWNS[i].x, R1_TOWNS[i].y);
        if (t == nullptr) break;
        _generating_world = true;
        for (int g = 0; g < 2; g++) Command<CMD_EXPAND_TOWN>::Do(DC_EXEC, (TownID)t->index, (uint32)0);
        _generating_world = false;
        Command<CMD_TOWN_GROWTH_RATE>::Do(DC_EXEC, (TownID)t->index, (uint16)80);  // slow: rare full-screen repaints -> no stutter (towns already sizeable from the seed)
        founded++;
    }
    _current_company = OWNER_NONE;
    g_live = true;   // the render loop now ticks the engine each frame (r1_tick)
    g_full_dirty = true;  // force the first full frame to draw
    r1_trace_route();// trace the bus route along the roads the towns just built

    // Place the industries on their flat pads (bare grass only). index=0 is a dummy —
    // our minimal proc draws from gfx alone and never touches the Industry pool.
    uint nind = 0;
    for (uint i = 0; i < lengthof(R1_INDUSTRIES); i++) {
        const R1IndSite &s = R1_INDUSTRIES[i];
        for (int j = 0; j < s.n; j++) {
            int x = s.bx + s.tiles[j].dx, y = s.by + s.tiles[j].dy;
            if (x < 1 || x >= (int)MapMaxX() || y < 1 || y >= (int)MapMaxY()) continue;
            TileIndex t = TileXY((uint)x, (uint)y);
            if (IsTileType(t, MP_CLEAR) && r1_hmap[y * R1_MAP + x] == 0) {
                MakeIndustry(t, (IndustryID)0, (IndustryGfx)s.tiles[j].gfx,
                             (uint8)r1_hash(x, y), WATER_CLASS_INVALID);
                nind++;
            }
        }
    }

    // Scatter forests across the wilderness (bare grass, away from town centres so
    // growth isn't boxed in). A coarse noise field makes trees CLUMP into woods; the
    // real DrawTile_Trees renders them. Real MP_TREES tiles, real temperate species.
    uint ntrees = 0;
    for (uint y = 1; y < MapMaxY(); y++)
        for (uint x = 1; x < MapMaxX(); x++) {
            TileIndex t = TileXY(x, y);
            if (!IsTileType(t, MP_CLEAR)) continue;          // only bare grass
            bool near_town = false;
            for (uint i = 0; i < lengthof(R1_TOWNS); i++) {
                int dx = (int)x - (int)R1_TOWNS[i].x; if (dx < 0) dx = -dx;
                int dy = (int)y - (int)R1_TOWNS[i].y; if (dy < 0) dy = -dy;
                if ((dx > dy ? dx : dy) < 6) { near_town = true; break; }
            }
            if (near_town) continue;
            if (r1_noise(x, y, 6) < 190) continue;           // fewer "wooded" regions (cheaper full repaint)
            unsigned r = (unsigned)r1_hash((int)x, (int)y);
            if ((r & 1) == 0) continue;                      // ~50% gaps inside a wood
            MakeTree(t, (TreeType)(r % 12), (r >> 4) & 3, 3, TREE_GROUND_GRASS, 3);
            ntrees++;
        }

    uint nh = 0, nr = 0;
    for (TileIndex tt = 0; tt < MapSize(); tt++) { if (IsTileType(tt, MP_HOUSE)) nh++; else if (IsTileType(tt, MP_ROAD)) nr++; }
    ottd_log("R1: %u towns seeded, mp_house=%u mp_road=%u trees=%u (LIVE growth on)", founded, nh, nr, ntrees);
}

// Advance the real engine one game tick. Called from GameLoop() (b2_shims.cpp,
// R1_MERGE) at game cadence. IncreaseDate() runs the genuine calendar + monthly
// town loops; OnTick_Town() runs TownTickHandler -> GrowTown for the town when its
// grow_counter expires. Wrapped in _generating_world=true so each new house
// completes and gains population immediately (build-10 path) — no tile loop needed.
// ===================== moving bus (minimal road vehicle) =====================
// One road vehicle driving a route traced along the town's REAL roads. No vehicle
// pool / engine subsystem — just a position + a direction sprite (_roadveh_images[0]
// = 0xCD4, + Direction), drawn by our real ViewportAddVehicles (un-stubbed) via
// AddSortableSpriteToDraw and advanced each tick. Ping-pongs the route at its ends.
static TileIndex g_route[64];
static int g_route_len = 0, g_bus_i = 0, g_bus_prog = 0, g_bus_dir = 1;

static void r1_trace_route(void)
{
    g_route_len = 0; g_bus_i = 0; g_bus_prog = 0; g_bus_dir = 1;
    TileIndex start = INVALID_TILE;
    for (int r = 0; r <= 12 && start == INVALID_TILE; r++)
        for (int dy = -r; dy <= r && start == INVALID_TILE; dy++)
            for (int dx = -r; dx <= r; dx++) {
                int x = 32 + dx, y = 32 + dy;
                if (x < 1 || y < 1 || x >= (int)MapMaxX() || y >= (int)MapMaxY()) continue;
                TileIndex t = TileXY((uint)x, (uint)y);
                if (IsTileType(t, MP_ROAD)) { start = t; break; }
            }
    if (start == INVALID_TILE) return;
    g_route[g_route_len++] = start;
    TileIndex cur = start;
    DiagDirection came = INVALID_DIAGDIR;
    while (g_route_len < 64) {
        RoadBits rb = GetAnyRoadBits(cur, RTT_ROAD, false);
        DiagDirection best = INVALID_DIAGDIR;
        for (DiagDirection d = DIAGDIR_BEGIN; d < DIAGDIR_END; d = (DiagDirection)(d + 1)) {
            if (d == came || !(rb & DiagDirToRoadBits(d))) continue;
            TileIndex nb = TileAddByDiagDir(cur, d);
            if (nb >= MapSize() || !IsTileType(nb, MP_ROAD)) continue;
            best = d; break;
        }
        if (best == INVALID_DIAGDIR) break;   // dead end -> the bus will ping-pong back
        cur = TileAddByDiagDir(cur, best);
        if (cur == start) break;              // closed loop
        g_route[g_route_len++] = cur;
        came = ReverseDiagDir(best);
    }
    ottd_log("R1: bus route len=%d", g_route_len);
}

// Advance the bus; returns true only on a tick where it actually moved a sub-pixel,
// so the caller redraws only then. The game loop ticks fast, so move 1 sub-pixel every
// 4th tick -> a calm bus (~a tile every ~64 ticks) instead of the turbo dash.
static bool r1_bus_move(void)
{
    if (g_route_len < 2) return false;
    // R1-40: advance one sub-step every call (r1_tick calls this every tick → 16
    // ticks/tile). The whole-screen redraw cadence in r1_tick shows the motion.
    if (++g_bus_prog >= 16) {
        g_bus_prog = 0;
        g_bus_i += g_bus_dir;
        if (g_bus_i >= g_route_len - 1) { g_bus_i = g_route_len - 1; g_bus_dir = -1; }
        else if (g_bus_i <= 0)          { g_bus_i = 0;              g_bus_dir = 1; }
    }
    return true;
}

// Real ViewportAddVehicles (we removed the no-op stub): draw the bus at its
// interpolated world position, facing its travel direction. Called inside the real
// ViewportDoDraw, so AddSortableSpriteToDraw sorts it correctly over the road.
void ViewportAddVehicles(DrawPixelInfo *)
{
    if (g_route_len < 2) return;
    int ni = g_bus_i + g_bus_dir;
    if (ni < 0 || ni >= g_route_len) return;
    TileIndex a = g_route[g_bus_i], b = g_route[ni];
    int ax = (int)TileX(a), ay = (int)TileY(a);
    int dx = (int)TileX(b) - ax, dy = (int)TileY(b) - ay;
    int wx = ax * TILE_SIZE + TILE_SIZE / 2 + dx * g_bus_prog;
    int wy = ay * TILE_SIZE + TILE_SIZE / 2 + dy * g_bus_prog;
    int wz = (int)TileHeight(a) * TILE_HEIGHT;
    Direction dir = (dx > 0) ? DIR_SW : (dx < 0) ? DIR_NE : (dy > 0) ? DIR_SE : DIR_NW;
    AddSortableSpriteToDraw((SpriteID)(0xCD4 + dir), PAL_NONE, wx, wy, 4, 4, 6, wz, false, 0, 0, 0);
}

// Screen-centre of the bus for a given viewport (false if no route).
static int g_pbx0 = 0, g_pby0 = 0, g_pbx1 = 0, g_pby1 = 0;  // previous bus screen rect
static bool r1_bus_screen(const Viewport *vp, int *sxp, int *syp)
{
    if (g_route_len < 2) return false;
    int ni = g_bus_i + g_bus_dir; if (ni < 0 || ni >= g_route_len) return false;
    TileIndex a = g_route[g_bus_i], b = g_route[ni];
    int ax = (int)TileX(a), ay = (int)TileY(a);
    int dx = (int)TileX(b) - ax, dy = (int)TileY(b) - ay;
    int wx = ax * TILE_SIZE + TILE_SIZE / 2 + dx * g_bus_prog;
    int wy = ay * TILE_SIZE + TILE_SIZE / 2 + dy * g_bus_prog;
    int wz = (int)TileHeight(a) * TILE_HEIGHT;
    Point p = RemapCoords(wx, wy, wz);
    int z = (int)g_zoom;
    *sxp = (int)((p.x - vp->virtual_left) >> z) + vp->left;
    *syp = (int)((p.y - vp->virtual_top)  >> z) + vp->top;
    return true;
}

// PARTIAL redraw: repaint only the screen region around the bus (old + new position)
// via a sub-rectangle ViewportDoDraw — cheap, so the bus animates without repainting
// the whole map. Returns the dirty screen rect (for a small MakeDirty).
extern "C" int r1_bus_draw(int scroll_x, int scroll_y, int *ox, int *oy, int *ow, int *oh)
{
    *ow = 0;
    Viewport vp; r1_make_viewport(scroll_x, scroll_y, &vp);
    int sxp, syp;
    if (!r1_bus_screen(&vp, &sxp, &syp)) return 0;
    const int M = 24;
    int x0 = sxp - M, y0 = syp - M, x1 = sxp + M, y1 = syp + M;
    if (g_pbx1 > g_pbx0) {   // union with the previous rect so the old sprite is erased
        if (g_pbx0 < x0) x0 = g_pbx0;
        if (g_pby0 < y0) y0 = g_pby0;
        if (g_pbx1 > x1) x1 = g_pbx1;
        if (g_pby1 > y1) y1 = g_pby1;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > _screen.width)  x1 = _screen.width;
    if (y1 > _screen.height) y1 = _screen.height;
    g_pbx0 = sxp - M; g_pby0 = syp - M; g_pbx1 = sxp + M; g_pby1 = syp + M;
    if (x1 <= x0 || y1 <= y0) return 0;

    DrawPixelInfo screen_dpi;
    screen_dpi.dst_ptr = _screen.dst_ptr; screen_dpi.pitch = _screen.pitch;
    screen_dpi.left = 0; screen_dpi.top = 0;
    screen_dpi.width = _screen.width; screen_dpi.height = _screen.height;
    screen_dpi.zoom = ZOOM_LVL_NORMAL;
    _cur_dpi = &screen_dpi;
    int z = (int)g_zoom;
    ViewportDoDraw(&vp,
        ((x0 - vp.left) << z) + vp.virtual_left, ((y0 - vp.top) << z) + vp.virtual_top,
        ((x1 - vp.left) << z) + vp.virtual_left, ((y1 - vp.top) << z) + vp.virtual_top);
    _cur_dpi = nullptr;

    *ox = x0; *oy = y0; *ow = x1 - x0; *oh = y1 - y0;
    return 1;
}

extern "C" void r1_tick(void)
{
    if (!g_live) return;

    // R1-30: one-time marker — if this prints, the window came up and the crash is
    // NOT in the ctor (i.e. the missing "main window up" was a dropped UDP packet).
    static bool first = true;
    if (first) { first = false; ottd_log("R1: first tick (window is up, engine running)"); }

    // Toolbar PAUSE: freeze the simulation (no date advance, no growth, no bus). The
    // real window system keeps running independently, so the toolbar stays clickable
    // and the map stays on screen — clicking PAUSE again resumes.
    if (g_r1_paused) return;

    // Toolbar FAST-FORWARD: run the sim body several times per frame so the town grows
    // and the bus drives visibly faster. One sub-step normally.
    int steps = g_r1_fast ? 3 : 1;
    static unsigned exp = 0;
    for (int s = 0; s < steps; s++) {
        IncreaseDate();

        bool save_gw = _generating_world;
        _generating_world = true;   // houses complete instantly + gain population
        OnTick_Town();
        _generating_world = save_gw;

        // R1-39: OnTick_Town's internal GrowTown silently froze (houses stuck at 12 since
        // the window-system merge). Drive growth the PROVEN way instead: periodic
        // CMD_EXPAND_TOWN (the exact command that seeds the towns in r1_build_world),
        // rotating one town per cycle so the map fills in gradually and live. Needs a real
        // _current_company (OWNER_DEITY) + _generating_world, exactly like the seed.
        if ((++exp % 40) == 0) {
            CompanyID save_co = _current_company;
            _current_company = OWNER_DEITY;
            bool sg = _generating_world; _generating_world = true;
            TownID tid = (TownID)(((exp / 40) - 1) % lengthof(R1_TOWNS));
            Town *t = Town::GetIfValid(tid);
            if (t != nullptr) Command<CMD_EXPAND_TOWN>::Do(DC_EXEC, tid, (uint32)0);
            _generating_world = sg;
            _current_company = save_co;
        }

        r1_bus_move();
    }

    // Repaint on growth via ONE MarkWholeScreenDirty (safe: marking hundreds of tiles
    // per tick with MarkTileDirtyByTile overflowed the dirty-block bitmap -> Type 2).
    // The cost of each full repaint is cut by a sparser forest (see r1_build_world).
    static uint last_houses = 0xFFFFFFFFu;
    uint total = 0;
    for (const Town *tt : Town::Iterate()) total += tt->cache.num_houses;
    if (total != last_houses) { last_houses = total; MarkWholeScreenDirty(); }

    // The bus advances inside the sub-step loop above. Drive its animation off a steady
    // WHOLE-SCREEN redraw cadence. Every full redraw at a zoomed-out view is heavy (whole
    // map + trees + re-layout of every town-name label through the real string system), so
    // keep the cadence gentle — every 8th tick (~4/s). Growth still repaints immediately
    // via last_houses' MarkWholeScreenDirty above, so only the bus motion is throttled.
    static unsigned rd = 0;
    if ((++rd & 7) == 0) MarkWholeScreenDirty();

    // Throttled liveness log: prove on the sink that the clock ticks + town grows.
    // First call confirms the hook fires; then every ~128 ticks show the town
    // climbing (houses/pop) and the calendar advancing (date_fract/year).
    static unsigned n = 0;
    ++n;
    if (n == 1 || (n & 0x7F) == 0) {
        const Town *t0 = Town::GetIfValid(0);
        ottd_log("R1: live tick=%u date=%d houses=%u pop=%u | bus prog=%d i=%d dir=%d",
                 n, (int)_date,
                 t0 ? (uint)t0->cache.num_houses : 0u,
                 t0 ? (uint)t0->cache.population : 0u,
                 g_bus_prog, g_bus_i, g_bus_dir);
    }
}
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

// ================= Phase 1 Step 2: the REAL main window =================
// A minimal WC_MAIN_WINDOW = one full-screen viewport following the town, driven by
// the real window system. The real UpdateWindows -> DrawWindows -> the viewport draws
// the map with NATIVE dirty-blocks (a changed tile -> MarkTileDirtyByTile -> only that
// tile repaints). Modeled on main_gui.cpp's MainWindow, minus the cargo overlay.
static const NWidgetPart _r1_main_widgets[] = {
    NWidget(NWID_VIEWPORT, INVALID_COLOUR, 0), SetResize(1, 1),
};
static WindowDesc _r1_main_desc(
    WDP_MANUAL, nullptr, 0, 0,
    WC_MAIN_WINDOW, WC_NONE, 0,
    _r1_main_widgets, lengthof(_r1_main_widgets), nullptr);

struct R1MainWindow : Window {
    R1MainWindow(WindowDesc *desc) : Window(desc)
    {
        // BYPASS the NWidgetPart parser: dynamic_cast on a real object bus-errors on
        // this Retro68/XCOFF toolchain (RTTI broken), and MakeNWidget's WPT_RESIZE case
        // dynamic_casts. Build the one-viewport tree by hand + call SetResize directly.
        // GetWidget<> also dynamic_casts, so keep the nvp pointer instead of fetching it.
        NWidgetViewport *nvp = new NWidgetViewport(0);   // ctor calls SetIndex(0)
        nvp->SetResize(1, 1);
        NWidgetVertical *root = new NWidgetVertical();
        root->Add(nvp);
        this->nested_root = root;
        this->nested_array_size = 1;
        this->nested_array = CallocT<NWidgetBase *>(this->nested_array_size);
        this->nested_root->FillNestedArray(this->nested_array, this->nested_array_size);
        this->shade_select = nullptr;
        this->FinishInitNested(0);
        CLRBITS(this->flags, WF_WHITE_BORDER);
        ResizeWindow(this, _screen.width, _screen.height);
        nvp->InitializeViewport(this, TileXY(32, 32), ScaleZoomGUI(ZOOM_LVL_VIEWPORT));
        MarkWholeScreenDirty();   // force the first full redraw of the real window
    }
};

// ---------------------------------------------------------------------------
// R1-47: the real TOOLBAR. Unlike R1MainWindow (hand-built tree to dodge the
// dynamic_cast landmine), this window is built via the REAL InitNested() — the
// game's own NWidgetPart parser (MakeWindowNWidgetTree/MakeNWidget/MakeWidgetTree).
// That path is now safe: widget_type.h's RTTI-free As*() downcasts replace the
// dynamic_casts that used to Type-2. So this toolbar both SHOWS a real button bar
// AND proves the parser fix, unblocking every future real window/dialog.
// A slim horizontal strip of image buttons drawn by the real widget renderer.
static void r1_toggle_info_window();   // defined after R1InfoWindow below

enum R1ToolbarWidgets {
    R1TB_PAUSE = 0,
    R1TB_FASTFWD,
    R1TB_ZOOMIN,
    R1TB_ZOOMOUT,
    R1TB_SMALLMAP,
    R1TB_TOWN,
};

static const NWidgetPart _r1_toolbar_widgets[] = {
    NWidget(NWID_HORIZONTAL),
        NWidget(WWT_IMGBTN, COLOUR_GREY, R1TB_PAUSE),    SetDataTip(SPR_IMG_PAUSE, 0),       SetMinimalSize(22, 22),
        NWidget(WWT_IMGBTN, COLOUR_GREY, R1TB_FASTFWD),  SetDataTip(SPR_IMG_FASTFORWARD, 0), SetMinimalSize(22, 22),
        // Momentary buttons: WWT_PUSHIMGBTN so HandleButtonClick + the auto-raise timeout
        // (RaiseButtons only pops WWB_PUSHBUTTON types) gives the pressed-then-released feel.
        NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, R1TB_ZOOMIN),   SetDataTip(SPR_IMG_ZOOMIN, 0),   SetMinimalSize(22, 22),
        NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, R1TB_ZOOMOUT),  SetDataTip(SPR_IMG_ZOOMOUT, 0),  SetMinimalSize(22, 22),
        NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, R1TB_SMALLMAP), SetDataTip(SPR_IMG_SMALLMAP, 0), SetMinimalSize(22, 22),
        NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, R1TB_TOWN),     SetDataTip(SPR_IMG_TOWN, 0),     SetMinimalSize(22, 22),
    EndContainer(),
};

static WindowDesc _r1_toolbar_desc(
    WDP_MANUAL, nullptr, 0, 0,
    WC_MAIN_TOOLBAR, WC_NONE, 0,
    _r1_toolbar_widgets, lengthof(_r1_toolbar_widgets), nullptr);

// Local copy of main_gui.cpp's DoZoomInOutWindow (not linked in the R1 build — it
// lives in main_gui.o which we deliberately don't pull). Zooms the main viewport
// one step and keeps virtual dims / scroll consistent.
static void r1_zoom_main(bool in)
{
    Window *w = FindWindowById(WC_MAIN_WINDOW, 0);
    if (w == nullptr || w->viewport == nullptr) return;
    ViewportData *vp = w->viewport;   // scrollpos_* live on ViewportData, not Viewport
    if (in) {
        if (vp->zoom <= _settings_client.gui.zoom_min) return;
        vp->zoom = (ZoomLevel)((int)vp->zoom - 1);
        vp->virtual_width >>= 1;
        vp->virtual_height >>= 1;
        vp->scrollpos_x += vp->virtual_width >> 1;
        vp->scrollpos_y += vp->virtual_height >> 1;
    } else {
        if (vp->zoom >= _settings_client.gui.zoom_max) return;
        vp->zoom = (ZoomLevel)((int)vp->zoom + 1);
        vp->scrollpos_x -= vp->virtual_width >> 1;
        vp->scrollpos_y -= vp->virtual_height >> 1;
        vp->virtual_width <<= 1;
        vp->virtual_height <<= 1;
    }
    vp->dest_scrollpos_x = vp->scrollpos_x;
    vp->dest_scrollpos_y = vp->scrollpos_y;
    vp->virtual_left = vp->scrollpos_x;
    vp->virtual_top  = vp->scrollpos_y;
    MarkWholeScreenDirty();
}

// R1-59: a REAL dropdown menu off the toolbar's map button — proves the dropdown widget
// (widgets/dropdown.cpp) renders a clickable list of real StringID text via the language
// pack. Terminated by INVALID_STRING_ID. Selecting "Town directory" opens the info window.
static const StringID _r1_map_menu[] = {
    STR_MAP_MENU_MAP_OF_WORLD,     // 0: "Map of world"
    STR_MAP_MENU_EXTRA_VIEWPORT,   // 1: "Extra viewport"
    STR_MAP_MENU_SIGN_LIST,        // 2: "Sign list"
    STR_TOWN_MENU_TOWN_DIRECTORY,  // 3: "Town directory"
    INVALID_STRING_ID,
};

struct R1ToolbarWindow : Window {
    R1ToolbarWindow(WindowDesc *desc) : Window(desc)
    {
        // THE REAL PATH: InitNested runs the NWidgetPart parser (now RTTI-safe).
        this->InitNested(0);
        CLRBITS(this->flags, WF_WHITE_BORDER);
        ottd_log("R1: toolbar up (real InitNested parser OK) w=%d h=%d",
                 (int)this->width, (int)this->height);
    }

    void OnClick(Point pt, int widget, int click_count) override
    {
        ottd_log("R1: toolbar click widget=%d", widget);
        switch (widget) {
            case R1TB_PAUSE:
                g_r1_paused = !g_r1_paused;
                this->SetWidgetLoweredState(R1TB_PAUSE, g_r1_paused);
                this->SetWidgetDirty(R1TB_PAUSE);
                break;
            case R1TB_FASTFWD:
                g_r1_fast = !g_r1_fast;
                this->SetWidgetLoweredState(R1TB_FASTFWD, g_r1_fast);
                this->SetWidgetDirty(R1TB_FASTFWD);
                break;
            case R1TB_ZOOMIN:
                this->HandleButtonClick(R1TB_ZOOMIN);   // momentary pressed feedback
                r1_zoom_main(true);
                break;
            case R1TB_ZOOMOUT:
                this->HandleButtonClick(R1TB_ZOOMOUT);
                r1_zoom_main(false);
                break;
            case R1TB_TOWN:
                // Toggle the real Game Info window (draggable + closeable).
                this->HandleButtonClick(R1TB_TOWN);
                r1_toggle_info_window();
                break;
            case R1TB_SMALLMAP: {
                // Open a REAL dropdown menu (widgets/dropdown.cpp). ShowDropDownMenu forces
                // auto_width=false -> the menu is only as wide as the 22px button (text cut).
                // Build the list ourselves and call ShowDropDownList with auto_width=true so
                // it expands to the widest item ("Extra viewport").
                DropDownList list;
                for (uint i = 0; _r1_map_menu[i] != INVALID_STRING_ID; i++) {
                    list.emplace_back(new DropDownListStringItem(_r1_map_menu[i], (int)i, false));
                }
                ShowDropDownList(this, std::move(list), -1, R1TB_SMALLMAP, 0, true);
                break;
            }
        }
    }

    // A dropdown item was picked (index into _r1_map_menu). Only "Town directory" acts for
    // now (opens the info window); the rest just prove the menu text + click routing.
    void OnDropdownSelect(int widget, int index) override
    {
        ottd_log("R1: dropdown widget=%d index=%d", widget, index);
        if (widget == R1TB_SMALLMAP && index == 3) r1_toggle_info_window();
    }
};

// ---------------------------------------------------------------------------
// R1-50: a REAL draggable + closeable window, opened from the toolbar. Proves the
// full window system: a title bar (grab to drag — StartWindowDrag), a close box
// (WWT_CLOSEBOX -> Window::Close), a panel, and coexistence with the main viewport
// + toolbar (multiple real windows). Text is drawn with literal DrawString (the real
// layouter/font cache): StringID text is stubbed to empty in this build, so the
// caption title + panel stats are drawn by hand over the widgets.
enum R1InfoWidgets {
    R1IW_CLOSE = 0,
    R1IW_CAPTION,
    R1IW_PANEL,
};

static const NWidgetPart _r1_info_widgets[] = {
    NWidget(NWID_HORIZONTAL),
        NWidget(WWT_CLOSEBOX, COLOUR_GREY, R1IW_CLOSE),
        NWidget(WWT_CAPTION, COLOUR_GREY, R1IW_CAPTION), SetDataTip(STR_TOWN_DIRECTORY_CAPTION, 0),
    EndContainer(),
    NWidget(WWT_PANEL, COLOUR_GREY, R1IW_PANEL), SetMinimalSize(210, 66), EndContainer(),
};

static WindowDesc _r1_info_desc(
    WDP_CENTER, nullptr, 0, 0,
    WC_TOWN_DIRECTORY, WC_NONE, 0,
    _r1_info_widgets, lengthof(_r1_info_widgets), nullptr);

struct R1InfoWindow : Window {
    R1InfoWindow(WindowDesc *desc) : Window(desc)
    {
        this->InitNested(0);
    }

    void DrawWidget(const Rect &r, int widget) const override
    {
        if (widget != R1IW_PANEL) return;
        uint totpop = 0, ntowns = 0;
        for (const Town *tt : Town::Iterate()) { totpop += tt->cache.population; ntowns++; }
        char buf[64];
        int y = r.top + 5;
        snprintf(buf, sizeof buf, "Year:        %d", (int)_cur_year);
        DrawString(r.left + 8, r.right - 8, y, buf, TC_BLACK); y += 16;
        snprintf(buf, sizeof buf, "Towns:       %u", ntowns);
        DrawString(r.left + 8, r.right - 8, y, buf, TC_BLACK); y += 16;
        snprintf(buf, sizeof buf, "Population:   %u", totpop);
        DrawString(r.left + 8, r.right - 8, y, buf, TC_BLACK);
    }

    // R1-57: the caption now renders a REAL StringID (STR_TOWN_DIRECTORY_CAPTION ->
    // "{WHITE}Towns") via the loaded language pack — no more hand-drawn title. DrawWidgets
    // draws the caption through the real GetStringWithArgs path.
    void OnPaint() override
    {
        this->DrawWidgets();
    }

    // Redraw the live stats a few times a second so the numbers climb while open.
    void OnRealtimeTick(uint delta_ms) override
    {
        static uint acc = 0;
        acc += delta_ms;
        if (acc >= 400) { acc = 0; this->SetWidgetDirty(R1IW_PANEL); }
    }
};

// Open the Game Info window, or close it if already open (toolbar TOWN button toggle).
static void r1_toggle_info_window()
{
    Window *info = FindWindowById(WC_TOWN_DIRECTORY, 0);
    if (info != nullptr) info->Close();
    else new R1InfoWindow(&_r1_info_desc);
}

// Bring up the real window system + the main window (the map viewport). Diagnostic
// logs bracket each step so the sink pinpoints any first-run crash on PPC.
extern "C" void r1_start_window_system(void)
{
    // THE dirty-block fix: the video driver's Start() calls GameSizeChanged() but NOT
    // ScreenSizeChanged() — and only ScreenSizeChanged allocates _dirty_blocks and sets
    // _dirty_bytes_per_line. Without it _dirty_bytes_per_line==0 and _dirty_blocks==null,
    // so AddDirtyBlock (from MarkWholeScreenDirty) only ever marks the top 8px row (its
    // per-row stride is 0), and DrawDirtyBlocks blits just that strip — the map rendered
    // ONCE (via the window update-event full blit) then froze. Call it now that _screen
    // is 640x480 so the whole-screen dirty path actually repaints (live bus + growth).
    ScreenSizeChanged();
    ottd_log("R1: ScreenSizeChanged() done (dirty-blocks allocated for %dx%d)",
             (int)_screen.width, (int)_screen.height);

    // Zeroed _settings_client leaves gui.zoom_min == gui.zoom_max == 0, so
    // InitializeViewport's Clamp(zoom, min, max) forces vp->zoom to 0 (NORMAL) even
    // though it computes virtual_width/height from the requested zoom (OUT_4X). That
    // inconsistency (virtual sized for zoom 2, zoom field 0) drove the landscape draw
    // out of bounds -> Type 2 at the first real frame (R1-34). Give the clamp real
    // bounds so the viewport keeps OUT_4X and virtual/zoom stay consistent.
    _settings_client.gui.zoom_min = ZOOM_LVL_MIN;
    _settings_client.gui.zoom_max = ZOOM_LVL_MAX;

    ottd_log("R1: InitWindowSystem...");
    InitWindowSystem();
    for (uint i = 0; i != 16; i++) {   // colour ramps (SetupColoursAndInitialWindow)
        const byte *b = GetNonSprite(PALETTE_RECOLOUR_START + i, ST_RECOLOUR);
        if (b != nullptr) std::memcpy(_colour_gradient[i], b + 0xC6, sizeof(_colour_gradient[i]));
    }

    // R1-51: town-name signs. ViewportAddKdtreeSigns gates town labels on
    // HasBit(_display_opt, DO_SHOW_TOWN_NAMES) — our zeroed deadpool left every display
    // bit off, so no names drew. Enable the usual on-by-default set (names + signs +
    // palette animation + full detail).
    _display_opt = (1 << DO_SHOW_TOWN_NAMES) | (1 << DO_SHOW_STATION_NAMES) |
                   (1 << DO_SHOW_SIGNS) | (1 << DO_FULL_ANIMATION) | (1 << DO_FULL_DETAIL);

    // r1_found_town used `new Town` directly (not DoCreateTown), so the towns were never
    // entered into the viewport-sign kdtree and their sign width was never computed. Do it
    // now — the font cache + the real GetString town-name shim (b1_shims.cpp) are both
    // ready, so each town's sign gets a real name + geometry and the real viewport draw
    // (ViewportAddKdtreeSigns/ViewportDrawStrings) paints them.
    ottd_log("R1: UpdateAllTownVirtCoords (town-name signs)...");
    UpdateAllTownVirtCoords();
    // R1-62: stand up one real player company (Company::GetNumItems()>0 + _local_company
    // valid) so the canonical toolbar's build/station/finance/vehicle buttons enable and
    // construction has an owner. Must precede AllocateToolbar (the toolbar OnPaint reads it).
    r1_make_company();
    ottd_log("R1: player company up (GetNumItems + _local_company set)");

    ottd_log("R1: creating main window...");
    new R1MainWindow(&_r1_main_desc);
    ottd_log("R1: main window up (real viewport drives the map now)");

    // R1-61: the CANONICAL OpenTTD toolbar (toolbar_gui.cpp's MainToolbarWindow) —
    // replaces the R1-47 custom toolbar. AllocateToolbar() builds the full real button
    // bar with all the real dropdown menus. Most button handlers are no-op stubs (the
    // Show* sub-windows aren't compiled), but the bar renders + menus open with real text
    // + zoom works (real DoZoomInOutWindow). The old R1ToolbarWindow is now dead code.
    ottd_log("R1: creating canonical toolbar (AllocateToolbar)...");
    AllocateToolbar();
    ottd_log("R1: canonical toolbar up");
}

extern "C" int b2_scene_init(void)
{
    _sprite_cache_size = 2;
    init_cur_palette();

    Blitter *bl = BlitterFactory::SelectBlitter("8bpp-simple");
    if (bl == nullptr) { ottd_log("B2: SelectBlitter failed"); return -1; }
    GfxInitSpriteMem(); // CRITICAL before any GRF load (B1 lesson: NULL pool writes low memory)

    if (LoadGrfFileB2("ogfx1_base.grf", 0, false) == 0) { ottd_log("B2: grf load failed"); return -2; }

    // R1: build the real town into the real map (replaces B2's synthetic H[]).
    r1_build_world();

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

    // Pick the parent-sprite sorter for the real viewport (generic C++ sorter on
    // PPC; the SSE variants' checkers return false). MUST run before ViewportDoDraw
    // — _vp_sprite_sorter is null until this sets it, and ViewportDoDraw calls it.
    InitializeSpriteSorter();

    // R1-57: load the REAL language pack so captions / menus / town names render real
    // text through GetStringWithArgs (replacing the empty GetString stub). ReadLanguagePack
    // does a plain fopen(lang->file) + fread — no search paths — so english.lng just needs
    // to sit in the app's working dir. Its strrchr(PATHSEPCHAR) is sed-patched null-safe
    // (build.sh) so a bare filename works with the Mac fopen (HOpenDF opens by bare name).
    {
        static LanguageMetadata lmd;
        FILE *lf = std::fopen("english.lng", "rb");
        if (lf != nullptr) {
            size_t got = std::fread(&lmd, 1, sizeof(LanguagePackHeader), lf);
            std::fclose(lf);
            strecpy(lmd.file, "english.lng", lastof(lmd.file));
            bool ok = (got == sizeof(LanguagePackHeader)) && ReadLanguagePack(&lmd);
            ottd_log("R1: langpack %s (hdr bytes=%u)", ok ? "loaded" : "FAILED", (unsigned)got);
        } else {
            ottd_log("R1: english.lng NOT FOUND in app dir");
        }
    }

    // Bind the sprite font to the just-loaded base GRF. The SpriteFontCache static
    // ctors ran at startup with NO sprites loaded (empty glyph map); rebuild the
    // char->sprite map now that the font glyphs exist, so DrawString rasterises text.
    InitFontCache(false);
    InitializeUnicodeGlyphMap();

    // Phase 1 Step 2: the real window system + main window are stood up in b2_run,
    // AFTER SelectDriver runs the video driver's Start() (which sizes _screen to
    // 640x480). Creating the window here — before Start() — left _screen 0x0, so the
    // viewport came up 0x0 and painted noise (R1-33 diagnosis). r1_start_window_system
    // is now called from b2_run right after SelectDriver.

    ottd_log("B2: scene init ok");
    return 0;
}

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
            // The R1 real map is FLAT (base 0). The old synthetic H[] geometry no
            // longer matches what b2_scene_draw renders, so project flat here to keep
            // the pick aligned with the drawn tiles (else clicks land a tile off).
            int base = 0, slope = 0;
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

static void r1_make_viewport(int scroll_x, int scroll_y, Viewport *vp);   // defined with the draw code below

// CLICK-TO-BUILD on WORLD tile wt: build a road there via the REAL CMD_BUILD_ROAD
// (the same command GrowTown lays live) — the input->DoCommand->render loop the full
// GUI will reuse. Context-aware bits (the essence of the road tool): point a bit at
// each neighbouring road and give that neighbour the reciprocal bit, so junctions
// render as real straights/corners/T/crossroads. Only builds on empty grass.
static void r1_build_road_at(TileIndex wt)
{
    if (wt >= MapSize() || !IsTileType(wt, MP_CLEAR)) {
        ottd_log("R1: tap world(%u,%u) type=%d -> no build",
                 (uint)TileX(wt), (uint)TileY(wt), wt < MapSize() ? (int)GetTileType(wt) : -1);
        return;
    }
    _current_company = OWNER_DEITY;
    RoadBits bits = ROAD_NONE;
    for (DiagDirection d = DIAGDIR_BEGIN; d < DIAGDIR_END; d = (DiagDirection)(d + 1)) {
        TileIndex nb = TileAddByDiagDir(wt, d);
        if (nb >= MapSize() || !IsTileType(nb, MP_ROAD)) continue;
        bits |= DiagDirToRoadBits(d);
        Command<CMD_BUILD_ROAD>::Do(DC_EXEC | DC_AUTO, nb,
            DiagDirToRoadBits(ReverseDiagDir(d)), ROADTYPE_ROAD, DRD_NONE, (TownID)0);
    }
    if (bits == ROAD_NONE) bits = ROAD_X;
    CommandCost rc = Command<CMD_BUILD_ROAD>::Do(DC_EXEC | DC_AUTO, wt,
                        bits, ROADTYPE_ROAD, DRD_NONE, (TownID)0);
    _current_company = OWNER_NONE;
    ottd_log("R1: built road world(%u,%u) bits=%u ok=%d",
             (uint)TileX(wt), (uint)TileY(wt), (uint)bits, (int)rc.Succeeded());
}

// Pick the tile under a screen pixel using OpenTTD's REAL height-aware inverse
// projection (TranslateXYToTileCoord), so clicks land exactly on the drawn tile.
// Light it the real way (set _thd -> the viewport's DrawTileSelection draws the
// white rect), then build there.
extern "C" void b2_scene_pick(int screen_x, int screen_y, int scroll_x, int scroll_y)
{
    Viewport vp;
    r1_make_viewport(scroll_x, scroll_y, &vp);
    Point p = TranslateXYToTileCoord(&vp, screen_x, screen_y, true);
    if (p.x < 0) { ottd_log("B2: pick screen(%d,%d) -> off-map", screen_x, screen_y); return; }
    TileIndex wt = TileVirtXY((uint)p.x, (uint)p.y);   // world pixel coords -> TileIndex

    std::memset((void *)&_thd, 0, sizeof(_thd));
    _thd.drawstyle = HT_RECT;
    _thd.pos.x = TileX(wt) * TILE_SIZE; _thd.pos.y = TileY(wt) * TILE_SIZE;
    _thd.size.x = TILE_SIZE; _thd.size.y = TILE_SIZE;
    _thd.redsq = INVALID_TILE;

    ottd_log("B2: pick screen(%d,%d) -> world(%u,%u)", screen_x, screen_y, (uint)TileX(wt), (uint)TileY(wt));
    r1_build_road_at(wt);
}

// Cycle zoom one step while keeping the world point under (cx,cy) fixed on screen.
// Work in virtual coords: the virtual point under the cursor must stay under it after
// the zoom, which fixes the new pan offset. (No height needed — pure viewport math.)
extern "C" void b2_scene_zoom_at(int cx, int cy, int old_sx, int old_sy, int *new_sx, int *new_sy)
{
    Viewport vp;
    r1_make_viewport(old_sx, old_sy, &vp);
    int vx = ((cx - vp.left) << (int)g_zoom) + vp.virtual_left;   // virtual pt under cursor (old zoom)
    int vy = ((cy - vp.top)  << (int)g_zoom) + vp.virtual_top;

    int nz = (int)g_zoom + 1; if (nz > ZOOM_LVL_OUT_8X) nz = ZOOM_LVL_NORMAL;
    b2_scene_set_zoom(nz);

    int nvw = _screen.width << nz, nvh = _screen.height << nz;
    int nvl = vx - (cx << nz);   // new virtual_left keeping (vx,vy) under (cx,cy)
    int nvt = vy - (cy << nz);
    Point tc = RemapCoords(32 * TILE_SIZE, 32 * TILE_SIZE, 0);
    *new_sx = (nvl - tc.x + nvw / 2) >> nz;   // invert r1_make_viewport's virtual_left formula
    *new_sy = (nvt - tc.y + nvh / 2) >> nz;
}

// Draw the landscape into _screen shifted by (scroll_x, scroll_y) screen pixels.
// Draw the grown map through the GAME'S REAL VIEWPORT (viewport.cpp). This is the
// first genuine "OpenTTD's own renderer runs on OS 9" step: ViewportDoDraw iterates
// the visible tiles and calls each tile's real draw_tile_proc, which emit sprites
// into the viewport draw-lists that get sorted and blitted — instead of our bespoke
// per-tile loop. Centred on the town at world (32,32); scroll_* pan in screen px.
// Build the Viewport for a given pan offset. Draw AND pick call this, so screen<->
// world math is guaranteed identical (else clicks wouldn't land where they're drawn).
// Centred on the map centre (32,32) at height 0, panned by scroll_* screen pixels.
static void r1_make_viewport(int scroll_x, int scroll_y, Viewport *vp)
{
    vp->left = 0; vp->top = 0;
    vp->width = _screen.width; vp->height = _screen.height;
    vp->zoom = g_zoom;
    vp->virtual_width  = _screen.width  << (int)g_zoom;
    vp->virtual_height = _screen.height << (int)g_zoom;
    Point tc = RemapCoords(32 * TILE_SIZE, 32 * TILE_SIZE, 0);
    vp->virtual_left = tc.x - vp->virtual_width  / 2 + (scroll_x << (int)g_zoom);
    vp->virtual_top  = tc.y - vp->virtual_height / 2 + (scroll_y << (int)g_zoom);
    vp->overlay = nullptr;
}

static void r1_viewport_draw(int scroll_x, int scroll_y)
{
    // The "window" surface ViewportDoDraw draws into (it reads this as old_dpi:
    // dst_ptr/pitch/left/top). Full screen at NORMAL zoom.
    DrawPixelInfo screen_dpi;
    screen_dpi.dst_ptr = _screen.dst_ptr;
    screen_dpi.pitch   = _screen.pitch;
    screen_dpi.left = 0; screen_dpi.top = 0;
    screen_dpi.width = _screen.width; screen_dpi.height = _screen.height;
    screen_dpi.zoom = ZOOM_LVL_NORMAL;
    _cur_dpi = &screen_dpi;

    std::memset(_screen.dst_ptr, 0xC9, (size_t)_screen.pitch * _screen.height); // sky under off-map

    Viewport vp;
    r1_make_viewport(scroll_x, scroll_y, &vp);

    // Repeat the proof-of-life log (one-shot packets drop on the lossy UDP sink):
    // if these appear, the REAL ViewportDoDraw is what rendered the frame.
    static unsigned vpn = 0;
    if ((++vpn & 0xFF) == 1)
        ottd_log("R1: REAL viewport draw #%u vl=%d vt=%d vw=%d vh=%d zoom=%d",
                 vpn, vp.virtual_left, vp.virtual_top, vp.virtual_width, vp.virtual_height, (int)vp.zoom);

    ViewportDoDraw(&vp, vp.virtual_left, vp.virtual_top,
                   vp.virtual_left + vp.virtual_width, vp.virtual_top + vp.virtual_height);

    // HUD: draw the live year + town/population totals as REAL TEXT via DrawString
    // (the real gfx_layout layouter + sprite font cache). _cur_dpi is a full-screen
    // NORMAL-zoom surface so glyphs blit at 1:1.
    screen_dpi.zoom = ZOOM_LVL_NORMAL;
    _cur_dpi = &screen_dpi;
    {
        uint totpop = 0, ntowns = 0;
        for (const Town *tt : Town::Iterate()) { totpop += tt->cache.population; ntowns++; }
        char buf[96];
        snprintf(buf, sizeof buf, "OpenTTD  -  Year %d   Towns %u   Population %u",
                 (int)_cur_year, ntowns, totpop);
        DrawString(8, _screen.width - 8, 6, buf, TC_WHITE);
    }
    _cur_dpi = nullptr;
}

// true = the real game viewport (r1_viewport_draw); false = the bespoke fallback
// loop below (r1_manual_draw). Kept switchable so a HW render issue can fall back
// in one line + rebuild while we tune the real path.
static bool g_real_vp = true;

extern "C" void b2_scene_draw(int scroll_x, int scroll_y)
{
    if (g_real_vp) { r1_viewport_draw(scroll_x, scroll_y); return; }
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
    // R1: draw the REAL map. Scene tile (tx,ty) maps to world tile (tx+OFF,ty+OFF)
    // so the 19x19 window is centred on the town at world (32,32) -> scene (9,9).
    // Terrain is flat (base 0): grass ground everywhere, a road overlay on MP_ROAD
    // and a townhouse building on MP_HOUSE. Painter's order = ascending tx+ty.
    const int OFF = (int)(R1_MAP / 2) - (N - 1) / 2;   // 32 - 9 = 23
    for (int key = 0; key <= 2 * (N - 2); key++) {
        for (int ty = 0; ty < N - 1; ty++) {
            for (int tx = 0; tx < N - 1; tx++) {
                if (tx + ty != key) continue;
                TileIndex wt = TileXY((uint)(tx + OFF), (uint)(ty + OFF));
                int type = (int)GetTileType(wt);
                Point pt = RemapCoords(tx * TILE_SIZE, ty * TILE_SIZE, 0);
                if (type == MP_HOUSE) {
                    // Each house's REAL sprite by type/stage/hash, mirroring
                    // DrawTile_Town (town_cmd.cpp): index _town_draw_tile_data by
                    // house_id<<4 | hash<<2 | building_stage. Ground (often bare
                    // land under the building) then the building sprite itself.
                    HouseID hid = GetHouseType(wt);
                    uint stage = GetHouseBuildingStage(wt);
                    uint hash = TileHash2Bit(TileX(wt) * TILE_SIZE, TileY(wt) * TILE_SIZE);
                    const DrawBuildingsTileStruct *d =
                        &_town_draw_tile_data[(hid << 4) | (hash << 2) | stage];
                    DrawSpriteViewport(d->ground.sprite, d->ground.pal, pt.x, pt.y, nullptr);
                    if (d->building.sprite != 0)
                        DrawSpriteViewport(d->building.sprite, d->building.pal, pt.x, pt.y, nullptr);
                } else {
                    // grass ground under everything else
                    DrawSpriteViewport(SPR_FLAT_GRASS_TILE, PAL_NONE, pt.x, pt.y, nullptr);
                    if (type == MP_ROAD) {
                        // Real road sprite from this tile's RoadBits (connected
                        // straights/corners/junctions). On flat ground the sprite
                        // is SPR_ROAD_Y + offsets[bits] (GetRoadSpriteOffset).
                        static const uint roff[16] = {0,18,17,7,16,0,10,5,15,8,1,4,9,3,6,2};
                        RoadBits rb = GetAnyRoadBits(wt, RTT_ROAD, false);
                        DrawSpriteViewport(SPR_ROAD_Y + roff[rb & 15], PAL_NONE, pt.x, pt.y, nullptr);
                    }
                }
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
