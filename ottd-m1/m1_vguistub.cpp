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
 * m1_vguistub.cpp — the link surface of the game's OWN vehicle_gui.cpp.
 *
 * Until now R1 shipped m1_vehicle_list_gui.cpp: a hand-trimmed re-implementation of
 * the vehicle list. It was written when the vehicle stack itself was fake, and it
 * has drifted — its sort comparators, its "Manage list" dropdown and its image
 * column are ours, not the game's. Now that roadveh_cmd.cpp / vehicle.cpp /
 * engine.cpp / order_cmd.cpp are the real thing, the hand copy is the only lie left
 * in that window. Compiling the real vehicle_gui.cpp removes it, and this file is
 * the price: the ~30 symbols that TU expects from the five neighbours it was never
 * meant to be separated from (vehiclelist.cpp, vehicle_cmd.cpp, roadveh_gui.cpp,
 * train_gui.cpp, and the group/timetable/order/build windows).
 *
 * The split between real and stub here is not "easy vs hard", it is REACHABLE vs
 * NOT. Everything the list window actually touches at draw time or data time is
 * the game's own code, transplanted verbatim: GenerateVehicleSortList decides which
 * vehicles appear, DrawRoadVehImage paints the sprite column, and the mass
 * start/stop pair behind the "Manage list" dropdown really does stop the buses.
 * The rest is absent by construction — R1 has no aircraft, ships, player trains,
 * depots, groups, autoreplace, refit or NewGRF — so those return the value that
 * means "that subsystem does not exist here" instead of pretending.
 *
 * On the image column specifically: DrawRoadVehImage is the one place where the
 * old hand copy took a shortcut, because the honest path looked like it dragged
 * the whole engine/NewGRF sprite cascade. It does not. GetVehiclePalette and
 * RoadVehicle::GetImage are already linked (vehicle.cpp / roadveh_cmd.cpp), so the
 * verbatim function below costs nothing extra and draws the same bus the viewport
 * draws — which is the point of having a real vehicle stack at all.
 *
 * Keep this file symbol-disjoint from the other m1_*stub*.cpp files: each owns its
 * own slice of the missing game, and duplicate definitions break the link.
 */
#include "stdafx.h"

#include "aircraft.h"
#include "airport.h"        /* STARTTAKEOFF / TERM7, for the (dead) aircraft arm */
#include "autoreplace_gui.h"
#include "command_func.h"
#include "depot_cmd.h"
#include "gfx_func.h"
#include "group_gui.h"
#include "group_type.h"
#include "gui.h"
#include "newgrf_engine.h"
#include "newgrf_text.h"
#include "news_func.h"
#include "order_base.h"
#include "roadveh.h"
#include "string_func.h"
#include "strings_func.h"
#include "texteff.hpp"
#include "timetable.h"
#include "train.h"
#include "train_cmd.h"
#include "vehicle_base.h"
#include "vehicle_cmd.h"
#include "vehicle_func.h"
#include "vehicle_gui.h"
#include "vehiclelist.h"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"

#include "widgets/vehicle_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/* Declared privately at the top of vehicle_gui.cpp (no header owns them) — matched
 * verbatim so the mangled names line up. */
extern int GetTrainDetailsWndVScroll(VehicleID veh_id, TrainDetailsWindowTabs det_tab);
extern void DrawTrainDetails(const Train *v, const Rect &r, int vscroll_pos, uint16 vscroll_cap, TrainDetailsWindowTabs det_tab);
extern void DrawRoadVehDetails(const Vehicle *v, const Rect &r);
extern void DrawShipDetails(const Vehicle *v, const Rect &r);
extern void DrawAircraftDetails(const Aircraft *v, const Rect &r);

/* --- Error-message tables (vehicle_cmd.cpp) ---------------------------------
 * Pure data, copied verbatim. GetCmdRefitVehMsg()/GetCmdSendToDepotMsg() index
 * these by VehicleType, so they must stay four entries wide even though only
 * VEH_ROAD can ever be the subject. Their two siblings (_veh_build_msg_table,
 * _veh_sell_msg_table) are already provided elsewhere in the port. */
