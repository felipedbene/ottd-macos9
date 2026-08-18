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
 * m1_depotgui_stubs.cpp — link surface for the REAL depot_gui.cpp +
 * build_vehicle_gui.cpp + vehicle_cmd.cpp bundle (R1_REAL_DEPOT_GUI wave).
 *
 * Measured per harness/handoff-train-gui.md: the three TUs probe-compile clean
 * under the full R1 flag set and, against a fresh have.txt (ottd-r1/obj +
 * ottd-b1 + ottd-b2), their combined net link surface is exactly the symbols
 * defined below. The split is the usual one: everything the depot window and
 * the purchase window actually reach at data/draw time is the game's own code,
 * transplanted verbatim (list builders, sorters, the train/roadveh consist
 * painters, the money check, the buy callback); everything that belongs to a
 * subsystem the port does not have (ships, aircraft, groups, refit, NewGRF
 * articulation, order-backup, the query dialog) answers "that does not exist
 * here" instead of pretending.
 *
 * Pattern mirrors: m1_trainstub.cpp (train_cmd.cpp surface) and
 * m1_realveh_stubs.cpp (roadveh_cmd.cpp surface). Several transplants here are
 * lifted from ottd-m1/m1_vguistub.cpp, which stays OUT of the build — if that
 * file ever joins M1_TUS, its copies of DrawRoadVehImage /
 * GenerateVehicleSortList / DrawShipImage / DrawAircraftImage must be guarded
 * out first (this TU owns them now).
 */
#include "stdafx.h"

#include "aircraft.h"
#include "aircraft_cmd.h"
#include "articulated_vehicles.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "depot_cmd.h"
#include "engine_base.h"
#include "engine_gui.h"
#include "gfx_func.h"
#include "group.h"
#include "group_cmd.h"
#include "order_backup.h"
#include "order_base.h"
#include "roadveh.h"
#include "ship.h"
#include "ship_cmd.h"
#include "strings_func.h"
#include "textbuf_gui.h"
#include "train.h"
#include "train_cmd.h"
#include "vehicle_base.h"
#include "vehicle_cmd.h"
#include "vehicle_func.h"
#include "vehicle_gui.h"
#include "vehiclelist.h"
#include "window_gui.h"
#include "zoom_func.h"

#include "table/strings.h"

#include "safeguards.h"

/* ===========================================================================
 * Group 1 — small real transplants. These are on the windows' hot data/draw
 * paths, so they are the game's own bodies, copied verbatim from their home
 * TUs (which stay unlinked because the rest of each TU drags subsystems the
 * port does not have).
 * ======================================================================== */

/* --- vehiclelist.cpp:70 — BuildDepotVehicleList, verbatim -------------------
 * The depot window's data path: which consists and free wagons appear in the
 * matrix. Train wagon logic (articulated part / rear head / free-wagon rows)
 * copied faithfully — train_cmd.cpp is linked, so every Train method is real. */
void BuildDepotVehicleList(VehicleType type, TileIndex tile, VehicleList *engines, VehicleList *wagons, bool individual_wagons)
{
	engines->clear();
	if (wagons != nullptr && wagons != engines) wagons->clear();

	for (const Vehicle *v : Vehicle::Iterate()) {
		/* General tests for all vehicle types */
		if (v->type != type) continue;
		if (v->tile != tile) continue;

		switch (type) {
			case VEH_TRAIN: {
				const Train *t = Train::From(v);
				if (t->IsArticulatedPart() || t->IsRearDualheaded()) continue;
				if (!t->IsInDepot()) continue;
				if (wagons != nullptr && t->First()->IsFreeWagon()) {
					if (individual_wagons || t->IsFreeWagon()) wagons->push_back(t);
					continue;
				}
				if (!t->IsPrimaryVehicle()) continue;
				break;
			}

			default:
				if (!v->IsPrimaryVehicle()) continue;
				if (!v->IsInDepot()) continue;
				break;
		}

		engines->push_back(v);
	}

	/* Ensure the lists are not wasting too much space. If the lists are fresh
	 * (i.e. built within a command) then this will actually do nothing. */
	engines->shrink_to_fit();
	if (wagons != nullptr && wagons != engines) wagons->shrink_to_fit();
}

