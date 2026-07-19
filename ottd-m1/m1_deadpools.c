/* M1: raw zeroed storage for the Vehicle / LinkGraph pools that date.cpp's
 * OnNewYear references ONLY inside the MAX_YEAR calendar-wrap dead branch
 * (Vehicle::Iterate() / LinkGraph::Iterate()). That branch never executes for a
 * 1950s run, so all-zero storage (items==0, first_unused==0) is safe. Kept in a
 * header-free C file so the char-array definitions don't collide with the real
 * `extern VehiclePool _vehicle_pool;` declarations in the OpenTTD headers.
 * Sized generously (> sizeof any Pool<> instance). */
char _vehicle_pool[4096]    = {0};
char _link_graph_pool[4096] = {0};

/* M1 build 6: town_cmd.cpp is compiled for real (the town monthly/yearly
 * simulation now runs). --gc-sections is a no-op in this Retro68/XCOFF flow,
 * so the ENTIRE town_cmd.o is pulled, dragging its command/road/house/station
 * references. These globals are all off the monthly/yearly runtime path
 * (proven: UpdateTownAmounts/Growth/Rating/Unwanted + UpdateTownRadius only
 * iterate EMPTY pools/lists and do math), so all-zero raw storage is safe:
 * Company::Iterate()/Station::Iterate() over a zeroed Pool yield nothing.
 * Global-scope symbols are unmangled, so C char-array defs match the C++
 * `extern <Type> _sym;` declarations without pulling those types. Generously
 * sized (>= sizeof the real type). Function stubs live in m1_town_stubs.cpp. */
/* _company_pool is now the REAL CompanyPool, defined in m1_company.cpp (R1-62). */
char _station_pool[8192]         = {0};
char _industry_pool[8192]        = {0};
char _object_pool[8192]          = {0};
char _depot_pool[8192]           = {0};
char _station_kdtree[4096]       = {0};
#ifndef R1_MERGE  /* real viewport.o defines _viewport_sign_kdtree in the render-merge build */
char _viewport_sign_kdtree[4096] = {0};
#endif
char _economy[4096]              = {0};
char _price[2048]                = {0};
char _cheats[512]                = {0};
char _house_mngr[4096]           = {0};
#ifndef R1_STRINGS  /* real strings.cpp owns the StringParameters _global_string_params */
char _global_string_params[4096] = {0};
#endif

/* R1-57: extra zeroed storage the real strings.cpp references. All off the caption /
 * menu render path (Engine/Group pools only via Get*IfValid over an empty pool ->
 * nullptr; currency/cargo/searchpaths only via currency/cargo/searchpath control codes
 * we never emit). Global-scope symbols are unmangled so these C defs bind the C++
 * `extern <Type> _sym;` declarations. Generously sized. */
#ifdef R1_STRINGS
char _engine_pool[8192]        = {0};
char _group_pool[8192]         = {0};
char _currency_specs[8192]     = {0};   /* CurrencySpec[CURRENCY_END] */
char _sorted_cargo_specs[64]   = {0};   /* std::vector (zeroed == empty) */
char _valid_searchpaths[64]    = {0};   /* std::vector (zeroed == empty) */
const char _openttd_revision[] = "13.4";
#endif
char _roadtypes[65536]           = {0};   /* RoadTypeInfo[ROADTYPE_END] */
char _roadtypes_type[64]         = {0};
char _transparency_opt[64]       = {0};
char _invisibility_opt[64]       = {0};
char _current_company[16]        = {0};   /* CompanyID (byte); build 7 sets this to OWNER_DEITY before CmdTownGrowthRate */
char _local_company[16]          = {0};
char _generating_world[16]       = {0};   /* bool; false */

/* M1 build 8: landscape.cpp defines the _tile_type_procs[] master dispatch
 * table, which takes the address of every tile type's TileTypeProcs struct.
 * town & clear procs are REAL (town_cmd.o / clear_cmd.o); the other 9 are only
 * ADDRESS-referenced by the table and NEVER dereferenced when we clear an
 * MP_CLEAR tile, so raw-zeroed storage (all-null procs) is safe. Sized > any
 * real TileTypeProcs. */
char _tile_type_rail_procs[128]         = {0};
char _tile_type_road_procs[128]         = {0};
char _tile_type_station_procs[128]      = {0};
char _tile_type_water_procs[128]        = {0};
char _tile_type_void_procs[128]         = {0};
char _tile_type_industry_procs[128]     = {0};
char _tile_type_tunnelbridge_procs[128] = {0};
char _tile_type_object_procs[128]       = {0};
char _tile_type_trees_procs[128]        = {0};

/* Globals referenced by clear_cmd.cpp / landscape.cpp, all off the
 * clear-one-grass-tile runtime path (draw/sound/GRF/network/water state). Raw
 * zeroed storage; a zeroed std::vector (_cleared_object_areas) is a valid EMPTY
 * vector. _networking/_pause_mode come from m1_shims.cpp. */
char _cleared_object_areas[256]  = {0};   /* std::vector<ClearedObjectArea>: empty */
char _misc_grf_features[16]      = {0};
#ifndef R1_MERGE  /* real gfx.o owns _shift_pressed in the render-merge build */
char _shift_pressed[16]          = {0};   /* bool false -> commands exec, not estimate */
#endif
char _temp_store[4096]           = {0};   /* NewGRF PersistentStorageArray */
char _debug_desync_level[16]     = {0};
#ifndef R1_MERGE  /* real b1_shims.o owns _slope_to_sprite_offset in the render-merge build */
char _slope_to_sprite_offset[64] = {0};   /* const byte[32] draw table, never drawn */
#endif