const StringID _veh_refit_msg_table[] = {
	STR_ERROR_CAN_T_REFIT_TRAIN,
	STR_ERROR_CAN_T_REFIT_ROAD_VEHICLE,
	STR_ERROR_CAN_T_REFIT_SHIP,
	STR_ERROR_CAN_T_REFIT_AIRCRAFT,
};

const StringID _send_to_depot_msg_table[] = {
	STR_ERROR_CAN_T_SEND_TRAIN_TO_DEPOT,
	STR_ERROR_CAN_T_SEND_ROAD_VEHICLE_TO_DEPOT,
	STR_ERROR_CAN_T_SEND_SHIP_TO_DEPOT,
	STR_ERROR_CAN_T_SEND_AIRCRAFT_TO_HANGAR,
};

/* --- vehiclelist.cpp --------------------------------------------------------
 * This is the list window's data path, so it is the game's own code. Only the
 * VL_GROUP_LIST arm differs: GroupIsInGroup() lives in group_cmd.cpp, which the
 * port does not link because there is no group pool. With no groups, no vehicle
 * can be a member of one, so a named group's list is legitimately empty — that is
 * the truthful answer, not a failure. ALL_GROUP still means "everything", so it
 * falls through to VL_STANDARD exactly as upstream does.
 *
 * UnPack() normally delegates to UnpackIfValid(), but nothing in the port defines
 * that member — vehiclelist.cpp is not linked and only Pack() was ever hand-provided.
 * Calling it would trade one missing symbol for another, so the four-field decode is
 * spelled out here instead, bit-for-bit identical to upstream's. */
/* static */ VehicleListIdentifier VehicleListIdentifier::UnPack(uint32 data)
{
	VehicleListIdentifier result;
	byte c         = GB(data, 28, 4);
	result.company = c == 0xF ? OWNER_NONE : (CompanyID)c;
	result.type    = (VehicleListType)GB(data, 23, 3);
	result.vtype   = (VehicleType)GB(data, 26, 2);
	result.index   = GB(data, 0, 20);
	assert(result.type < VLT_END);
	return result;
}

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

/* --- roadveh_gui.cpp --------------------------------------------------------
 * Both functions verbatim. The road vehicle is the only vehicle a player owns in
 * R1, so this is the one vehicle type whose GUI deserves the real thing: the list
 * row draws the same sprite sequence the viewport draws, and the details pane
 * reports the cargo the real CargoList is holding. Every callee below is already
 * linked (GetVehiclePalette, RoadVehicle::GetImage/GetDisplayImageWidth,
 * VehicleSpriteSeq::Draw, FillDrawPixelInfo, GetCargoSubtypeText from
 * vehicle_gui.cpp itself) — no NewGRF sprite cascade is dragged in. */
