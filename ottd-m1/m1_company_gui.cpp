/*
 * This file is part of ottd-macos9 — a port of OpenTTD to Mac OS 9 / PowerPC.
 * Copyright (c) 2026 Felipe De Bene.
 *
 * Derived from OpenTTD's src/company_gui.cpp (the CompanyWindow slice),
 * Copyright (c) the OpenTTD Development Team. Modified for the Mac OS 9 / PowerPC
 * port in 2026.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2, as published
 * by the Free Software Foundation. This program comes with NO WARRANTY. See
 * the LICENSE and NOTICE files in the repository root for the full terms.
 */

// R1: the REAL CompanyWindow (the company "general info" overview), extracted M1-style so the
// canonical toolbar can open the genuine company window — real manager face, inauguration year,
// colour scheme, vehicle/infrastructure counts, share owners, company value — instead of a no-op
// stub. Same "own the real object, stub the cascade" pattern as m1_finance_gui.cpp /
// m1_town_directory_gui.cpp: the slice below is verbatim OpenTTD company_gui.cpp (DrawCompanyManagerFace
// + widget tree + GetAmountOwnedBy + the CompanyWindow struct + ShowCompany), and the only things
// this TU adds are thin stubs for the cascade whose real TUs are not compiled (the livery /
// manager-face-select windows, the infrastructure window, the tile-placement + network-password
// machinery, and the money/share/rename/build-object command procs). The buttons that reach those
// stubs become harmless no-ops.
//
// company_manager_face.h supplies GetCompanyManagerFaceSprite / GetCompanyManagerFaceBits / _cmf_info
// inline, so DrawCompanyManagerFace links with no extra .cpp — the face renders (c->face==0 is valid,
// its sprites are in the loaded ogfx1_base.grf).

#include "stdafx.h"
#include "currency.h"
#include "window_gui.h"
#include "company_func.h"
#include "command_func.h"
#include "strings_func.h"
#include "date_func.h"
#include "company_base.h"
#include "economy_func.h"
#include "gfx_func.h"
#include "window_func.h"
#include "misc_cmd.h"
#include "core/geometry_func.hpp"
#include "company_gui.h"
#include "company_manager_face.h"
#include "economy_cmd.h"
#include "company_cmd.h"
#include "object_cmd.h"
#include "tilehighlight_func.h"
#include "viewport_func.h"
#include "textbuf_gui.h"
#include "gui.h"                          /* ShowExtraViewportWindow */
#include "sprite.h"                       /* GENERAL_SPRITE_COLOUR / COMPANY_SPRITE_COLOUR */
#include "network/network.h"              /* _networking / _network_server */
#include "network/network_func.h"
#include "network/network_gui.h"
#include "table/sprites.h"    /* SPR_GRADIENT / SPR_VEH_BUS_SW_VIEW / SPR_CURSOR_HQ / SPR_LOCK */

#include "widgets/company_widget.h"

#include "safeguards.h"

// The cascade windows / face-select proc whose real TUs aren't compiled (stubbed at the bottom).
static void ShowCompanyInfrastructure(CompanyID company);
static void DoSelectCompanyManagerFace(Window *parent);

// ----------------------------------------------------------------------------
// Verbatim slice from OpenTTD src/company_gui.cpp (company overview window).
// ----------------------------------------------------------------------------

/**
 * Draws the face of a company manager's face.
 * @param cmf   the company manager's face
 * @param colour the (background) colour of the gradient
 * @param x     x-position to draw the face
 * @param y     y-position to draw the face
 */
