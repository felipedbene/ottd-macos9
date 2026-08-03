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

/*
 * m1_gwstub.cpp — link surface for compiling real tgp.cpp + genworld.cpp.
 *
 * Those two TUs are the game's own TerraGenesis perlin terrain and the
 * generate-world orchestrator. Pulling them in closes the hand-rolled
 * GenerateTerrainPerlin stub in m1_land_stubs.cpp, but they also reference a
 * handful of subsystems this port does not have: Game Script VM, savegame
 * layer, NewGRF error UI, modal progress dialogs, disasters, and the full
 * industry/object placement pass that assumes those. Every definition here is
 * an honest no-op / "absent" return so the linker is satisfied; none of them
 * are meant to stand in for the real subsystem later — when a subsystem lands
 * for real, its stub moves out of this file the same way the vehicle stack
 * did under R1_REAL_VEHICLE_STACK.
 *
 * Deliberately NOT defined here (owned elsewhere / runtime):
 *   GenerateTerrainPerlin  — real body in tgp.cpp; stub guarded in m1_land_stubs.cpp
 *   _generating_world      — already in m1_deadpools.c (merged with real TUs)
 */
#include "stdafx.h"

#include "debug.h"
#include "newgrf_storage.h"
#include "heightmap.h"
#include "game/game.hpp"
#include "gfxinit.h"
#include "genworld.h"
#include "saveload/saveload.h"
#include "company_func.h"
#include "progress.h"
#include "window_func.h"
#include "newgrf.h"
#include "openttd.h"

#include "safeguards.h"

/* Network debug channel — genworld logs through Debug(net, ...). Level 0 = quiet. */
int _debug_net_level = 0;

/* NewGRF persistent-storage mode switches during map gen callbacks. No NewGRF
 * storage layer here, so mode changes are ignored. */
void BasePersistentStorageArray::SwitchMode(PersistentStorageMode, bool) {}

/* Heightmap path used when the generator wants a flat base before perlin.
 * Terrain shaping is tgp's job; a flat fill is not required for the link. */
void FlatEmptyWorld(byte) {}

/* Game Script VM is not compiled. instance stays null; loop/start are no-ops. */
/* static */ class GameInstance *Game::instance = nullptr;
void Game::GameLoop() {}
void Game::StartNew() {}

/* Industry / object placement during map gen — not wired; empty map is fine. */
void GenerateIndustries() {}
void GenerateObjects() {}

/* Sprite reload at end of generation — R1 already owns its sprite path. */
void GfxLoadSprites() {}

/* Full game re-init (map size / date / settings). genworld calls this; the port
 * brings the map up through its own path, so this is a no-op. */
void InitializeGame(uint, uint, bool, bool) {}

/* Modal progress UI for map generation — no dialogs in this build. */
void PrepareGenerateWorldProgress() {}
void ShowGenerateWorldProgress() {}
void SetModalProgress(bool) {}

/* Savegame layer absent — refuse any save/load request. */
SaveOrLoadResult SaveOrLoad(const std::string &, SaveLoadOperation, DetailedFileType, Subdirectory, bool)
{
	return SL_ERROR;
}

/* Mirror m1_company.cpp: point both locals at the new owner, skip window churn. */
void SetLocalCompany(CompanyID new_company)
{
	_current_company = _local_company = new_company;
}

/* Colour tables + first windows — R1 scene owns its own bring-up. */
void SetupColoursAndInitialWindow() {}

/* NewGRF error popup — no NewGRF error UI. */
void ShowNewGRFError() {}

/* Vital windows after generation — no window stack to restore. */
void ShowVitalWindows() {}

/* Company / economy / disaster startup that follows a fresh map. Companies are
 * stood up by r1_make_company(); economy seeding lives in m1_economy.cpp;
 * disasters are not compiled. */
void StartupCompanies() {}
void StartupDisasters() {}
void StartupEconomy() {}

/* Mode switcher (menu / newgame / editor / …). Port drives its own mode. */
void SwitchToMode(SwitchMode) {}