/* --- vehiclelist.cpp:113 — GenerateVehicleSortList ---------------------------
 * Faithful for every arm the port can reach (VL_STANDARD company+type,
 * VL_STATION_LIST, VL_SHARED_ORDERS, VL_DEPOT_LIST). The single adaptation is
 * VL_GROUP_LIST: upstream filters through GroupIsInGroup() from group_cmd.cpp,
 * which is not linked because there is no group pool — with no groups, a named
 * group's list is legitimately empty (truthful, not a failure), and ALL_GROUP
 * still falls through to the whole company exactly as upstream does. Same
 * adaptation as the (uncompiled) m1_vguistub.cpp copy this supersedes. */
bool GenerateVehicleSortList(VehicleList *list, const VehicleListIdentifier &vli)
{
	list->clear();

	switch (vli.type) {
		case VL_STATION_LIST:
			for (const Vehicle *v : Vehicle::Iterate()) {
				if (v->type == vli.vtype && v->IsPrimaryVehicle()) {
					for (const Order *order : v->Orders()) {
						if ((order->IsType(OT_GOTO_STATION) || order->IsType(OT_GOTO_WAYPOINT) || order->IsType(OT_IMPLICIT))
								&& order->GetDestination() == vli.index) {
							list->push_back(v);
							break;
						}
					}
				}
			}
			break;

		case VL_SHARED_ORDERS: {
			/* Add all vehicles from this vehicle's shared order list */
			const Vehicle *v = Vehicle::GetIfValid(vli.index);
			if (v == nullptr || v->type != vli.vtype || !v->IsPrimaryVehicle()) return false;

			for (; v != nullptr; v = v->NextShared()) {
				list->push_back(v);
			}
			break;
		}

		case VL_GROUP_LIST:
			/* No group pool exists in the port: every vehicle stays in DEFAULT_GROUP,
			 * so any specific group is empty. ALL_GROUP is the whole company. */
			if (vli.index != ALL_GROUP) break;
			FALLTHROUGH;

		case VL_STANDARD:
			for (const Vehicle *v : Vehicle::Iterate()) {
				if (v->type == vli.vtype && v->owner == vli.company && v->IsPrimaryVehicle()) {
					list->push_back(v);
				}
			}
			break;

		case VL_DEPOT_LIST:
			for (const Vehicle *v : Vehicle::Iterate()) {
				if (v->type == vli.vtype && v->IsPrimaryVehicle()) {
					for (const Order *order : v->Orders()) {
						if (order->IsType(OT_GOTO_DEPOT) && !(order->GetDepotActionType() & ODATFB_NEAREST_DEPOT) && order->GetDestination() == vli.index) {
							list->push_back(v);
							break;
						}
					}
				}
			}
			break;

		default: return false;
	}

	list->shrink_to_fit();
	return true;
}

/* --- vehicle_gui.cpp:403 — DepotSortList, verbatim ---------------------------
 * The comparator (vehicle_gui.cpp:1326) is file-static upstream; brought along
 * as a local static so the mangled surface stays exactly one symbol. */
static bool VehicleNumberSorter(const Vehicle * const &a, const Vehicle * const &b)
{
	return a->unitnumber < b->unitnumber;
}

void DepotSortList(VehicleList *list)
{
	if (list->size() < 2) return;
	std::sort(list->begin(), list->end(), &VehicleNumberSorter);
}

/* --- vehicle_gui.cpp:172 — GetUnitNumberDigits, verbatim ---------------------
 * Its helper CountDigitsForAllocatingSpace (vehicle_gui.cpp:153) has extern
 * linkage upstream but no header declaration; kept file-static here (same
 * choice m1_vehicle_list_gui.cpp already made) so the real vehicle_gui.cpp can
 * link later without a duplicate-definition collision. */
static uint CountDigitsForAllocatingSpace(uint number)
{
	if (number >= 10000) return 5;
	if (number >= 1000) return 4;
	if (number >= 100) return 3;

	/*
	 * When the smallest unit number is less than 10, it is
	 * quite likely that it will expand to become more than
	 * 10 quite soon.
	 */
	return 2;
}

uint GetUnitNumberDigits(VehicleList &vehicles)
{
	uint unitnumber = 0;
	for (const Vehicle *v : vehicles) {
		unitnumber = std::max<uint>(unitnumber, v->unitnumber);
	}

	return CountDigitsForAllocatingSpace(unitnumber);
}

