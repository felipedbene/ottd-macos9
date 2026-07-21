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
 * m1_toolbar_stubs.cpp — link-only stubs for the real toolbar_gui.cpp
 * (OpenTTD's canonical main toolbar: ShowMainToolbar / MainToolbarWindow) in the
 * R1 render-merge build.
 *
 * toolbar_gui.cpp is the top of the GUI: every toolbar button / drop-down menu
 * item fans out to one of ~80 Show / Ask / BuildToolbar handlers that live in GUI
 * TUs we do NOT compile (company_gui, graph_gui, rail_gui, road_gui, ai_gui,
 * network_gui, smallmap_gui, ...). Since NOTHING is wired to click a toolbar
 * button yet (ShowMainToolbar is dead code until integration), each handler is a
 * safe no-op returning a "nothing happened" default: void -> {}, Window* ->
 * nullptr, bool -> false, int -> 0, RoadTypes -> ROADTYPES_NONE, DropDownList ->
 * empty, CommandCost -> default (== success/zero cost, never inspected).
 *
 * Signatures come from the real headers (included below) so the mangled names
 * bind EXACTLY — a wrong signature is a silent link error. Everything defined
 * here is provided by NO other object in the R1 link (verified by probe-link):
 *   - the *_gui / *_cmd handler surface (this file's bulk),
 *   - the CommandHelperBase Post/Execute template hooks (command dispatch backend;
 *     command.cpp is not compiled — m1_cmd_stubs.cpp owns only Do{Before,After}),
 *   - Hotkey / HotkeyList ctors+dtor (hotkeys.cpp not compiled; the static
 *     MainToolbarWindow HotkeyList needs to construct),
 *   - LinkGraphSchedule::ShiftDates (schedule TU not compiled).
 * Globals (pools + _grfconfig + LinkGraphSchedule::instance storage) live in
 * m1_deadpools.c as raw zeroed storage.
 *
 * NOT here (already provided, would multiple-define -> XCOFF ld segfault):
 *   ChangeGameSpeed / MarkWholeScreenDirty (gfx.o), HandleZoomMessage /
 *   SetObjectToPlace / ResetObjectToPlace / _draw_bounding_boxes /
 *   _draw_dirty_blocks (viewport.o), DoZoomInOutWindow (m1_viewport_stubs),
 *   DeleteAllMessages / PositionMainToolbar (window.o), HandleExitGameRequest
 *   (b2_shims.o),
 *   error / MallocError / DebugPrint (b1/m1 shims), InvalidateWindowClassesData
 *   (m1_shims), the Show*DropDownList item classes + ShowDropDownList (dropdown.o),
 *   all Window / WindowDesc / NWidget / MakeNWidgets (window.o, widget.o), Draw and Gfx
 *   (gfx.o), SetDate/ConvertYMDToDate (date.o), RecursiveCommandCounter::_counter
 *   + CommandHelperBase::InternalDo{Before,After} (m1_cmd_stubs).
 */
#include "stdafx.h"

/* --- command dispatch backend + the pause command proc --- */
#include "command_func.h"        /* CommandHelperBase, CommandCost, Backup<CompanyID> */
#include "misc_cmd.h"            /* CmdPause */

/* --- window / hotkey / schedule infrastructure --- */
#include "hotkeys.h"             /* Hotkey, HotkeyList */
#include "linkgraph/linkgraphschedule.h" /* LinkGraphSchedule::ShiftDates */

/* --- the GUI handler surface (one header per fan-out target) --- */
#include "company_gui.h"         /* ShowCompany, DrawCompanyIcon, ShowCompanyFinances, ShowCompanyStations */
#include "gui.h"                 /* ShowLandInfo, ShowGoalsList, ShowStoryBook, ShowAboutWindow, ShowGameOptions, ShowMusicWindow, ShowGameSettings, ShowSubsidiesList, ShowTownDirectory, ShowBuildAirToolbar, ShowFoundTownWindow, ShowBuildDocksToolbar, ShowBuildTreesToolbar, ShowIndustryDirectory, ShowBuildIndustryWindow, ShowExtraViewportWindow(ForTileUnderCursor), ShowBuildDocksScenToolbar, ShowIndustryCargoesWindow */
#include "signs_func.h"          /* ShowSignList, PlaceProc_Sign */
#include "smallmap_gui.h"        /* ShowSmallMap */
#include "console_gui.h"         /* IConsoleSwitch */
#include "network/network_gui.h" /* ShowClientList */
#include "gfx_func.h"            /* CheckBlitter */
#include "cheat_func.h"          /* ShowCheatWindow */
#include "graph_gui.h"           /* ShowIncomeGraph, ShowOperatingProfitGraph, ShowDeliveredCargoGraph, ShowCargoPaymentRates, ShowCompanyValueGraph, ShowPerformanceHistoryGraph, ShowPerformanceRatingDetail */
#include "textbuf_gui.h"         /* ShowQueryString, CharSetFilter, QueryStringFlags */
#include "openttd.h"             /* AskExitToGameMenu, HandleExitGameRequest */
#include "window_func.h"         /* window helper decls (DeleteAllMessages/PositionMainToolbar are in window.o, not stubbed) */
#include "highscore.h"           /* ShowHighscoreTable */
#include "news_gui.h"            /* ShowMessageHistory, ShowLastNewsMessage */
#include "newgrf_config.h"       /* ShowNewGRFSettings, GRFConfig */
#include "fios.h"                /* ShowSaveLoadDialog, AbstractFileType, SaveLoadOperation */
#include "network/network_func.h"/* NetworkServerDoMove, NetworkClientRequestMove, NetworkCompanyIsPassworded, NetworkSendCommand */
#include "framerate_type.h"      /* ShowFramerateWindow */
#include "linkgraph/linkgraph_gui.h" /* ShowLinkGraphLegend */
#include "rail_gui.h"            /* ShowBuildRailToolbar, GetRailTypeDropDownList */
#include "road_gui.h"            /* ShowBuildRoadToolbar, ShowBuildRoadScenToolbar, GetRoadTypeDropDownList, GetScenRoadTypeDropDownList */
#include "league_gui.h"          /* ShowFirstLeagueTable, ShowScriptLeagueTable, ShowPerformanceLeagueTable */
#include "screenshot_gui.h"      /* ShowScreenshotWindow */
#include "screenshot.h"          /* MakeScreenshotWithConfirm, ScreenshotType */
#include "terraform_gui.h"       /* ShowTerraformToolbar, ShowEditorTerraformToolbar */
#include "vehicle_gui.h"         /* ShowVehicleListWindow */
#include "newgrf_debug.h"        /* ShowSpriteAlignerWindow */
#include "transparency_gui.h"    /* ShowTransparencyToolbar */
#include "road_func.h"           /* GetRoadTypes */
#include "sound_func.h"          /* SndPlayFx */

/* ai/ai_gui.hpp + game/game_gui.hpp drag the whole script framework, so
 * forward-declare the 3 handlers we need (CompanyID == Owner from company_type.h,
 * pulled in via company_gui.h). */
Window *ShowAIDebugWindow(CompanyID show_company);
void ShowAIConfigWindow();
void ShowGSConfigWindow();

#include "safeguards.h"

/* ================================================================= *
 * Command dispatch backend — the Post/Execute template hooks         *
 * (command_func.h). Headless & dead: the toolbar issues no command   *
 * until it is wired, and even then these only shuttle test/exec      *
 * results. Defaults are "no error, nothing sent, zero cost".         *
 * ================================================================= */
std::tuple<bool, bool, bool> CommandHelperBase::InternalPostBefore(Commands, CommandFlags, TileIndex, StringID, bool)
{
	return { false, false, false }; /* {err, estimate_only, only_sending} */
}
void CommandHelperBase::InternalPostResult(const CommandCost &, TileIndex, bool, bool, StringID, bool) {}
bool CommandHelperBase::InternalExecutePrepTest(CommandFlags, TileIndex, Backup<CompanyID> &) { return false; }
std::tuple<bool, bool, bool> CommandHelperBase::InternalExecuteValidateTestAndPrepExec(CommandCost &, CommandFlags, bool, bool, Backup<CompanyID> &)
{
	return { false, false, false }; /* {exit_test, desync_log, send_net} */
}
CommandCost CommandHelperBase::InternalExecuteProcessResult(Commands, CommandFlags, const CommandCost &, const CommandCost &, Money, TileIndex, Backup<CompanyID> &)
{
	return CommandCost();
}
void CommandHelperBase::LogCommandExecution(Commands, StringID, TileIndex, const CommandDataBuffer &, bool) {}

/* The pause command proc (misc_cmd.cpp not compiled) — no-op success. */
CommandCost CmdPause(DoCommandFlag, PauseMode, bool) { return CommandCost(); }

/* NetworkSendCommand backend (network core not compiled) — single-player: drop. */
void NetworkSendCommand(Commands, StringID, CommandCallback *, CompanyID, const CommandDataBuffer &) {}

/* ================================================================= *
 * Hotkeys — the static MainToolbarWindow HotkeyList must construct.  *
 * The real ctor registers into a global _hotkey_lists (config I/O);  *
 * we skip that (no config), just store the fields. Never matched     *
 * (HotkeyList::CheckMatch is the m1_window_stubs -1 no-op).          *
 * ================================================================= */
Hotkey::Hotkey(uint16 default_keycode, const char *name, int num) : name(name), num(num)
{
	(void)default_keycode;
}
Hotkey::Hotkey(const uint16 *default_keycodes, const char *name, int num) : name(name), num(num)
{
	(void)default_keycodes;
}
HotkeyList::HotkeyList(const char *ini_group, Hotkey *items, GlobalHotkeyHandlerFunc global_hotkey_handler)
	: global_hotkey_handler(global_hotkey_handler), ini_group(ini_group), items(items) {}
HotkeyList::~HotkeyList() {}

/* LinkGraph schedule date-shift (schedule TU not compiled). instance storage is
 * in m1_deadpools.c; a no-op ShiftDates never touches its (zeroed) job lists. */
void LinkGraphSchedule::ShiftDates(int) {}

/* ================================================================= *
 * GUI handler surface — every toolbar button / menu target. No-ops   *
 * until ShowMainToolbar is wired. Grouped by return type.            *
 * ================================================================= */

/* ---- void () ---- */
void ShowSmallMap() {}
void IConsoleSwitch() {}
void ShowClientList() {}
void ShowAboutWindow() {}
void ShowCheatWindow() {}
void ShowGameOptions() {}
/* ShowIncomeGraph is now REAL — m1_graph_gui.cpp (R1-86). */
void ShowMusicWindow() {}
void ShowGameSettings() {}
void AskExitToGameMenu() {}
void ShowSubsidiesList() {}
/* ShowTownDirectory is now REAL — the extracted TownDirectoryWindow in m1_town_directory_gui.cpp (R1-82). */
void ShowAIConfigWindow() {}
void ShowGSConfigWindow() {}
void ShowMessageHistory() {}
void ShowLastNewsMessage() {}
void ShowFramerateWindow() {}
void ShowLinkGraphLegend() {}
void ShowFirstLeagueTable() {}
void ShowScreenshotWindow() {}
void ShowBuildTreesToolbar() {}
void ShowCargoPaymentRates() {}
void ShowCompanyValueGraph() {}
/* ShowIndustryDirectory now REAL — the extracted IndustryDirectoryWindow in m1_industry_gui.cpp (R1-91). */
void ShowBuildIndustryWindow() {}
void ShowDeliveredCargoGraph() {}
void ShowSpriteAlignerWindow() {}
void ShowTransparencyToolbar() {}
/* ShowOperatingProfitGraph is now REAL — m1_graph_gui.cpp (R1-86). */
void ShowIndustryCargoesWindow() {}
void ShowPerformanceLeagueTable() {}
void ShowPerformanceHistoryGraph() {}
void ShowPerformanceRatingDetail() {}
void ShowExtraViewportWindowForTileUnderCursor() {}
void CheckBlitter() {}
void ShowFoundTownWindow() {}

/* ---- void (args) ---- */
/* ShowCompany is now REAL — m1_company_gui.cpp (R1-86). */
/* ShowCompanyFinances is now REAL — the extracted CompanyFinancesWindow in m1_finance_gui.cpp (R1-79). */
/* ShowCompanyStations is now REAL — m1_station_gui.cpp (R1-86). */
void DrawCompanyIcon(CompanyID, int, int) {}
void ShowLandInfo(TileIndex) {}
void ShowGoalsList(CompanyID) {}
void ShowStoryBook(CompanyID, uint16) {}
void PlaceProc_Sign(TileIndex) {}
void ShowQueryString(StringID, StringID, uint, Window *, CharSetFilter, QueryStringFlags) {}
void ShowHighscoreTable(int, int8) {}
void ShowNewGRFSettings(bool, bool, bool, GRFConfig **) {}
void ShowSaveLoadDialog(AbstractFileType, SaveLoadOperation) {}
void NetworkServerDoMove(ClientID, CompanyID) {}
void NetworkClientRequestMove(CompanyID, const std::string &) {}
void ShowScriptLeagueTable(LeagueTableID) {}
/* ShowVehicleListWindow is now REAL — m1_vehicle_list_gui.cpp (R1-86). */
void ShowExtraViewportWindow(TileIndex) {}
void MakeScreenshotWithConfirm(ScreenshotType) {}
void SndPlayFx(SoundID) {}

/* ---- Window* () ---- */
Window *ShowSignList() { return nullptr; }
Window *ShowBuildAirToolbar() { return nullptr; }
Window *ShowBuildDocksToolbar() { return nullptr; }
Window *ShowBuildDocksScenToolbar() { return nullptr; }
Window *ShowEditorTerraformToolbar() { return nullptr; }

/* ---- Window* (args) ---- */
Window *ShowAIDebugWindow(CompanyID) { return nullptr; }
Window *ShowBuildRailToolbar(RailType) { return nullptr; }
Window *ShowBuildRoadToolbar(RoadType) { return nullptr; }
Window *ShowBuildRoadScenToolbar(RoadType) { return nullptr; }
Window *ShowTerraformToolbar(Window *) { return nullptr; }

/* ---- scalar returns ---- */
bool NetworkCompanyIsPassworded(CompanyID) { return false; }
RoadTypes GetRoadTypes(bool) { return ROADTYPES_NONE; }

/* ---- DropDownList () — empty menu ---- */
DropDownList GetRailTypeDropDownList(bool, bool) { return {}; }
DropDownList GetRoadTypeDropDownList(RoadTramTypes, bool, bool) { return {}; }
DropDownList GetScenRoadTypeDropDownList(RoadTramTypes) { return {}; }
