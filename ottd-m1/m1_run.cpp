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

// M1: run the REAL OpenTTD game loop headless on Mac OS 9 / PPC.
// No GUI, no rendering: allocate a small map, sculpt terrain with the real
// accessors, set the calendar to 1950, then drive the genuine IncreaseDate()
// (which fans out to the real daily/monthly/yearly simulation loops). Each new
// year we log the year + live town count to the UDP sink.
#include "stdafx.h"
#include "openttd.h"
#include "date_func.h"
#include "date_type.h"
#include "map_func.h"
#include "tile_map.h"
#include "clear_map.h"
#include "void_map.h"
#include "settings_type.h"
#include "town.h"
#include "town_type.h"
#include "town_kdtree.h"
#include "company_type.h"
#include "command_func.h"
#include "town_cmd.h"
#include "landscape_cmd.h"
#include "road_cmd.h"
#include "road_type.h"
#include "company_func.h"
#include <cstring>

extern "C" void ottd_log(const char *fmt, ...);
extern "C" void m1_settle(void);   /* ~50ms Toolbox Delay between milestone logs */

// game-state globals that IncreaseDate() / the loops read.
extern GameMode _game_mode;
extern bool _generating_world;   // build 10: true during town growth so houses complete + gain population

// IncreaseDate() is declared only locally in openttd.cpp upstream; declare it here.
void IncreaseDate();

// A gentle pyramidal hill (same shape as maplogic.cpp) so the map is not flat.
static uint HillHeight(uint x, uint y, uint size)
{
	int cx = (int)size / 2, cy = (int)size / 2;
	int d = (x > (uint)cx ? (int)x - cx : cx - (int)x)
	      + (y > (uint)cy ? (int)y - cy : cy - (int)y);
	int h = 8 - d / 4;
	if (h < 0) h = 0;
	return (uint)h;
}

// Found a single ungrown Town at the map centre in the real TownPool. Mirrors
// the scalar-init half of DoCreateTown() (town_cmd.cpp lines 1843-1892), minus
// everything that reaches into other subsystems: the kdtree insert, the
// viewport sign (UpdateVirtCoord), the window invalidations, InitializeLayout,
// and the GrowTown() house-building loop. Those are the parts that pull the
// town-command cascade; the pool allocation itself is genuine.
//
// Build 6: town_cmd.cpp is compiled for real, so we now call the genuine
// UpdateTownRadius(t) (real town_cmd code) to compute the cached zone radii,
// and the real TownsMonthlyLoop()/TownsYearlyLoop() run each period inside the
// IncreaseDate() loop (invoked by the real date.cpp).
void UpdateTownRadius(Town *t);   // town_cmd.cpp (public)

static void FoundOneTown(uint size)
{
	if (!Town::CanAllocateItem()) { ottd_log("M1: town pool full?!"); return; }

	TileIndex centre = TileXY(size / 2, size / 2);
	Town *t = new Town(centre);   // real Pool<Town>::GetNew on PPC

	/* The Town pool is NOT zero-allocated (TownPool has Tzero=false), so cache +
	 * cargo/goal/unwanted arrays are garbage. Harmless through build 9 (never
	 * read), but build 10 house-building READS cache.building_counts / goal /
	 * received, so zero the POD regions here (NOT the std::string/std::list
	 * members — the ctor already constructed those). */
	memset((void *)&t->cache, 0, sizeof(t->cache));
	memset(t->goal, 0, sizeof(t->goal));
	memset(t->supplied, 0, sizeof(t->supplied));
	memset(t->received, 0, sizeof(t->received));
	memset(t->unwanted, 0, sizeof(t->unwanted));
	t->road_build_months = 0;
	t->noise_reached = 0;

	t->xy = centre;
	t->cache.num_houses = 0;
	t->cache.population = 0;
	t->time_until_rebuild = 10;
	t->flags = 0;
	t->grow_counter = t->index % TOWN_GROWTH_TICKS;
	t->growth_rate = TownTicksToGameTicks(250);
	t->show_zone = false;
	t->fund_buildings_months = 0;
	for (uint i = 0; i != MAX_COMPANIES; i++) t->ratings[i] = RATING_INITIAL;
	t->have_ratings = 0;
	t->exclusivity = INVALID_COMPANY;
	t->exclusive_counter = 0;
	t->statues = 0;
	t->townnameparts = 0;
	t->townnamegrfid = 0;
	t->townnametype = 0;
	t->larger_town = false;
	t->layout = TL_ORIGINAL;

	/* Insert the town into the real k-d tree, exactly as DoCreateTown does
	 * (town_cmd.cpp line ~1857). Build 9 fix: without this the pool has the town
	 * (GetNumItems()==1) but the kdtree is empty, and CalcClosestTownFromTile —
	 * reached by CmdBuildRoad with OWNER_DEITY — guards only on GetNumItems() then
	 * calls _town_kdtree.FindNearest on the empty tree -> Town::Get(garbage) ->
	 * silent abort. GrowTown() (build 10) needs the kdtree populated too. */
	_town_kdtree.Insert(t->index);

	UpdateTownRadius(t);   // real town_cmd.cpp: fills cache.squared_town_zone_radius[]

	ottd_log("M1: founded town index=%u xy=%u pop=%u houses=%u rad0=%u growth=%u",
	         (uint)t->index, (uint)t->xy,
	         (uint)t->cache.population, (uint)t->cache.num_houses,
	         (uint)t->cache.squared_town_zone_radius[0], (uint)t->growth_rate);
	m1_settle();
}