/* --- train_gui.cpp:61/94 — HighlightDragPosition + DrawTrainImage ------------
 * The depot matrix's train painter, verbatim from the real train_gui.cpp
 * (NOT the empty stub in m1_vguistub.cpp:564 — that stub predates the real
 * train stack; today every callee is linked: Train::GetDisplayImageWidth and
 * Train::GetImage live in train_cmd.o, GetVehiclePalette in vehicle.o,
 * _cursor/_colour_gradient/GfxFillRect/DrawFrameRect in the gfx/widget TUs).
 * The drag highlight comes along because dragging wagons inside the depot
 * window is core depot behaviour, not a nicety. */
static int HighlightDragPosition(int px, int max_width, int y, VehicleID selection, bool chain)
{
	bool rtl = _current_text_dir == TD_RTL;

	assert(selection != INVALID_VEHICLE);
	int dragged_width = 0;
	for (Train *t = Train::Get(selection); t != nullptr; t = chain ? t->Next() : (t->HasArticulatedPart() ? t->GetNextArticulatedPart() : nullptr)) {
		dragged_width += t->GetDisplayImageWidth(nullptr);
	}

	int drag_hlight_left = rtl ? std::max(px - dragged_width + 1, 0) : px;
	int drag_hlight_right = rtl ? px : std::min(px + dragged_width, max_width) - 1;
	int drag_hlight_width = std::max(drag_hlight_right - drag_hlight_left + 1, 0);

	if (drag_hlight_width > 0) {
		int height = ScaleSpriteTrad(12);
		int top = y - height / 2;
		Rect r = {drag_hlight_left, top, drag_hlight_right, top + height - 1};
		/* Sprite-scaling is used here as the area is from sprite size */
		GfxFillRect(r.Shrink(ScaleSpriteTrad(1)), _colour_gradient[COLOUR_GREY][7]);
	}

	return drag_hlight_width;
}

void DrawTrainImage(const Train *v, const Rect &r, VehicleID selection, EngineImageType image_type, int skip, VehicleID drag_dest)
{
	bool rtl = _current_text_dir == TD_RTL;
	Direction dir = rtl ? DIR_E : DIR_W;

	DrawPixelInfo tmp_dpi, *old_dpi;
	/* Position of highlight box */
	int highlight_l = 0;
	int highlight_r = 0;
	int max_width = r.Width();

	if (!FillDrawPixelInfo(&tmp_dpi, r.left, r.top, r.Width(), r.Height())) return;

	old_dpi = _cur_dpi;
	_cur_dpi = &tmp_dpi;

	int px = rtl ? max_width + skip : -skip;
	int y = r.Height() / 2;
	bool sel_articulated = false;
	bool dragging = (drag_dest != INVALID_VEHICLE);
	bool drag_at_end_of_train = (drag_dest == v->index); // Head index is used to mark dragging at end of train.
	for (; v != nullptr && (rtl ? px > 0 : px < max_width); v = v->Next()) {
		if (dragging && !drag_at_end_of_train && drag_dest == v->index) {
			/* Highlight the drag-and-drop destination inside the train. */
			int drag_hlight_width = HighlightDragPosition(px, max_width, y, selection, _cursor.vehchain);
			px += rtl ? -drag_hlight_width : drag_hlight_width;
		}

		Point offset;
		int width = Train::From(v)->GetDisplayImageWidth(&offset);

		if (rtl ? px + width > 0 : px - width < max_width) {
			PaletteID pal = (v->vehstatus & VS_CRASHED) ? PALETTE_CRASH : GetVehiclePalette(v);
			VehicleSpriteSeq seq;
			v->GetImage(dir, image_type, &seq);
			seq.Draw(px + (rtl ? -offset.x : offset.x), y + offset.y, pal, (v->vehstatus & VS_CRASHED) != 0);
		}

		if (!v->IsArticulatedPart()) sel_articulated = false;

		if (v->index == selection) {
			/* Set the highlight position */
			highlight_l = rtl ? px - width : px;
			highlight_r = rtl ? px - 1 : px + width - 1;
			sel_articulated = true;
		} else if ((_cursor.vehchain && highlight_r != 0) || sel_articulated) {
			if (rtl) {
				highlight_l -= width;
			} else {
				highlight_r += width;
			}
		}

		px += rtl ? -width : width;
	}

	if (dragging && drag_at_end_of_train) {
		/* Highlight the drag-and-drop destination at the end of the train. */
		HighlightDragPosition(px, max_width, y, selection, _cursor.vehchain);
	}

	_cur_dpi = old_dpi;

	if (highlight_l != highlight_r) {
		/* Draw the highlight. Now done after drawing all the engines, as
		 * the next engine after the highlight could overlap it. */
		int height = ScaleSpriteTrad(12);
		Rect hr = {highlight_l, 0, highlight_r, height - 1};
		DrawFrameRect(hr.Translate(r.left, CenterBounds(r.top, r.bottom, height)).Expand(WidgetDimensions::scaled.bevel), COLOUR_WHITE, FR_BORDERONLY);
	}
}

