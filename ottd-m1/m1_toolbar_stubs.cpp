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
 *     command.cpp is not compiled — m1_cmd_stubs.cpp owns only Do{Before,After}; this
 *     file owns Post/Execute so toolbar CMD_PAUSE can run),
 *   - Hotkey / HotkeyList ctors+dtor (hotkeys.cpp not compiled; the static
 *     MainToolbarWindow HotkeyList needs to construct),
 *   - LinkGraphSchedule::ShiftDates (schedule TU not compiled).
 *   - Lean ExtraViewportWindow (viewport_gui.cpp not compiled) + real CmdPause /
 *     DrawCompanyIcon / AskExitToGameMenu / ShowLandInfo / GetRoadTypes fill-ins.
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
#include "viewport_func.h"       /* ScrollMainWindowToTile, GetTileBelowCursor, RemapCoords, DoZoomInOutWindow, HandleZoomMessage, ZoomInOrOutToCursorWindow */
#include "viewport_type.h"       /* Viewport */
#include "window_gui.h"          /* Window / NWidgetViewport for ExtraViewportWindow */
#include "zoom_func.h"           /* ScaleZoomGUI, ScaleByZoom */
#include "widgets/viewport_widget.h" /* WID_EV_* */
#include "sprite.h"              /* SPR_COMPANY_ICON, COMPANY_SPRITE_COLOUR */
#include "table/sprites.h"       /* SPR_IMG_ZOOMIN / ZOOMOUT */
#include "table/strings.h"       /* STR_EXTRA_VIEWPORT_TITLE */
#include "company_func.h"        /* _company_colours (COMPANY_SPRITE_COLOUR) */
#include "settings_type.h"       /* _settings_client.gui.zoom_min */
#include "landscape.h"           /* TilePixelHeight */
#include "tile_map.h"            /* TileX / TileY */
#include "vehicle_type.h"        /* INVALID_VEHICLE */

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
/* Must return true or Command<>::Post never reaches the command proc — that left the
 * canonical toolbar's Pause button dead even with a real CmdPause. */
bool CommandHelperBase::InternalExecutePrepTest(CommandFlags, TileIndex, Backup<CompanyID> &) { return true; }
std::tuple<bool, bool, bool> CommandHelperBase::InternalExecuteValidateTestAndPrepExec(CommandCost &res, CommandFlags, bool estimate_only, bool, Backup<CompanyID> &)
{
	/* exit_test=true skips the DC_EXEC pass; do that on failure or estimate-only. */
	if (res.Failed() || estimate_only) return { true, false, false };
	return { false, false, false }; /* {exit_test, desync_log, send_net} */
}
CommandCost CommandHelperBase::InternalExecuteProcessResult(Commands, CommandFlags, const CommandCost &, const CommandCost &res_exec, Money, TileIndex, Backup<CompanyID> &)
{
	return res_exec;
}
void CommandHelperBase::LogCommandExecution(Commands, StringID, TileIndex, const CommandDataBuffer &, bool) {}

/* REAL — misc_cmd.cpp CmdPause, minus network pause hooks / NewGRF unpause query
 * (neither subsystem is linked). Sets _pause_mode so the toolbar button lights and
 * r1_tick can freeze the sim. */
CommandCost CmdPause(DoCommandFlag flags, PauseMode mode, bool pause)
{
	switch (mode) {
		case PM_PAUSED_SAVELOAD:
		case PM_PAUSED_ERROR:
		case PM_PAUSED_NORMAL:
		case PM_PAUSED_GAME_SCRIPT:
		case PM_PAUSED_LINK_GRAPH:
			break;
		case PM_PAUSED_JOIN:
		case PM_PAUSED_ACTIVE_CLIENTS:
			if (!_networking) return CMD_ERROR;
			break;
		default:
			return CMD_ERROR;
	}
	if (flags & DC_EXEC) {
		if (pause) {
			_pause_mode |= mode;
		} else {
			_pause_mode &= ~mode;
		}
		SetWindowDirty(WC_MAIN_TOOLBAR, 0);
	}
	return CommandCost();
}

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
 * Extra viewport — lean port of viewport_gui.cpp so Map→Extra Viewport
 * and Ctrl+click location buttons in company/station/town/industry/
 * subsidy windows actually open a second map view (NWID_VIEWPORT is
 * already proven by the main window). Shade/defsize/sticky/resize boxes
 * omitted (R1-84: those sprites are not in the loaded base GRF).        *
 * ================================================================= */