void DrawCompanyManagerFace(CompanyManagerFace cmf, int colour, int x, int y)
{
	GenderEthnicity ge = (GenderEthnicity)GetCompanyManagerFaceBits(cmf, CMFV_GEN_ETHN, GE_WM);

	bool has_moustache   = !HasBit(ge, GENDER_FEMALE) && GetCompanyManagerFaceBits(cmf, CMFV_HAS_MOUSTACHE,   ge) != 0;
	bool has_tie_earring = !HasBit(ge, GENDER_FEMALE) || GetCompanyManagerFaceBits(cmf, CMFV_HAS_TIE_EARRING, ge) != 0;
	bool has_glasses     = GetCompanyManagerFaceBits(cmf, CMFV_HAS_GLASSES, ge) != 0;
	PaletteID pal;

	/* Modify eye colour palette only if 2 or more valid values exist */
	if (_cmf_info[CMFV_EYE_COLOUR].valid_values[ge] < 2) {
		pal = PAL_NONE;
	} else {
		switch (GetCompanyManagerFaceBits(cmf, CMFV_EYE_COLOUR, ge)) {
			default: NOT_REACHED();
			case 0: pal = PALETTE_TO_BROWN; break;
			case 1: pal = PALETTE_TO_BLUE;  break;
			case 2: pal = PALETTE_TO_GREEN; break;
		}
	}

	/* Draw the gradient (background) */
	DrawSprite(SPR_GRADIENT, GENERAL_SPRITE_COLOUR(colour), x, y);

	for (CompanyManagerFaceVariable cmfv = CMFV_CHEEKS; cmfv < CMFV_END; cmfv++) {
		switch (cmfv) {
			case CMFV_MOUSTACHE:   if (!has_moustache)   continue; break;
			case CMFV_LIPS:
			case CMFV_NOSE:        if (has_moustache)    continue; break;
			case CMFV_TIE_EARRING: if (!has_tie_earring) continue; break;
			case CMFV_GLASSES:     if (!has_glasses)     continue; break;
			default: break;
		}
		DrawSprite(GetCompanyManagerFaceSprite(cmf, cmfv, ge), (cmfv == CMFV_EYEBROWS) ? pal : PAL_NONE, x, y);
	}
}