/* --- roadveh_gui.cpp — DrawRoadVehImage, verbatim ----------------------------
 * Lifted from the already-transplanted copy in ottd-m1/m1_vguistub.cpp:298
 * (which is not compiled — this TU owns the symbol now). Draws the same bus
 * the viewport draws; every callee is linked (roadveh_cmd.o / vehicle.o). */
void DrawRoadVehImage(const Vehicle *v, const Rect &r, VehicleID selection, EngineImageType image_type, int skip)
{
	bool rtl = _current_text_dir == TD_RTL;
	Direction dir = rtl ? DIR_E : DIR_W;
	const RoadVehicle *u = RoadVehicle::From(v);

	DrawPixelInfo tmp_dpi, *old_dpi;
	int max_width = r.Width();

	if (!FillDrawPixelInfo(&tmp_dpi, r.left, r.top, r.Width(), r.Height())) return;

	old_dpi = _cur_dpi;
	_cur_dpi = &tmp_dpi;

	int px = rtl ? max_width + skip : -skip;
	int y = r.Height() / 2;
	for (; u != nullptr && (rtl ? px > 0 : px < max_width); u = u->Next())
	{
		Point offset;
		int width = u->GetDisplayImageWidth(&offset);

		if (rtl ? px + width > 0 : px - width < max_width) {
			PaletteID pal = (u->vehstatus & VS_CRASHED) ? PALETTE_CRASH : GetVehiclePalette(u);
			VehicleSpriteSeq seq;
			u->GetImage(dir, image_type, &seq);
			seq.Draw(px + (rtl ? -offset.x : offset.x), y + offset.y, pal, (u->vehstatus & VS_CRASHED) != 0);
		}

		px += rtl ? -width : width;
	}

	_cur_dpi = old_dpi;

	if (v->index == selection) {
		int height = ScaleSpriteTrad(12);
		Rect hr = {(rtl ? px : 0), 0, (rtl ? max_width : px) - 1, height - 1};
		DrawFrameRect(hr.Translate(r.left, CenterBounds(r.top, r.bottom, height)).Expand(WidgetDimensions::scaled.bevel), COLOUR_WHITE, FR_BORDERONLY);
	}
}

/* --- engine_gui.cpp:327/340 — EngList_Sort / EngList_SortPartial, verbatim -- */
void EngList_Sort(GUIEngineList *el, EngList_SortTypeFunction compare)
{
	if (el->size() < 2) return;
	std::sort(el->begin(), el->end(), compare);
}

void EngList_SortPartial(GUIEngineList *el, EngList_SortTypeFunction compare, size_t begin, size_t num_items)
{
	if (num_items < 2) return;
	assert(begin < el->size());
	assert(begin + num_items <= el->size());
	std::sort(el->begin() + begin, el->begin() + begin + num_items, compare);
}

/* --- engine_gui.cpp:297 — DrawVehicleEngine ----------------------------------
 * The purchase list's icon painter. Train and road arms are the real calls
 * (DrawTrainEngine / DrawRoadVehEngine are ALREADY LINKED — they live in
 * train_cmd.o / roadveh_cmd.o, not in the gui TUs). The ship/aircraft arms are
 * empty: R1 has no ships or aircraft, so there is no engine icon to draw and
 * dragging ship_cmd/aircraft_cmd for one would be a lie. */