static const NWidgetPart _nested_extra_viewport_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_EV_CAPTION), SetDataTip(STR_EXTRA_VIEWPORT_TITLE, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_VIEWPORT, INVALID_COLOUR, WID_EV_VIEWPORT), SetPadding(2, 2, 2, 2), SetResize(1, 1), SetFill(1, 1), SetMinimalSize(300, 200),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_EV_ZOOM_IN), SetDataTip(SPR_IMG_ZOOMIN, STR_TOOLBAR_TOOLTIP_ZOOM_THE_VIEW_IN),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_EV_ZOOM_OUT), SetDataTip(SPR_IMG_ZOOMOUT, STR_TOOLBAR_TOOLTIP_ZOOM_THE_VIEW_OUT),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_EV_MAIN_TO_VIEW), SetFill(1, 1), SetResize(1, 0),
					SetDataTip(STR_EXTRA_VIEW_MOVE_MAIN_TO_VIEW, STR_EXTRA_VIEW_MOVE_MAIN_TO_VIEW_TT),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_EV_VIEW_TO_MAIN), SetFill(1, 1), SetResize(1, 0),
					SetDataTip(STR_EXTRA_VIEW_MOVE_VIEW_TO_MAIN, STR_EXTRA_VIEW_MOVE_VIEW_TO_MAIN_TT),
		EndContainer(),
	EndContainer(),
};

struct ExtraViewportWindow : Window {
	ExtraViewportWindow(WindowDesc *desc, int window_number, TileIndex tile) : Window(desc)
	{
		this->InitNested(window_number);

		NWidgetViewport *nvp = this->GetWidget<NWidgetViewport>(WID_EV_VIEWPORT);
		nvp->InitializeViewport(this, 0, ScaleZoomGUI(ZOOM_LVL_VIEWPORT));
		if (_settings_client.gui.zoom_min == this->viewport->zoom) this->DisableWidget(WID_EV_ZOOM_IN);

		Point pt;
		if (tile == INVALID_TILE) {
			const Window *w = FindWindowById(WC_MAIN_WINDOW, 0);
			if (w != nullptr && w->viewport != nullptr) {
				pt.x = w->viewport->scrollpos_x + w->viewport->virtual_width / 2;
				pt.y = w->viewport->scrollpos_y + w->viewport->virtual_height / 2;
			} else {
				pt.x = 0;
				pt.y = 0;
			}
		} else {
			pt = RemapCoords(TileX(tile) * TILE_SIZE + TILE_SIZE / 2, TileY(tile) * TILE_SIZE + TILE_SIZE / 2, TilePixelHeight(tile));
		}

		this->viewport->scrollpos_x = pt.x - this->viewport->virtual_width / 2;
		this->viewport->scrollpos_y = pt.y - this->viewport->virtual_height / 2;
		this->viewport->dest_scrollpos_x = this->viewport->scrollpos_x;
		this->viewport->dest_scrollpos_y = this->viewport->scrollpos_y;
	}

	void SetStringParameters(int widget) const override
	{
		if (widget == WID_EV_CAPTION) SetDParam(0, this->window_number + 1);
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_EV_ZOOM_IN:  DoZoomInOutWindow(ZOOM_IN,  this); break;
			case WID_EV_ZOOM_OUT: DoZoomInOutWindow(ZOOM_OUT, this); break;

			case WID_EV_MAIN_TO_VIEW: {
				Window *w = FindWindowById(WC_MAIN_WINDOW, 0);
				if (w == nullptr || w->viewport == nullptr || this->viewport == nullptr) break;
				w->viewport->dest_scrollpos_x = this->viewport->scrollpos_x - (w->viewport->virtual_width - this->viewport->virtual_width) / 2;
				w->viewport->dest_scrollpos_y = this->viewport->scrollpos_y - (w->viewport->virtual_height - this->viewport->virtual_height) / 2;
				w->viewport->follow_vehicle = INVALID_VEHICLE;
				break;
			}

			case WID_EV_VIEW_TO_MAIN: {
				const Window *w = FindWindowById(WC_MAIN_WINDOW, 0);
				if (w == nullptr || w->viewport == nullptr || this->viewport == nullptr) break;
				this->viewport->dest_scrollpos_x = w->viewport->scrollpos_x + (w->viewport->virtual_width - this->viewport->virtual_width) / 2;
				this->viewport->dest_scrollpos_y = w->viewport->scrollpos_y + (w->viewport->virtual_height - this->viewport->virtual_height) / 2;
				break;
			}
		}
	}

	void OnResize() override
	{
		if (this->viewport != nullptr) {
			NWidgetViewport *nvp = this->GetWidget<NWidgetViewport>(WID_EV_VIEWPORT);
			nvp->UpdateViewportCoordinates(this);
		}
	}

	void OnScroll(Point delta) override
	{
		this->viewport->scrollpos_x += ScaleByZoom(delta.x, this->viewport->zoom);
		this->viewport->scrollpos_y += ScaleByZoom(delta.y, this->viewport->zoom);
		this->viewport->dest_scrollpos_x = this->viewport->scrollpos_x;
		this->viewport->dest_scrollpos_y = this->viewport->scrollpos_y;
	}

	void OnMouseWheel(int wheel) override
	{
		if (_settings_client.gui.scrollwheel_scrolling != 2) {
			ZoomInOrOutToCursorWindow(wheel < 0, this);
		}
	}

	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		HandleZoomMessage(this, this->viewport, WID_EV_ZOOM_IN, WID_EV_ZOOM_OUT);
	}
};