static const NWidgetPart _nested_company_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_C_CAPTION), SetDataTip(STR_COMPANY_VIEW_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		// R1-84: dropped SHADEBOX + STICKYBOX — their GUI sprites aren't in the loaded base GRF,
		// so they rendered as the missing-sprite "?". Closebox + caption stay.
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_HORIZONTAL), SetPIP(4, 6, 4),
			NWidget(NWID_VERTICAL), SetPIP(4, 2, 4),
				NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_FACE), SetMinimalSize(92, 119), SetFill(1, 0),
				NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_FACE_TITLE), SetFill(1, 1), SetMinimalTextLines(2, 0),
			EndContainer(),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_VERTICAL), SetPIP(4, 5, 5),
						NWidget(WWT_TEXT, COLOUR_GREY, WID_C_DESC_INAUGURATION), SetDataTip(STR_COMPANY_VIEW_INAUGURATED_TITLE, STR_NULL), SetFill(1, 0),
						NWidget(NWID_HORIZONTAL), SetPIP(0, 5, 0),
							NWidget(WWT_LABEL, COLOUR_GREY, WID_C_DESC_COLOUR_SCHEME), SetDataTip(STR_COMPANY_VIEW_COLOUR_SCHEME_TITLE, STR_NULL),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_DESC_COLOUR_SCHEME_EXAMPLE), SetMinimalSize(30, 0), SetFill(0, 1),
							NWidget(NWID_SPACER), SetFill(1, 0),
						EndContainer(),
						NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
							NWidget(NWID_VERTICAL),
								NWidget(WWT_TEXT, COLOUR_GREY, WID_C_DESC_VEHICLE), SetDataTip(STR_COMPANY_VIEW_VEHICLES_TITLE, STR_NULL),
								NWidget(NWID_SPACER), SetFill(0, 1),
							EndContainer(),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_DESC_VEHICLE_COUNTS), SetMinimalTextLines(4, 0),
							NWidget(NWID_SPACER), SetFill(1, 0),
						EndContainer(),
					EndContainer(),
					NWidget(NWID_VERTICAL), SetPIP(4, 2, 4),
						NWidget(NWID_SELECTION, INVALID_COLOUR, WID_C_SELECT_VIEW_BUILD_HQ),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_VIEW_HQ), SetDataTip(STR_COMPANY_VIEW_VIEW_HQ_BUTTON, STR_COMPANY_VIEW_VIEW_HQ_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_C_BUILD_HQ), SetDataTip(STR_COMPANY_VIEW_BUILD_HQ_BUTTON, STR_COMPANY_VIEW_BUILD_HQ_TOOLTIP),
						EndContainer(),
						NWidget(NWID_SELECTION, INVALID_COLOUR, WID_C_SELECT_RELOCATE),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_C_RELOCATE_HQ), SetDataTip(STR_COMPANY_VIEW_RELOCATE_HQ, STR_COMPANY_VIEW_RELOCATE_COMPANY_HEADQUARTERS),
							NWidget(NWID_SPACER),
						EndContainer(),
						NWidget(NWID_SPACER), SetFill(0, 1),
					EndContainer(),
				EndContainer(),
				NWidget(WWT_TEXT, COLOUR_GREY, WID_C_DESC_COMPANY_VALUE), SetDataTip(STR_COMPANY_VIEW_COMPANY_VALUE, STR_NULL), SetFill(1, 0),
				NWidget(NWID_VERTICAL), SetPIP(4, 2, 4),
					NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
						NWidget(NWID_VERTICAL),
							NWidget(WWT_TEXT, COLOUR_GREY, WID_C_DESC_INFRASTRUCTURE), SetDataTip(STR_COMPANY_VIEW_INFRASTRUCTURE, STR_NULL),
							NWidget(NWID_SPACER), SetFill(0, 1),
						EndContainer(),
						NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_DESC_INFRASTRUCTURE_COUNTS), SetMinimalTextLines(5, 0), SetFill(1, 0),
						NWidget(NWID_VERTICAL),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_VIEW_INFRASTRUCTURE), SetDataTip(STR_COMPANY_VIEW_INFRASTRUCTURE_BUTTON, STR_COMPANY_VIEW_INFRASTRUCTURE_TOOLTIP),
							NWidget(NWID_SPACER),
						EndContainer(),
					EndContainer(),
				EndContainer(),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SELECTION, INVALID_COLOUR, WID_C_SELECT_DESC_OWNERS),
						NWidget(NWID_VERTICAL), SetPIP(5, 5, 4),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_DESC_OWNERS), SetMinimalTextLines(MAX_COMPANY_SHARE_OWNERS, 0),
							NWidget(NWID_SPACER), SetFill(0, 1),
						EndContainer(),
					EndContainer(),
					/* Multi player buttons. */
					NWidget(NWID_VERTICAL), SetPIP(4, 2, 4),
						NWidget(NWID_SPACER), SetFill(0, 1),
						NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
							NWidget(NWID_SPACER), SetFill(1, 0),
							NWidget(NWID_SELECTION, INVALID_COLOUR, WID_C_SELECT_GIVE_MONEY),
								NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_GIVE_MONEY), SetDataTip(STR_COMPANY_VIEW_GIVE_MONEY_BUTTON, STR_COMPANY_VIEW_GIVE_MONEY_TOOLTIP),
							EndContainer(),
						EndContainer(),
						NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
							NWidget(WWT_EMPTY, COLOUR_GREY, WID_C_HAS_PASSWORD),
							NWidget(NWID_SELECTION, INVALID_COLOUR, WID_C_SELECT_MULTIPLAYER),
								NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_COMPANY_PASSWORD), SetDataTip(STR_COMPANY_VIEW_PASSWORD, STR_COMPANY_VIEW_PASSWORD_TOOLTIP),
								NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_COMPANY_JOIN), SetDataTip(STR_COMPANY_VIEW_JOIN, STR_COMPANY_VIEW_JOIN_TOOLTIP),
							EndContainer(),
						EndContainer(),
					EndContainer(),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
	/* Button bars at the bottom. */
	NWidget(NWID_SELECTION, INVALID_COLOUR, WID_C_SELECT_BUTTONS),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_NEW_FACE), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_NEW_FACE_BUTTON, STR_COMPANY_VIEW_NEW_FACE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_COLOUR_SCHEME), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_COLOUR_SCHEME_BUTTON, STR_COMPANY_VIEW_COLOUR_SCHEME_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_PRESIDENT_NAME), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_PRESIDENT_NAME_BUTTON, STR_COMPANY_VIEW_PRESIDENT_NAME_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_COMPANY_NAME), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_COMPANY_NAME_BUTTON, STR_COMPANY_VIEW_COMPANY_NAME_TOOLTIP),
		EndContainer(),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_BUY_SHARE), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_BUY_SHARE_BUTTON, STR_COMPANY_VIEW_BUY_SHARE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_SELL_SHARE), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_SELL_SHARE_BUTTON, STR_COMPANY_VIEW_SELL_SHARE_TOOLTIP),
		EndContainer(),
	EndContainer(),
};