extern "C" int m1_run(void)
{
	ottd_log("=== M1: real game loop, headless ===");

	_game_mode = GM_NORMAL;

	// Minimal settings: rely on zero/default-initialised _settings_game /
	// _settings_client (defined in m1_shims). Turn off the game-end trigger.
	_settings_game.game_creation.ending_year = 0;
	_settings_client.gui.autosave = 0;
	// Build 10: let GrowTown lay its first road (else its road-block is skipped
	// when allow_town_roads=0 && !_generating_world) and allow slope foundations.
	_settings_game.economy.allow_town_roads = true;
	_settings_game.construction.build_on_slopes = true;

	const uint SIZE = 64;
	ottd_log("M1: AllocateMap(%u,%u)", SIZE, SIZE); m1_settle();
	AllocateMap(SIZE, SIZE);
	ottd_log("M1: map allocated MapSize=%u", (uint)MapSize()); m1_settle();

	/* Sculpt exactly like the real InitializeLandscape() (landscape.cpp): clear
	 * only the INNER tiles (x,y < MapMax) then MakeVoid the SE/SW border. Build
	 * 3 proved that MakeClear on a border tile (63,0) trips SetTileType's
	 * `IsInnerTile(t) == (type != MP_VOID)` assert -> silent abort(). Only the
	 * inner playable area is MP_CLEAR; the max-edge row/col must be MP_VOID.
	 * freeform_edges is off, so the near edges (x=0,y=0) are still playable. */
	/* Build 10: FLAT terrain (height 0). Build 9's decorative hill put the town
	 * centre (32,32) on a slope, which would make GrowTown's road/house builds
	 * fail CheckRoadSlope. A flat map lets roads and houses go anywhere. */
	for (uint y = 0; y < MapMaxY(); y++) {
		for (uint x = 0; x < MapMaxX(); x++) {
			TileIndex t = TileXY(x, y);
			MakeClear(t, CLEAR_GRASS, 3);
			SetTileHeight(t, 0);
		}
	}
	for (uint x = 0; x < MapSizeX(); x++) MakeVoid(TileXY(x, MapMaxY()));
	for (uint y = 0; y < MapSizeY(); y++) MakeVoid(TileXY(MapMaxX(), y));
	ottd_log("M1: terrain sculpted (flat, inner clear + void border)"); m1_settle();

	/* Found ONE real Town in the genuine TownPool (m1_pools.cpp now instantiates
	 * the pool's GetNew via INSTANTIATE_POOL_METHODS). This is a faithful subset
	 * of DoCreateTown (town_cmd.cpp): the REAL `new Town(tile)` allocation + the
	 * scalar field init. We deliberately STOP before GrowTown()/BuildTownHouse()
	 * — house-building fans out into the NewGRF/sprite/viewport cascade (the
	 * ~357 symbols M1 bounds by stubbing). So the town is founded but ungrown:
	 * num_houses = population = 0. What this proves on HW: the real Pool<Town>
	 * allocator runs on PPC, assigns an index, and the record persists through
	 * the multi-year IncreaseDate() loop below. */
	FoundOneTown(SIZE);
	ottd_log("M1: after founding towns=%u", (uint)Town::GetNumItems()); m1_settle();

	/* Build 7: execute a REAL OpenTTD command through the genuine dispatch.
	 * Command<CMD_TOWN_GROWTH_RATE>::Do runs the real, header-only Command<>::Do
	 * template (RecursiveCommandCounter guard + test pass + exec pass) and invokes
	 * the genuine CmdTownGrowthRate proc (already compiled in town_cmd.o). The
	 * command is CMD_DEITY, so we take the deity role first. Observable proof:
	 * the town's growth_rate becomes exactly our requested 4242 AND the real proc
	 * sets the TOWN_CUSTOM_GROWTH flag (so flags != 0, and the monthly loop then
	 * STOPS overwriting growth_rate — it stays 4242 for the rest of the run).
	 * This proves the command dispatch system — the keystone for roads/rails/
	 * stations/vehicles — runs on PPC. (Tile commands like roads need
	 * landscape.cpp + the _tile_type_procs table; that's build 8.) */
	_current_company = OWNER_DEITY;
	const Town *tg = Town::GetIfValid(0);
	ottd_log("M1: pre-cmd  growth=%u flags=%u", tg ? (uint)tg->growth_rate : 0u,
	         tg ? (uint)tg->flags : 0u); m1_settle();
	CommandCost rc = Command<CMD_TOWN_GROWTH_RATE>::Do(DC_EXEC, (TownID)0, (uint16)4242);
	tg = Town::GetIfValid(0);
	ottd_log("M1: town cmd ok=%d growth=%u flags=%u (growth=4242 & flags!=0 -> real cmd ran)",
	         (int)rc.Succeeded(), tg ? (uint)tg->growth_rate : 0u,
	         tg ? (uint)tg->flags : 0u); m1_settle();

	/* Build 8: a real TILE command through the full dispatch chain. landscape.cpp
	 * (the _tile_type_procs table + CmdLandscapeClear) and clear_cmd.cpp (real
	 * ClearTile_Clear) are compiled for real. We paint a tile CLEAR_ROCKS, then
	 * Command<CMD_LANDSCAPE_CLEAR>::Do dispatches through _tile_type_procs[MP_CLEAR]
	 * -> ClearTile_Clear -> DoClearSquare, which normalises it to CLEAR_GRASS.
	 * ground 2->0 = a real tile command mutated the map on PPC (the tile-command
	 * keystone that roads/rails/stations all build on). */
	TileIndex ctile = TileXY(20, 20);
	MakeClear(ctile, CLEAR_ROCKS, 3);
	ottd_log("M1: pre-clear  tile=%u type=%u ground=%u", (uint)ctile,
	         (uint)GetTileType(ctile), (uint)GetClearGround(ctile)); m1_settle();
	CommandCost rcc = Command<CMD_LANDSCAPE_CLEAR>::Do(DC_EXEC, ctile);
	ottd_log("M1: clear cmd ok=%d type=%u ground=%u (ground 2->0 grass -> tile cmd mutated map)",
	         (int)rcc.Succeeded(), (uint)GetTileType(ctile), (uint)GetClearGround(ctile)); m1_settle();

	/* Build 9: lay a REAL road via Command<CMD_BUILD_ROAD>::Do. This runs the
	 * genuine CmdBuildRoad, which internally clears the tile (real
	 * Command<CMD_LANDSCAPE_CLEAR>) then MakeRoadNormal()s an MP_ROAD tile. We
	 * build on a FLAT tile in the map's height-0 zone (8,8) so CheckRoadSlope
	 * doesn't demand a foundation, and take the deity role (CmdBuildRoad maps
	 * OWNER_DEITY -> OWNER_TOWN/OWNER_NONE; town_id must be 0 for deity). type
	 * MP_CLEAR(%u) -> MP_ROAD proves a road was laid on PPC. This is the road
	 * seam that real GrowTown() needs to grow houses. */
	_current_company = OWNER_DEITY;
	TileIndex rtile = TileXY(8, 8);
	ottd_log("M1: pre-road   tile=%u type=%u (MP_CLEAR expected)", (uint)rtile,
	         (uint)GetTileType(rtile)); m1_settle();
	CommandCost rcr = Command<CMD_BUILD_ROAD>::Do(DC_EXEC, rtile, ROAD_X, ROADTYPE_ROAD, DRD_NONE, (TownID)0);
	ottd_log("M1: road cmd  ok=%d type=%u isroad=%d (type -> MP_ROAD -> real road laid)",
	         (int)rcr.Succeeded(), (uint)GetTileType(rtile),
	         (int)IsTileType(rtile, MP_ROAD)); m1_settle();

	/* Build 10 — THE FINISH LINE: grow real HOUSES. ResetHouses() populates the
	 * real _house_specs from the built-in _original_house_specs table (both in
	 * town_cmd.o). Then Command<CMD_EXPAND_TOWN>::Do runs the genuine GrowTown(t)
	 * in a loop: GrowTown lays town roads via the (build-9) command system and
	 * BuildTownHouse()s real MP_HOUSE tiles from the spec table, ChangePopulation
	 * bumping the town's population. houses>0 && pop>0 && MP_HOUSE tiles on the
	 * map = the real OpenTTD town-growth engine grew a town on Mac OS 9 / PPC. */
	/* Set the calendar to 1950 BEFORE growing: BuildTownHouse skips any house
	 * whose min_year > _cur_year (town_cmd.cpp ~2615). Build 10 first attempt grew
	 * roads but 0 houses because _cur_year was still 0 -> every era house skipped. */
	SetDate(ConvertYMDToDate(1950, 0, 1), 0);
	ResetHouses();
	_current_company = OWNER_DEITY;
	_generating_world = true;   // houses complete immediately + add population
	const Town *th = Town::GetIfValid(0);
	ottd_log("M1: pre-grow  year=%d houses=%u pop=%u", (int)_cur_year,
	         th ? (uint)th->cache.num_houses : 0u,
	         th ? (uint)th->cache.population : 0u); m1_settle();
	/* grow_amount=0 uses CmdExpandTown's DoCreateTown-style path: temporarily
	 * INFLATE num_houses (=> real zone radius via UpdateTownRadius) then run
	 * GrowTown amount*10 times so BuildTownHouse actually has buildable area.
	 * grow_amount!=0 skips the inflation -> radius stays tiny (rad^2=4) -> no
	 * houses (build 10/10b). Loop it to bootstrap a real town (each pass inflates
	 * more as num_houses climbs). */
	CommandCost rce;
	for (int g = 0; g < 8; g++) {
		rce = Command<CMD_EXPAND_TOWN>::Do(DC_EXEC, (TownID)0, (uint32)0);
		th = Town::GetIfValid(0);
		ottd_log("M1: grow pass=%d ok=%d houses=%u pop=%u", g, (int)rce.Succeeded(),
		         th ? (uint)th->cache.num_houses : 0u, th ? (uint)th->cache.population : 0u);
		m1_settle();
	}
	_generating_world = false;
	th = Town::GetIfValid(0);
	uint mp_house = 0, mp_road = 0;
	for (TileIndex tt = 0; tt < MapSize(); tt++) {
		if (IsTileType(tt, MP_HOUSE)) mp_house++;
		else if (IsTileType(tt, MP_ROAD)) mp_road++;
	}
	ottd_log("M1: grow cmd  ok=%d houses=%u pop=%u mp_house=%u mp_road=%u (houses>0 -> town GREW)",
	         (int)rce.Succeeded(), th ? (uint)th->cache.num_houses : 0u,
	         th ? (uint)th->cache.population : 0u, mp_house, mp_road); m1_settle();
	_current_company = OWNER_NONE;

	// Start the calendar at 1950-01-01.
	Date start = ConvertYMDToDate(1950, 0, 1);
	SetDate(start, 0);
	ottd_log("M1: start date set year=%d towns=%u", (int)_cur_year, (uint)Town::GetNumItems()); m1_settle();

	// Drive the real IncreaseDate() across several years. DAY_TICKS ticks = 1 day.
	const int YEARS = 6;
	const int TOTAL_TICKS = YEARS * DAYS_IN_YEAR * DAY_TICKS + DAY_TICKS;
	int last_year = (int)_cur_year;
	for (int i = 0; i < TOTAL_TICKS; i++) {
		IncreaseDate();
		if ((int)_cur_year != last_year) {
			last_year = (int)_cur_year;
			const Town *t0 = Town::GetIfValid(0);
			ottd_log("M1: year=%d towns=%u date=%d growth=%u flags=%u",
			         last_year, (uint)Town::GetNumItems(), (int)_date,
			         t0 ? (uint)t0->growth_rate : 0u, t0 ? (uint)t0->flags : 0u); m1_settle();
		}
	}

	ottd_log("=== M1: done, final year=%d ticks=%d ===", (int)_cur_year, TOTAL_TICKS); m1_settle();
	return 0;
}