static WindowDesc _extra_viewport_desc(
	WDP_AUTO, "extra_viewport", 300, 268,
	WC_EXTRA_VIEWPORT, WC_NONE,
	0,
	_nested_extra_viewport_widgets, lengthof(_nested_extra_viewport_widgets)
);

/* ================================================================= *
 * GUI handler surface — every toolbar button / menu target. Grouped  *
 * by return type. Handlers with real bodies elsewhere are commented  *
 * out (would multiple-define).                                       *
 * ================================================================= */

/* ---- void () ---- */
/* ShowSmallMap now REAL — the extracted SmallMapWindow in m1_smallmap_gui.cpp. */
void IConsoleSwitch() {}
void ShowClientList() {}
void ShowAboutWindow() {}
void ShowCheatWindow() {}
void ShowGameOptions() {}
/* ShowIncomeGraph is now REAL — m1_graph_gui.cpp (R1-86). */
void ShowMusicWindow() {}
void ShowGameSettings() {}
/* No intro/menu TU — exiting the app is the honest stand-in. */
void AskExitToGameMenu() { HandleExitGameRequest(); }
/* ShowSubsidiesList now REAL — the extracted SubsidyListWindow in m1_subsidy_gui.cpp. */
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
void ShowExtraViewportWindowForTileUnderCursor()
{
	Point pt = GetTileBelowCursor();
	ShowExtraViewportWindow(pt.x != -1 ? TileVirtXY(pt.x, pt.y) : INVALID_TILE);
}
void CheckBlitter() {}
void ShowFoundTownWindow() {}

/* ---- void (args) ---- */
/* ShowCompany is now REAL — m1_company_gui.cpp (R1-86). */
/* ShowCompanyFinances is now REAL — the extracted CompanyFinancesWindow in m1_finance_gui.cpp (R1-79). */
/* ShowCompanyStations is now REAL — m1_station_gui.cpp (R1-86). */
/* REAL — company_cmd.cpp DrawCompanyIcon (toolbar company dropdown icons). */
void DrawCompanyIcon(CompanyID c, int x, int y)
{
	DrawSprite(SPR_COMPANY_ICON, COMPANY_SPRITE_COLOUR(c), x, y);
}
/* Land-info window TU not compiled — centre the main view on the inquired tile. */
void ShowLandInfo(TileIndex tile)
{
	if (tile != INVALID_TILE) ScrollMainWindowToTile(tile);
}
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
void ShowExtraViewportWindow(TileIndex tile)
{
	int i = 0;
	while (FindWindowById(WC_EXTRA_VIEWPORT, i) != nullptr) i++;
	new ExtraViewportWindow(&_extra_viewport_desc, i, tile);
}
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
/* Road exists in this port (road_cmd linked + R1 builds roads); without road.cpp's
 * engine scan, report the basic road type so editor/tool enable checks are honest. */
RoadTypes GetRoadTypes(bool) { return ROADTYPES_ROAD; }

/* ---- DropDownList () — empty menu (build toolbars themselves are still absent) ---- */
DropDownList GetRailTypeDropDownList(bool, bool) { return {}; }
DropDownList GetRoadTypeDropDownList(RoadTramTypes, bool, bool) { return {}; }
DropDownList GetScenRoadTypeDropDownList(RoadTramTypes) { return {}; }