int GetAmountOwnedBy(const Company *c, Owner owner)
{
	auto share_owned_by = [owner](auto share_owner) { return share_owner == owner; };
	return std::count_if(c->share_owners.begin(), c->share_owners.end(), share_owned_by);
}

/** Strings for the company vehicle counts */
static const StringID _company_view_vehicle_count_strings[] = {
	STR_COMPANY_VIEW_TRAINS, STR_COMPANY_VIEW_ROAD_VEHICLES, STR_COMPANY_VIEW_SHIPS, STR_COMPANY_VIEW_AIRCRAFT
};

/**
 * Window with general information about a company
 */
struct CompanyWindow : Window
{
	CompanyWidgets query_widget;

	/** Display planes in the company window. */
	enum CompanyWindowPlanes {
		/* Display planes of the #WID_C_SELECT_MULTIPLAYER selection widget. */
		CWP_MP_C_PWD = 0, ///< Display the company password button.
		CWP_MP_C_JOIN,    ///< Display the join company button.

		/* Display planes of the #WID_C_SELECT_VIEW_BUILD_HQ selection widget. */
		CWP_VB_VIEW = 0,  ///< Display the view button
		CWP_VB_BUILD,     ///< Display the build button

		/* Display planes of the #WID_C_SELECT_RELOCATE selection widget. */
		CWP_RELOCATE_SHOW = 0, ///< Show the relocate HQ button.
		CWP_RELOCATE_HIDE,     ///< Hide the relocate HQ button.

		/* Display planes of the #WID_C_SELECT_BUTTONS selection widget. */
		CWP_BUTTONS_LOCAL = 0, ///< Buttons of the local company.
		CWP_BUTTONS_OTHER,     ///< Buttons of the other companies.
	};

	CompanyWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
		this->owner = (Owner)this->window_number;
		this->OnInvalidateData();
	}

	void OnPaint() override
	{
		const Company *c = Company::Get((CompanyID)this->window_number);
		bool local = this->window_number == _local_company;

		if (!this->IsShaded()) {
			bool reinit = false;

			/* Button bar selection. */
			int plane = local ? CWP_BUTTONS_LOCAL : CWP_BUTTONS_OTHER;
			NWidgetStacked *wi = this->GetWidget<NWidgetStacked>(WID_C_SELECT_BUTTONS);
			if (plane != wi->shown_plane) {
				wi->SetDisplayedPlane(plane);
				this->InvalidateData();
				reinit = true;
			}

			/* Build HQ button handling. */
			plane = (local && c->location_of_HQ == INVALID_TILE) ? CWP_VB_BUILD : CWP_VB_VIEW;
			wi = this->GetWidget<NWidgetStacked>(WID_C_SELECT_VIEW_BUILD_HQ);
			if (plane != wi->shown_plane) {
				wi->SetDisplayedPlane(plane);
				reinit = true;
			}

			this->SetWidgetDisabledState(WID_C_VIEW_HQ, c->location_of_HQ == INVALID_TILE);

			/* Enable/disable 'Relocate HQ' button. */
			plane = (!local || c->location_of_HQ == INVALID_TILE) ? CWP_RELOCATE_HIDE : CWP_RELOCATE_SHOW;
			wi = this->GetWidget<NWidgetStacked>(WID_C_SELECT_RELOCATE);
			if (plane != wi->shown_plane) {
				wi->SetDisplayedPlane(plane);
				reinit = true;
			}

			/* Owners of company */
			auto invalid_owner = [](auto owner) { return owner == INVALID_COMPANY; };
			plane = std::all_of(c->share_owners.begin(), c->share_owners.end(), invalid_owner) ? SZSP_HORIZONTAL : 0;
			wi = this->GetWidget<NWidgetStacked>(WID_C_SELECT_DESC_OWNERS);
			if (plane != wi->shown_plane) {
				wi->SetDisplayedPlane(plane);
				reinit = true;
			}

			/* Enable/disable 'Give money' button. */
			plane = ((local || _local_company == COMPANY_SPECTATOR || !_settings_game.economy.give_money) ? SZSP_NONE : 0);
			wi = this->GetWidget<NWidgetStacked>(WID_C_SELECT_GIVE_MONEY);
			if (plane != wi->shown_plane) {
				wi->SetDisplayedPlane(plane);
				reinit = true;
			}

			/* Multiplayer buttons. */
			plane = ((!_networking) ? (int)SZSP_NONE : (int)(local ? CWP_MP_C_PWD : CWP_MP_C_JOIN));
			wi = this->GetWidget<NWidgetStacked>(WID_C_SELECT_MULTIPLAYER);
			if (plane != wi->shown_plane) {
				wi->SetDisplayedPlane(plane);
				reinit = true;
			}
			this->SetWidgetDisabledState(WID_C_COMPANY_JOIN,   c->is_ai);

			if (reinit) {
				this->ReInit();
				return;
			}
		}

		this->DrawWidgets();
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_C_FACE: {
				Dimension face_size = GetSpriteSize(SPR_GRADIENT);
				size->width  = std::max(size->width,  face_size.width);
				size->height = std::max(size->height, face_size.height);
				break;
			}

			case WID_C_DESC_COLOUR_SCHEME_EXAMPLE: {
				Point offset;
				Dimension d = GetSpriteSize(SPR_VEH_BUS_SW_VIEW, &offset);
				d.width -= offset.x;
				d.height -= offset.y;
				*size = maxdim(*size, d);
				break;
			}

			case WID_C_DESC_COMPANY_VALUE:
				SetDParam(0, INT64_MAX); // Arguably the maximum company value
				size->width = GetStringBoundingBox(STR_COMPANY_VIEW_COMPANY_VALUE).width;
				break;

			case WID_C_DESC_VEHICLE_COUNTS:
				SetDParamMaxValue(0, 5000); // Maximum number of vehicles
				for (uint i = 0; i < lengthof(_company_view_vehicle_count_strings); i++) {
					size->width = std::max(size->width, GetStringBoundingBox(_company_view_vehicle_count_strings[i]).width + padding.width);
				}
				break;

			case WID_C_DESC_INFRASTRUCTURE_COUNTS:
				SetDParamMaxValue(0, UINT_MAX);
				size->width = GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_RAIL).width;
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_ROAD).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_WATER).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_STATION).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_AIRPORT).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_NONE).width);
				size->width += padding.width;
				break;

			case WID_C_DESC_OWNERS: {
				for (const Company *c2 : Company::Iterate()) {
					SetDParamMaxValue(0, 75);
					SetDParam(1, c2->index);

					size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_SHARES_OWNED_BY).width);
				}
				break;
			}

			case WID_C_VIEW_HQ:
			case WID_C_BUILD_HQ:
			case WID_C_RELOCATE_HQ:
			case WID_C_VIEW_INFRASTRUCTURE:
			case WID_C_GIVE_MONEY:
			case WID_C_COMPANY_PASSWORD:
			case WID_C_COMPANY_JOIN:
				size->width = GetStringBoundingBox(STR_COMPANY_VIEW_VIEW_HQ_BUTTON).width;
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_BUILD_HQ_BUTTON).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_RELOCATE_HQ).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_BUTTON).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_GIVE_MONEY_BUTTON).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_PASSWORD).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_JOIN).width);
				size->width += padding.width;
				break;

			case WID_C_HAS_PASSWORD:
				*size = maxdim(*size, GetSpriteSize(SPR_LOCK));
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		const Company *c = Company::Get((CompanyID)this->window_number);
		switch (widget) {
			case WID_C_FACE:
				DrawCompanyManagerFace(c->face, c->colour, r.left, r.top);
				break;

			case WID_C_FACE_TITLE:
				SetDParam(0, c->index);
				DrawStringMultiLine(r.left, r.right, r.top, r.bottom, STR_COMPANY_VIEW_PRESIDENT_MANAGER_TITLE, TC_FROMSTRING, SA_HOR_CENTER);
				break;

			case WID_C_DESC_COLOUR_SCHEME_EXAMPLE: {
				Point offset;
				Dimension d = GetSpriteSize(SPR_VEH_BUS_SW_VIEW, &offset);
				d.height -= offset.y;
				DrawSprite(SPR_VEH_BUS_SW_VIEW, COMPANY_SPRITE_COLOUR(c->index), r.left - offset.x, CenterBounds(r.top, r.bottom, d.height) - offset.y);
				break;
			}

			case WID_C_DESC_VEHICLE_COUNTS: {
				uint amounts[4];
				amounts[0] = c->group_all[VEH_TRAIN].num_vehicle;
				amounts[1] = c->group_all[VEH_ROAD].num_vehicle;
				amounts[2] = c->group_all[VEH_SHIP].num_vehicle;
				amounts[3] = c->group_all[VEH_AIRCRAFT].num_vehicle;

				int y = r.top;
				if (amounts[0] + amounts[1] + amounts[2] + amounts[3] == 0) {
					DrawString(r.left, r.right, y, STR_COMPANY_VIEW_VEHICLES_NONE);
				} else {
					static_assert(lengthof(amounts) == lengthof(_company_view_vehicle_count_strings));

					for (uint i = 0; i < lengthof(amounts); i++) {
						if (amounts[i] != 0) {
							SetDParam(0, amounts[i]);
							DrawString(r.left, r.right, y, _company_view_vehicle_count_strings[i]);
							y += FONT_HEIGHT_NORMAL;
						}
					}
				}
				break;
			}

			case WID_C_DESC_INFRASTRUCTURE_COUNTS: {
				uint y = r.top;

				/* Collect rail and road counts. */
				uint rail_pieces = c->infrastructure.signal;
				uint road_pieces = 0;
				for (uint i = 0; i < lengthof(c->infrastructure.rail); i++) rail_pieces += c->infrastructure.rail[i];
				for (uint i = 0; i < lengthof(c->infrastructure.road); i++) road_pieces += c->infrastructure.road[i];

				if (rail_pieces == 0 && road_pieces == 0 && c->infrastructure.water == 0 && c->infrastructure.station == 0 && c->infrastructure.airport == 0) {
					DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_NONE);
				} else {
					if (rail_pieces != 0) {
						SetDParam(0, rail_pieces);
						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_RAIL);
						y += FONT_HEIGHT_NORMAL;
					}
					if (road_pieces != 0) {
						SetDParam(0, road_pieces);
						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_ROAD);
						y += FONT_HEIGHT_NORMAL;
					}
					if (c->infrastructure.water != 0) {
						SetDParam(0, c->infrastructure.water);
						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_WATER);
						y += FONT_HEIGHT_NORMAL;
					}
					if (c->infrastructure.station != 0) {
						SetDParam(0, c->infrastructure.station);
						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_STATION);
						y += FONT_HEIGHT_NORMAL;
					}
					if (c->infrastructure.airport != 0) {
						SetDParam(0, c->infrastructure.airport);
						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_AIRPORT);
					}
				}

				break;
			}

			case WID_C_DESC_OWNERS: {
				uint y = r.top;

				for (const Company *c2 : Company::Iterate()) {
					uint amt = GetAmountOwnedBy(c, c2->index);
					if (amt != 0) {
						SetDParam(0, amt * 25);
						SetDParam(1, c2->index);

						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_SHARES_OWNED_BY);
						y += FONT_HEIGHT_NORMAL;
					}
				}
				break;
			}

			case WID_C_HAS_PASSWORD:
				if (_networking && NetworkCompanyIsPassworded(c->index)) {
					DrawSprite(SPR_LOCK, PAL_NONE, r.left, r.top);
				}
				break;
		}
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_C_CAPTION:
				SetDParam(0, (CompanyID)this->window_number);
				SetDParam(1, (CompanyID)this->window_number);
				break;

			case WID_C_DESC_INAUGURATION:
				SetDParam(0, Company::Get((CompanyID)this->window_number)->inaugurated_year);
				break;

			case WID_C_DESC_COMPANY_VALUE:
				SetDParam(0, CalculateCompanyValue(Company::Get((CompanyID)this->window_number)));
				break;
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_C_NEW_FACE: DoSelectCompanyManagerFace(this); break;

			case WID_C_COLOUR_SCHEME:
				ShowCompanyLiveryWindow((CompanyID)this->window_number, INVALID_GROUP);
				break;

			case WID_C_PRESIDENT_NAME:
				this->query_widget = WID_C_PRESIDENT_NAME;
				SetDParam(0, this->window_number);
				ShowQueryString(STR_PRESIDENT_NAME, STR_COMPANY_VIEW_PRESIDENT_S_NAME_QUERY_CAPTION, MAX_LENGTH_PRESIDENT_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				break;

			case WID_C_COMPANY_NAME:
				this->query_widget = WID_C_COMPANY_NAME;
				SetDParam(0, this->window_number);
				ShowQueryString(STR_COMPANY_NAME, STR_COMPANY_VIEW_COMPANY_NAME_QUERY_CAPTION, MAX_LENGTH_COMPANY_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				break;

			case WID_C_VIEW_HQ: {
				TileIndex tile = Company::Get((CompanyID)this->window_number)->location_of_HQ;
				if (_ctrl_pressed) {
					ShowExtraViewportWindow(tile);
				} else {
					ScrollMainWindowToTile(tile);
				}
				break;
			}

			case WID_C_BUILD_HQ:
				if ((byte)this->window_number != _local_company) return;
				if (this->IsWidgetLowered(WID_C_BUILD_HQ)) {
					ResetObjectToPlace();
					this->RaiseButtons();
					break;
				}
				SetObjectToPlaceWnd(SPR_CURSOR_HQ, PAL_NONE, HT_RECT, this);
				SetTileSelectSize(2, 2);
				this->LowerWidget(WID_C_BUILD_HQ);
				this->SetWidgetDirty(WID_C_BUILD_HQ);
				break;

			case WID_C_RELOCATE_HQ:
				if (this->IsWidgetLowered(WID_C_RELOCATE_HQ)) {
					ResetObjectToPlace();
					this->RaiseButtons();
					break;
				}
				SetObjectToPlaceWnd(SPR_CURSOR_HQ, PAL_NONE, HT_RECT, this);
				SetTileSelectSize(2, 2);
				this->LowerWidget(WID_C_RELOCATE_HQ);
				this->SetWidgetDirty(WID_C_RELOCATE_HQ);
				break;

			case WID_C_VIEW_INFRASTRUCTURE:
				ShowCompanyInfrastructure((CompanyID)this->window_number);
				break;

			case WID_C_GIVE_MONEY:
				this->query_widget = WID_C_GIVE_MONEY;
				ShowQueryString(STR_EMPTY, STR_COMPANY_VIEW_GIVE_MONEY_QUERY_CAPTION, 30, this, CS_NUMERAL, QSF_NONE);
				break;

			case WID_C_BUY_SHARE:
				Command<CMD_BUY_SHARE_IN_COMPANY>::Post(STR_ERROR_CAN_T_BUY_25_SHARE_IN_THIS, (CompanyID)this->window_number);
				break;

			case WID_C_SELL_SHARE:
				Command<CMD_SELL_SHARE_IN_COMPANY>::Post(STR_ERROR_CAN_T_SELL_25_SHARE_IN, (CompanyID)this->window_number);
				break;

			case WID_C_COMPANY_PASSWORD:
				if (this->window_number == _local_company) ShowNetworkCompanyPasswordWindow(this);
				break;

			case WID_C_COMPANY_JOIN: {
				this->query_widget = WID_C_COMPANY_JOIN;
				CompanyID company = (CompanyID)this->window_number;
				if (_network_server) {
					NetworkServerDoMove(CLIENT_ID_SERVER, company);
					MarkWholeScreenDirty();
				} else if (NetworkCompanyIsPassworded(company)) {
					/* ask for the password */
					ShowQueryString(STR_EMPTY, STR_NETWORK_NEED_COMPANY_PASSWORD_CAPTION, NETWORK_PASSWORD_LENGTH, this, CS_ALPHANUMERAL, QSF_PASSWORD);
				} else {
					/* just send the join command */
					NetworkClientRequestMove(company);
				}
				break;
			}
		}
	}

	void OnHundredthTick() override
	{
		/* redraw the window every now and then */
		this->SetDirty();
	}

	void OnPlaceObject(Point pt, TileIndex tile) override
	{
		if (Command<CMD_BUILD_OBJECT>::Post(STR_ERROR_CAN_T_BUILD_COMPANY_HEADQUARTERS, tile, OBJECT_HQ, 0) && !_shift_pressed) {
			ResetObjectToPlace();
			this->RaiseButtons();
		}
	}

	void OnPlaceObjectAbort() override
	{
		this->RaiseButtons();
	}

	void OnQueryTextFinished(char *str) override
	{
		if (str == nullptr) return;

		switch (this->query_widget) {
			default: NOT_REACHED();

			case WID_C_GIVE_MONEY: {
				Money money = (Money)(strtoull(str, nullptr, 10) / _currency->rate);
				uint32 money_c = Clamp(ClampToI32(money), 0, 20000000); // Clamp between 20 million and 0

				Command<CMD_GIVE_MONEY>::Post(STR_ERROR_CAN_T_GIVE_MONEY, money_c, (CompanyID)this->window_number);
				break;
			}

			case WID_C_PRESIDENT_NAME:
				Command<CMD_RENAME_PRESIDENT>::Post(STR_ERROR_CAN_T_CHANGE_PRESIDENT, str);
				break;

			case WID_C_COMPANY_NAME:
				Command<CMD_RENAME_COMPANY>::Post(STR_ERROR_CAN_T_CHANGE_COMPANY_NAME, str);
				break;

			case WID_C_COMPANY_JOIN:
				NetworkClientRequestMove((CompanyID)this->window_number, str);
				break;
		}
	}


	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (this->window_number == _local_company) return;

		if (_settings_game.economy.allow_shares) { // Shares are allowed
			const Company *c = Company::Get(this->window_number);

			/* If all shares are owned by someone (none by nobody), disable buy button */
			this->SetWidgetDisabledState(WID_C_BUY_SHARE, GetAmountOwnedBy(c, INVALID_OWNER) == 0 ||
					/* Only 25% left to buy. If the company is human, disable buying it up.. TODO issues! */
					(GetAmountOwnedBy(c, INVALID_OWNER) == 1 && !c->is_ai) ||
					/* Spectators cannot do anything of course */
					_local_company == COMPANY_SPECTATOR);

			/* If the company doesn't own any shares, disable sell button */
			this->SetWidgetDisabledState(WID_C_SELL_SHARE, (GetAmountOwnedBy(c, _local_company) == 0) ||
					/* Spectators cannot do anything of course */
					_local_company == COMPANY_SPECTATOR);
		} else { // Shares are not allowed, disable buy/sell buttons
			this->DisableWidget(WID_C_BUY_SHARE);
			this->DisableWidget(WID_C_SELL_SHARE);
		}
	}
};