void DrawVehicleEngine(int left, int right, int preferred_x, int y, EngineID engine, PaletteID pal, EngineImageType image_type)
{
	const Engine *e = Engine::Get(engine);

	switch (e->type) {
		case VEH_TRAIN:
			DrawTrainEngine(left, right, preferred_x, y, engine, pal, image_type);
			break;

		case VEH_ROAD:
			DrawRoadVehEngine(left, right, preferred_x, y, engine, pal, image_type);
			break;

		case VEH_SHIP:
		case VEH_AIRCRAFT:
			/* No ships or aircraft in R1: nothing to draw. */
			break;

		default: NOT_REACHED();
	}
}

/* --- company_cmd.cpp:200 — CheckCompanyHasMoney, verbatim --------------------
 * The Buy button's wallet check. Company pool and _current_company are real
 * (m1_company.cpp / the R1 economy), so this is the game's own body. */
bool CheckCompanyHasMoney(CommandCost &cost)
{
	if (cost.GetCost() > 0) {
		const Company *c = Company::GetIfValid(_current_company);
		if (c != nullptr && cost.GetCost() > c->money) {
			SetDParam(0, cost.GetCost());
			cost.MakeError(STR_ERROR_NOT_ENOUGH_CASH_REQUIRES_CURRENCY);
			return false;
		}
	}
	return true;
}

/* --- vehicle_gui.cpp:3359 — CcBuildPrimaryVehicle, verbatim ------------------
 * The post-Buy callback. ShowVehicleViewWindow(Vehicle const*) is already in
 * have.txt (linked view-window lane), so the real body costs nothing: buying
 * an engine pops its vehicle view exactly as upstream does. */
void CcBuildPrimaryVehicle(Commands, const CommandCost &result, VehicleID new_veh_id, uint, uint16, CargoArray)
{
	if (result.Failed()) return;

	const Vehicle *v = Vehicle::Get(new_veh_id);
	ShowVehicleViewWindow(v);
}

/* ===========================================================================
 * Group 2 — honest-absence stubs. Each one names the subsystem that does not
 * exist in the port and answers accordingly.
 * ======================================================================== */

/* depot_cmd.cpp is not linked (no depot pool naming): renaming a depot has no
 * storage to land in, so the command honestly fails. */
CommandCost CmdRenameDepot(DoCommandFlag, DepotID, const std::string &)
{
	return CMD_ERROR;
}

/* group_cmd.cpp is not linked (no group pool): there is no group to add a
 * vehicle to, so the command honestly fails. */
std::tuple<CommandCost, GroupID> CmdAddVehicleGroup(DoCommandFlag, GroupID, VehicleID, bool)
{
	return { CMD_ERROR, INVALID_GROUP };
}

/* aircraft_cmd.cpp is not linked: no aircraft pool, no hangars — building an
 * aircraft cannot succeed. vehicle_cmd.cpp's build dispatch calls this arm. */
CommandCost CmdBuildAircraft(DoCommandFlag, TileIndex, const Engine *, Vehicle **)
{
	return CMD_ERROR;
}

/* ship_cmd.cpp is not linked: no ships — same honest refusal as aircraft. */
CommandCost CmdBuildShip(DoCommandFlag, TileIndex, const Engine *, Vehicle **)
{
	return CMD_ERROR;
}

/* ship_gui.cpp is not linked and no ship can exist: nothing to draw. */
void DrawShipImage(const Vehicle *, const Rect &, VehicleID, EngineImageType)
{
}

/* aircraft_gui.cpp is not linked and no aircraft can exist: nothing to draw. */
void DrawAircraftImage(const Vehicle *, const Rect &, VehicleID, EngineImageType)
{
}

/* ship_cmd.cpp is not linked: report a fixed nominal cell so the depot
 * window's ship page (unreachable in R1) still lays out sanely. */
void GetShipSpriteSize(EngineID, uint &width, uint &height, int &xoffs, int &yoffs, EngineImageType)
{
	width = 32;
	height = 16;
	xoffs = 0;
	yoffs = 0;
}

/* aircraft_cmd.cpp is not linked: same fixed nominal cell as ships. */
void GetAircraftSpriteSize(EngineID, uint &width, uint &height, int &xoffs, int &yoffs, EngineImageType)
{
	width = 32;
	height = 16;
	xoffs = 0;
	yoffs = 0;
}