void DrawRoadVehDetails(const Vehicle *v, const Rect &r)
{
	int y = r.top + (v->HasArticulatedPart() ? ScaleSpriteTrad(15) : 0); // Draw the first line below the sprite of an articulated RV instead of after it.
	StringID str;
	Money feeder_share = 0;

	SetDParam(0, PackEngineNameDParam(v->engine_type, EngineNameContext::VehicleDetails));
	SetDParam(1, v->build_year);
	SetDParam(2, v->value);
	DrawString(r.left, r.right, y, STR_VEHICLE_INFO_BUILT_VALUE);
	y += FONT_HEIGHT_NORMAL;

	if (v->HasArticulatedPart()) {
		CargoArray max_cargo;
		StringID subtype_text[NUM_CARGO];
		char capacity[512];

		memset(subtype_text, 0, sizeof(subtype_text));

		for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
			max_cargo[u->cargo_type] += u->cargo_cap;
			if (u->cargo_cap > 0) {
				StringID text = GetCargoSubtypeText(u);
				if (text != STR_EMPTY) subtype_text[u->cargo_type] = text;
			}
		}

		GetString(capacity, STR_VEHICLE_DETAILS_TRAIN_ARTICULATED_RV_CAPACITY, lastof(capacity));

		bool first = true;
		for (CargoID i = 0; i < NUM_CARGO; i++) {
			if (max_cargo[i] > 0) {
				char buffer[128];

				SetDParam(0, i);
				SetDParam(1, max_cargo[i]);
				GetString(buffer, STR_JUST_CARGO, lastof(buffer));

				if (!first) strecat(capacity, ", ", lastof(capacity));
				strecat(capacity, buffer, lastof(capacity));

				if (subtype_text[i] != 0) {
					GetString(buffer, subtype_text[i], lastof(buffer));
					strecat(capacity, buffer, lastof(capacity));
				}

				first = false;
			}
		}

		DrawString(r.left, r.right, y, capacity, TC_BLUE);
		y += FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_normal;

		for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
			if (u->cargo_cap == 0) continue;

			str = STR_VEHICLE_DETAILS_CARGO_EMPTY;
			if (u->cargo.StoredCount() > 0) {
				SetDParam(0, u->cargo_type);
				SetDParam(1, u->cargo.StoredCount());
				SetDParam(2, u->cargo.Source());
				str = STR_VEHICLE_DETAILS_CARGO_FROM;
				feeder_share += u->cargo.FeederShare();
			}
			DrawString(r.left, r.right, y, str);
			y += FONT_HEIGHT_NORMAL;
		}
		y += WidgetDimensions::scaled.vsep_normal;
	} else {
		SetDParam(0, v->cargo_type);
		SetDParam(1, v->cargo_cap);
		SetDParam(4, GetCargoSubtypeText(v));
		DrawString(r.left, r.right, y, STR_VEHICLE_INFO_CAPACITY);
		y += FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_normal;

		str = STR_VEHICLE_DETAILS_CARGO_EMPTY;
		if (v->cargo.StoredCount() > 0) {
			SetDParam(0, v->cargo_type);
			SetDParam(1, v->cargo.StoredCount());
			SetDParam(2, v->cargo.Source());
			str = STR_VEHICLE_DETAILS_CARGO_FROM;
			feeder_share += v->cargo.FeederShare();
		}
		DrawString(r.left, r.right, y, str);
		y += FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_normal;
	}

	/* Draw Transfer credits text */
	SetDParam(0, feeder_share);
	DrawString(r.left, r.right, y, STR_VEHICLE_INFO_FEEDER_CARGO_VALUE);
}

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

/* --- vehicle_cmd.cpp: the commands the list window can actually reach ---------
 * The "Manage list" dropdown's start-all / stop-all really works, so both halves
 * are the game's own code. CmdStartStopVehicle is verbatim (CheckOwnership,
 * GetVehicleCallback and DeleteVehicleNews are all linked; the train/aircraft arms
 * are unreachable but cost nothing and keep the function honest if a second
 * vehicle type ever lands). CmdMassStartStopVehicle drops only its depot arm:
 * BuildDepotVehicleList and IsDepotTile belong to a depot subsystem the port does
 * not have, and a caller asking to start "every vehicle in that depot" is asking
 * about something that cannot exist — so it fails rather than lies. */
CommandCost CmdStartStopVehicle(DoCommandFlag flags, VehicleID veh_id, bool evaluate_startstop_cb)
{
	/* Disable the effect of p2 bit 0, when DC_AUTOREPLACE is not set */
	if ((flags & DC_AUTOREPLACE) == 0) evaluate_startstop_cb = true;

	Vehicle *v = Vehicle::GetIfValid(veh_id);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->vehstatus & VS_CRASHED) return_cmd_error(STR_ERROR_VEHICLE_IS_DESTROYED);

	switch (v->type) {
		case VEH_TRAIN:
			if ((v->vehstatus & VS_STOPPED) && Train::From(v)->gcache.cached_power == 0) return_cmd_error(STR_ERROR_TRAIN_START_NO_POWER);
			break;

		case VEH_SHIP:
		case VEH_ROAD:
			break;

		case VEH_AIRCRAFT: {
			Aircraft *a = Aircraft::From(v);
			/* cannot stop airplane when in flight, or when taking off / landing */
			if (a->state >= STARTTAKEOFF && a->state < TERM7) return_cmd_error(STR_ERROR_AIRCRAFT_IS_IN_FLIGHT);
			if (HasBit(a->flags, VAF_HELI_DIRECT_DESCENT)) return_cmd_error(STR_ERROR_AIRCRAFT_IS_IN_FLIGHT);
			break;
		}

		default: return CMD_ERROR;
	}

	if (evaluate_startstop_cb) {
		/* Check if this vehicle can be started/stopped. Failure means 'allow'. */
		uint16 callback = GetVehicleCallback(CBID_VEHICLE_START_STOP_CHECK, 0, 0, v->engine_type, v);
		StringID error = STR_NULL;
		if (callback != CALLBACK_FAILED) {
			if (v->GetGRF()->grf_version < 8) {
				/* 8 bit result 0xFF means 'allow' */
				if (callback < 0x400 && GB(callback, 0, 8) != 0xFF) error = GetGRFStringID(v->GetGRFID(), 0xD000 + callback);
			} else {
				if (callback < 0x400) {
					error = GetGRFStringID(v->GetGRFID(), 0xD000 + callback);
				} else {
					switch (callback) {
						case 0x400: // allow
							break;

						default: // unknown reason -> disallow
							error = STR_ERROR_INCOMPATIBLE_RAIL_TYPES;
							break;
					}
				}
			}
		}
		if (error != STR_NULL) return_cmd_error(error);
	}

	if (flags & DC_EXEC) {
		if (v->IsStoppedInDepot() && (flags & DC_AUTOREPLACE) == 0) DeleteVehicleNews(veh_id, STR_NEWS_TRAIN_IS_WAITING + v->type);

		v->vehstatus ^= VS_STOPPED;
		if (v->type != VEH_TRAIN) v->cur_speed = 0; // trains can stop 'slowly'
		v->MarkDirty();
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		SetWindowDirty(WC_VEHICLE_DEPOT, v->tile);
		SetWindowClassesDirty(GetWindowClassForVehicleType(v->type));
		InvalidateWindowData(WC_VEHICLE_VIEW, v->index);
	}
	return CommandCost();
}