static WindowDesc _company_desc(
	WDP_AUTO, "company", 0, 0,
	WC_COMPANY, WC_NONE,
	0,
	_nested_company_widgets, lengthof(_nested_company_widgets)
);

/**
 * Show the window with the overview of the company.
 * @param company The company to show the window for.
 */
void ShowCompany(CompanyID company)
{
	if (!Company::IsValidID(company)) return;

	AllocateWindowDescFront<CompanyWindow>(&_company_desc, company);
}

// ----------------------------------------------------------------------------
// R1 stubs: the cascade whose real TUs are not compiled — the livery / manager-face-select
// windows, the infrastructure window, the tile-placement + network-password machinery, and the
// money/share/rename/build-object command procs. Signatures verified against economy_cmd.h,
// company_cmd.h, object_cmd.h, tilehighlight_func.h, viewport_func.h, network_gui.h and
// company_base.h. The buttons that reach these become harmless no-ops via the (already-stubbed)
// command Post backend / no-op window openers.
// ----------------------------------------------------------------------------
static void ShowCompanyInfrastructure(CompanyID) {}
static void DoSelectCompanyManagerFace(Window *) {}
void ShowCompanyLiveryWindow(CompanyID, GroupID) {}
void SetObjectToPlaceWnd(CursorID, PaletteID, HighLightStyle, Window *) {}
void SetTileSelectSize(int, int) {}
void ShowNetworkCompanyPasswordWindow(Window *) {}
Money CalculateCompanyValue(const Company *c, bool) { return c->money; }
CommandCost CmdBuyShareInCompany (DoCommandFlag, CompanyID) { return CommandCost(); }
CommandCost CmdSellShareInCompany(DoCommandFlag, CompanyID) { return CommandCost(); }
CommandCost CmdGiveMoney         (DoCommandFlag, uint32, CompanyID) { return CommandCost(); }
CommandCost CmdRenamePresident   (DoCommandFlag, const std::string &) { return CommandCost(); }
CommandCost CmdRenameCompany     (DoCommandFlag, const std::string &) { return CommandCost(); }
CommandCost CmdBuildObject       (DoCommandFlag, TileIndex, ObjectType, uint8) { return CommandCost(); }