/* The drag ghost that replaces the mouse cursor while dragging a vehicle is
 * cosmetic only; the drag itself works without it. */
void SetMouseCursorVehicle(const Vehicle *, EngineImageType)
{
}

/* vehicle_gui.cpp's depot-scoped list window is not linked; forward to the
 * 2-arg ShowVehicleListWindow(Owner, VehicleType) that m1_vehicle_list_gui.o
 * already defines (different mangled name — no collision). The depot tile
 * filter is dropped: the company-wide list is the closest window that exists. */
void ShowVehicleListWindow(CompanyID company, VehicleType vehicle_type, TileIndex)
{
	ShowVehicleListWindow(company, vehicle_type);
}

/* misc_gui.cpp's confirmation dialog is not linked. Deliberately inert: the
 * callback is NOT invoked, because auto-confirming "yes" would silently
 * mass-sell every vehicle in the depot the moment Sell All is clicked. With no
 * dialog, Sell All is honestly unreachable instead of destructively implicit. */
void ShowQuery(StringID, StringID, Window *, QueryCallbackProc *)
{
}

/* order_backup.cpp is not linked (no order-backup pool): nothing was ever
 * backed up, so there is nothing to reset. Sibling of the OrderBackup::Backup
 * no-op in m1_trainstub.cpp:140 (which does NOT define Reset/Restore). */
/* static */ void OrderBackup::Reset(TileIndex, bool)
{
}

/* Same absent pool: nothing was backed up, so nothing can be restored. */
/* static */ void OrderBackup::Restore(Vehicle *, uint32)
{
}

/* train_gui.cpp:30's real body auto-moves a freshly bought wagon to the end of
 * the depot's lone stopped loco via Command<CMD_MOVE_RAIL_VEHICLE>::Post — a
 * network-command dispatch this TU must not drag in. The wagon still appears
 * in the depot's free-wagon row (BuildDepotVehicleList above is real); the
 * auto-attach is a nicety deferred to a later lane. */
void CcBuildWagon(Commands, const CommandCost &, VehicleID, uint, uint16, CargoArray, TileIndex, EngineID, bool, CargoID, ClientID)
{
}

/* group_cmd.cpp is not linked (no group pool): no group owns any engine, so
 * the purchase list's owned-count column truthfully shows 0. */
uint GetGroupNumEngines(CompanyID, GroupID, EngineID)
{
	return 0;
}

/* The refit subsystem is absent port-wide: no refit options exist to list.
 * Returning y unchanged means "nothing drawn here" to the caller's layout. */
uint ShowRefitOptionsList(int, int, int y, EngineID)
{
	return y;
}

/* articulated_vehicles.cpp is not linked (no NewGRF articulation): an engine
 * IS its only part, so its capacity is its own base capacity. Cheapest real
 * accessor: Engine::GetDisplayDefaultCapacity → the real DetermineCapacity in
 * the linked engine.o. */
CargoArray GetCapacityOfArticulatedParts(EngineID engine)
{
	CargoArray capacity;
	const Engine *e = Engine::Get(engine);
	CargoID cargo = e->GetDefaultCargoType();
	if (cargo != CT_INVALID) capacity[cargo] = e->GetDisplayDefaultCapacity();
	return capacity;
}

/* engine_gui.cpp:164, verbatim — it just sums the CargoArray above, so with
 * the no-articulation answer underneath it returns the base capacity. */
uint GetTotalCapacityOfArticulatedParts(EngineID engine)
{
	CargoArray cap = GetCapacityOfArticulatedParts(engine);
	return cap.GetSum<uint>();
}

/* No NewGRF articulation: no hidden parts exist that could take another cargo. */
bool IsArticulatedVehicleRefittable(EngineID)
{
	return false;
}

/* No NewGRF articulation: every engine is a single part, so zero EXTRA parts. */
uint CountArticulatedParts(EngineID, bool)
{
	return 0;
}

/* No NewGRF cargo subtypes exist in the port: subtype 0 is the only subtype. */
byte GetBestFittingSubType(Vehicle *, Vehicle *, CargoID)
{
	return 0;
}

/* No aircraft pool: there is no aircraft cache to update. */
void UpdateAircraftCache(Aircraft *, bool)
{
}