CommandCost CmdMassStartStopVehicle(DoCommandFlag flags, TileIndex, bool do_start, bool vehicle_list_window, const VehicleListIdentifier &vli)
{
	VehicleList list;

	if (!vli.Valid()) return CMD_ERROR;
	if (!IsCompanyBuildableVehicleType(vli.vtype)) return CMD_ERROR;

	/* Upstream's other caller is a depot window; the port has no depots. */
	if (!vehicle_list_window) return CMD_ERROR;
	if (!GenerateVehicleSortList(&list, vli)) return CMD_ERROR;

	for (uint i = 0; i < list.size(); i++) {
		const Vehicle *v = list[i];

		if (!!(v->vehstatus & VS_STOPPED) != do_start) continue;

		/* Just try and don't care if some vehicle's can't be stopped. */
		Command<CMD_START_STOP_VEHICLE>::Do(flags, v->index, false);
	}

	return CommandCost();
}

/* File-local copy of vehicle_cmd.cpp's helper (it has no header, and exporting a
 * second definition of it would collide with the real TU if it is ever linked). */
static bool VGuiIsUniqueVehicleName(const std::string &name)
{
	for (const Vehicle *v : Vehicle::Iterate()) {
		if (!v->name.empty() && v->name == name) return false;
	}

	return true;
}

/* Verbatim. The name a bus carries is shown in the list window's caption column,
 * so the rename really has to take effect there. */
CommandCost CmdRenameVehicle(DoCommandFlag flags, VehicleID veh_id, const std::string &text)
{
	Vehicle *v = Vehicle::GetIfValid(veh_id);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	bool reset = text.empty();

	if (!reset) {
		if (Utf8StringLength(text) >= MAX_LENGTH_VEHICLE_NAME_CHARS) return CMD_ERROR;
		if (!(flags & DC_AUTOREPLACE) && !VGuiIsUniqueVehicleName(text)) return_cmd_error(STR_ERROR_NAME_MUST_BE_UNIQUE);
	}

	if (flags & DC_EXEC) {
		if (reset) {
			v->name.clear();
		} else {
			v->name = text;
		}
		InvalidateWindowClassesData(GetWindowClassForVehicleType(v->type), 1);
		MarkWholeScreenDirty();
	}

	return CommandCost();
}

/* --- Commands whose subsystem does not exist --------------------------------
 * Servicing needs a depot to service in and CompanyServiceInterval() from
 * company_cmd.cpp; depot orders need depots; cloning needs the build window and
 * the refit machinery; forcing a train through a signal needs trains and signals.
 * None of the four can be satisfied here, and CMD_ERROR is how a command says so
 * — the caller already puts the failure in front of the player. */