/* M1 build 8: landscape.cpp's map-gen dead branch references the savegame file
 * descriptor. Never touched on the single-tile-clear path; raw zeroed storage
 * (> sizeof FileToSaveLoad) is safe. */
char _file_to_saveload[1024]     = {0};   /* FileToSaveLoad */

/* M1 build 9: road_cmd.cpp draws/queries road & rail type tables, company
 * colours, viewport DPI and zoom. All off the CmdBuildRoad exec path (drawing /
 * GRF-feature checks never run headless building one grass-tile road piece);
 * raw zeroed storage is safe. Sized >= sizeof the real type. */
char _company_colours[64]          = {0};   /* Colours[MAX_COMPANIES] (byte enum) */
#ifndef R1_MERGE  /* real gfx.o owns _cur_dpi/_gui_zoom in the render-merge build */
char _cur_dpi[16]                  = {0};   /* DrawPixelInfo * (null) */
#endif
char _display_opt[16]              = {0};   /* byte */
#ifndef R1_MERGE
char _gui_zoom[16]                 = {0};   /* ZoomLevel (int) */
#endif
char _loaded_newgrf_features[256]  = {0};   /* GRFLoadedFeatures */
char _railtypes[131072]            = {0};   /* RailtypeInfo[RAILTYPE_END] */

/* R1 render-merge: the real viewport.cpp (game renderer) is compiled. It
 * references these globals, all OFF the town-render draw path (sign/kdtree
 * iteration over empty pools, mouse/debug/network state, GUI hit-testing). Raw
 * zeroed storage is safe: an empty _sign_pool yields no signs to draw; a zeroed
 * kdtree has zero items; the mouse/debug/network scalars are read-only defaults.
 * Function stubs live in m1_viewport_stubs.cpp. Sized >= sizeof the real type. */
char _sign_pool[8192]                    = {0};   /* SignPool: empty (Sign::Iterate yields nothing) */
char _town_local_authority_kdtree[4096]  = {0};   /* TownKdtree: zero items */
char _special_mouse_mode[16]             = {0};   /* SpecialMouseMode (WSM_NONE) */
char _debug_misc_level[16]               = {0};   /* int */
char _network_own_client_id[16]          = {0};   /* ClientID (uint32) */

/* typeinfo objects for the NWidget hierarchy. viewport.cpp instantiates
 * Window::GetWidget<NWID>(), whose dynamic_cast<NWID*>(NWidgetBase*) references
 * `typeinfo for NWidgetBase` and `typeinfo for NWidgetCore` (emitted for real in
 * the uncompiled widget.cpp). The mangled names are valid C identifiers, so raw
 * zeroed storage satisfies the linker. That dynamic_cast never executes on the
 * render path (no GetWidget<> call is reached rendering a grown town), so the
 * zeroed (never-dereferenced) typeinfo is safe. */
char _ZTI11NWidgetBase[64]               = {0};   /* typeinfo for NWidgetBase */
char _ZTI11NWidgetCore[64]               = {0};   /* typeinfo for NWidgetCore */

/* R1 render-merge: the real window.cpp + widget.cpp are compiled (OpenTTD's own
 * window manager / widget tree). They reference these globals, all OFF the
 * town-render / basic-window path: _caret_timer (text-edit blink, no edit box),
 * _toolbar_width (main-toolbar layout, defined in the uncompiled toolbar_gui.cpp)
 * and _network_dedicated (no networking). Exact scalar types (matching the C++
 * `extern` declarations) so no cast is needed; all safe as zero. Function stubs
 * live in m1_window_stubs.cpp. */
int          _caret_timer      = 0;   /* int  (window.cpp: extern int _caret_timer) */
unsigned int _toolbar_width    = 0;   /* uint (toolbar_gui.cpp: uint _toolbar_width) */
char         _network_dedicated = 0;  /* bool (network.h: extern bool _network_dedicated) */

/* R1 toolbar-merge: the real toolbar_gui.cpp (main toolbar) is compiled. Its
 * button/menu handlers reach these globals, all OFF any executed path (the
 * toolbar is dead code until ShowMainToolbar is wired, and even then the
 * handlers are m1_toolbar_stubs no-ops). Pools are empty (Get*IfValid over a
 * zeroed pool -> nullptr); _grfconfig is a null GRFConfig* head (empty list);
 * LinkGraphSchedule::instance is only address-taken by the no-op ShiftDates.
 * Global-scope symbols are unmangled so these C defs bind the C++ externs; the
 * one mangled name (instance) is a valid C identifier. Sized >= the real type.
 * Function stubs live in m1_toolbar_stubs.cpp. */
char _goal_pool[8192]                = {0};   /* GoalPool: empty */
char _story_page_pool[8192]          = {0};   /* StoryPagePool: empty */
char _league_table_pool[8192]        = {0};   /* LeagueTablePool: empty */
char _grfconfig[16]                  = {0};   /* GRFConfig * (null head of GRF list) */
char _ZN17LinkGraphSchedule8instanceE[256] = {0};   /* LinkGraphSchedule::instance (never dereferenced) */