CommandCost CmdChangeServiceInt(DoCommandFlag, VehicleID, uint16, bool, bool)
{
	return CMD_ERROR;
}

CommandCost CmdSendVehicleToDepot(DoCommandFlag, VehicleID, DepotCommand, const VehicleListIdentifier &)
{
	return CMD_ERROR;
}

std::tuple<CommandCost, VehicleID> CmdCloneVehicle(DoCommandFlag, TileIndex, VehicleID, bool)
{
	return { CMD_ERROR, INVALID_VEHICLE };
}

CommandCost CmdForceTrainProceed(DoCommandFlag, VehicleID)
{
	return CMD_ERROR;
}

/* The clone command can never succeed, so its callback can never be invoked. */
void CcCloneVehicle(Commands, const CommandCost &, VehicleID)
{
}

/* --- Windows that are not linked --------------------------------------------
 * build_vehicle_gui.cpp, group_gui.cpp, autoreplace_gui.cpp, order_gui.cpp and
 * timetable_gui.cpp are all out of the build. The buttons that would open them are
 * drawn by vehicle_gui.cpp regardless (they live in its widget tree), so the honest
 * behaviour is a click that opens nothing rather than a click that crashes. */
void ShowBuildVehicleWindow(TileIndex, VehicleType)
{
}

void ShowCompanyGroup(CompanyID, VehicleType, GroupID, bool)
{
}

void ShowCompanyGroupForVehicle(const Vehicle *)
{
}

void ShowReplaceGroupVehicleWindow(GroupID, VehicleType)
{
}

void ShowOrdersWindow(const Vehicle *)
{
}

void ShowTimetableWindow(const Vehicle *)
{
}

/* --- The other three vehicle types ------------------------------------------
 * train_gui.cpp / ship_gui.cpp / aircraft_gui.cpp are not linked and cannot be:
 * they reach into consists, articulated parts and rotor sprites that have no pool
 * behind them. vehicle_gui.cpp dispatches on VehicleType, and in R1 that switch
 * only ever selects VEH_ROAD — these arms exist to satisfy the linker, not the
 * player. Drawing nothing is correct: there is nothing to draw.
 *
 * Train::GetCursorImageOffset/GetDisplayImageWidth are plain Train methods from
 * train_cmd.cpp, NOT part of the GroundVehicle<> explicit instantiations at the
 * end of ground_vehicle.cpp — defining them here cannot shadow the real
 * GroundVehicle<RoadVehicle, VEH_ROAD> code the buses run on. */
int GetTrainDetailsWndVScroll(VehicleID, TrainDetailsWindowTabs)
{
	return 0;
}

void DrawTrainDetails(const Train *, const Rect &, int, uint16, TrainDetailsWindowTabs)
{
}

void DrawTrainImage(const Train *, const Rect &, VehicleID, EngineImageType, int, VehicleID)
{
}

void DrawShipDetails(const Vehicle *, const Rect &)
{
}

void DrawShipImage(const Vehicle *, const Rect &, VehicleID, EngineImageType)
{
}

void DrawAircraftDetails(const Aircraft *, const Rect &)
{
}

void DrawAircraftImage(const Vehicle *, const Rect &, VehicleID, EngineImageType)
{
}

int Train::GetCursorImageOffset() const
{
	return 0;
}

int Train::GetDisplayImageWidth(Point *offset) const
{
	if (offset != nullptr) {
		offset->x = 0;
		offset->y = 0;
	}
	return 0;
}

/* Helicopter rotors are a NewGRF sprite override; with no NewGRF loaded and no
 * aircraft pool, "no sprite" is the whole truth. */
void GetRotorOverrideSprite(EngineID, const struct Aircraft *, EngineImageType, VehicleSpriteSeq *result)
{
	result->Clear();
}

/* The text-effect subsystem is stubbed port-wide (DrawTextEffects and
 * MoveAllTextEffects are already no-ops in m1_viewport_stubs.cpp /
 * m1_window_stubs.cpp), so a text effect registered here could never be drawn or
 * aged. Reporting INVALID_TE_ID keeps that consistent instead of handing callers
 * an id nothing will ever honour. */
TextEffectID AddTextEffect(StringID, int, int, uint8, TextEffectMode)
{
	return INVALID_TE_ID;
}
