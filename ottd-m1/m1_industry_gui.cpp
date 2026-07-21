/*
 * This file is part of ottd-macos9 — a port of OpenTTD to Mac OS 9 / PowerPC.
 * Copyright (c) 2026 Felipe De Bene.
 *
 * Derived from OpenTTD's src/industry_gui.cpp (the IndustryViewWindow and
 * IndustryDirectoryWindow slices), Copyright (c) the OpenTTD Development Team.
 * Modified for the Mac OS 9 / PowerPC port in 2026.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2, as published
 * by the Free Software Foundation. This program comes with NO WARRANTY. See
 * the LICENSE and NOTICE files in the repository root for the full terms.
 */

// R1: the REAL IndustryViewWindow + IndustryDirectoryWindow, extracted M1-style (mirrors
// m1_town_directory_gui.cpp / m1_station_gui.cpp) so the game can open the genuine industry
// list (all industries, name + produced cargo + %transported, scrollable/sortable) and the
// per-industry view (production last month, cargo accepted with stockpile, cargo produced) on
// the REAL pooled Industry (R1-81). The slices below are verbatim OpenTTD industry_gui.cpp:
//   * IndustryViewWindow  (793-1215)
//   * IndustryDirectoryWindow (1217-1858)
// plus the shared cargo-suffix helpers (GetCargoSuffix / GetAllCargoSuffixes) and the
// _sorted_industry_types table those windows sort against.
//
// Title bars keep ONLY WWT_CLOSEBOX + WWT_CAPTION (+ the view window's SPR_GOTO_LOCATION
// pushimgbtn, whose sprite IS in the base GRF). DEBUGBOX/SHADEBOX/DEFSIZEBOX/STICKYBOX are
// dropped — their GUI sprites aren't in the loaded base GRF and would render as the
// missing-sprite "?".
//
// NewGRF industry callbacks are NOT compiled (industry_cmd.cpp / newgrf_industries.cpp are
// absent) and our industries are spec-lite (m1_industry.cpp seeds the Industry object directly,
// never an IndustrySpec). So this TU adds thin inert stubs for the uncompiled cascade at the
// bottom: a graceful zeroed GetIndustrySpec (+ its IndustrySpec members), the industry NewGRF
// callback helpers (all no-op → callback_mask is 0 on the stub spec, so every callback path is
// dead), the NewGRF-inspect overrides, and the no-op ShowIndustryCargoesWindow the buttons call.
// With a zeroed spec every callback_mask bit reads 0, so the windows fall through to drawing the
// REAL Industry object's cargo arrays — exactly the graceful degradation we want.
// strnatcmp is provided by b1_shims.o — do NOT define it here.

#include "stdafx.h"
#include "industry.h"
#include "cargotype.h"
#include "company_func.h"
#include "command_func.h"
#include "cheat_type.h"
#include "network/network.h"
#include "newgrf_industries.h"
#include "newgrf_text.h"
#include "newgrf_debug.h"
#include "strings_func.h"
#include "string_func.h"                  /* strnatcmp (b1_shims.o), StrEmpty */
#include "window_gui.h"
#include "window_func.h"
#include "textbuf_gui.h"                  /* ShowQueryString, QSF_NONE, CS_ALPHANUMERAL */
#include "widget_type.h"                  /* NWidgetViewport */
#include "gfx_func.h"
#include "zoom_func.h"                    /* ScaleZoomGUI */
#include "viewport_func.h"                /* ScrollMainWindowToTile, ScrollWindowToTile */
#include "gui.h"                          /* ShowExtraViewportWindow */
#include "settings_gui.h"                 /* DrawArrowButtons, SETTING_BUTTON_* */
#include "sortlist_type.h"
#include "core/geometry_func.hpp"         /* maxdim */
#include "widgets/dropdown_func.h"
#include "widgets/industry_widget.h"
#include "table/strings.h"

#include <bitset>

#include "safeguards.h"

// ----------------------------------------------------------------------------
// Shared cargo-suffix machinery (verbatim industry_gui.cpp:53-185). With our zeroed spec the
// CBM_IND_CARGO_SUFFIX bit is never set, so GetCargoSuffix returns the empty (CSD_CARGO_AMOUNT)
// default immediately — none of the NewGRF text machinery is actually reached at runtime.
// ----------------------------------------------------------------------------

/** Cargo suffix type (for which window is it requested) */
enum CargoSuffixType {
	CST_FUND,  ///< Fund-industry window
	CST_VIEW,  ///< View-industry window
	CST_DIR,   ///< Industry-directory window
};

/** Ways of displaying the cargo. */
enum CargoSuffixDisplay {
	CSD_CARGO,             ///< Display the cargo without sub-type (cb37 result 401).
	CSD_CARGO_AMOUNT,      ///< Display the cargo and amount (if useful), but no sub-type (cb37 result 400 or fail).
	CSD_CARGO_TEXT,        ///< Display then cargo and supplied string (cb37 result 800-BFF).
	CSD_CARGO_AMOUNT_TEXT, ///< Display then cargo, amount, and string (cb37 result 000-3FF).
};

/** Transfer storage of cargo suffix information. */
struct CargoSuffix {
	CargoSuffixDisplay display; ///< How to display the cargo and text.
	char text[512];             ///< Cargo suffix text.
};

static void ShowIndustryCargoesWindow(IndustryType id);

/**
 * Gets the string to display after the cargo name (using callback 37)
 */
static void GetCargoSuffix(uint cargo, CargoSuffixType cst, const Industry *ind, IndustryType ind_type, const IndustrySpec *indspec, CargoSuffix &suffix)
{
	suffix.text[0] = '\0';
	suffix.display = CSD_CARGO_AMOUNT;

	if (HasBit(indspec->callback_mask, CBM_IND_CARGO_SUFFIX)) {
		TileIndex t = (cst != CST_FUND) ? ind->location.tile : INVALID_TILE;
		uint16 callback = GetIndustryCallback(CBID_INDUSTRY_CARGO_SUFFIX, 0, (cst << 8) | cargo, const_cast<Industry *>(ind), ind_type, t);
		if (callback == CALLBACK_FAILED) return;

		if (indspec->grf_prop.grffile->grf_version < 8) {
			if (GB(callback, 0, 8) == 0xFF) return;
			if (callback < 0x400) {
				StartTextRefStackUsage(indspec->grf_prop.grffile, 6);
				GetString(suffix.text, GetGRFStringID(indspec->grf_prop.grffile->grfid, 0xD000 + callback), lastof(suffix.text));
				StopTextRefStackUsage();
				suffix.display = CSD_CARGO_AMOUNT_TEXT;
				return;
			}
			ErrorUnknownCallbackResult(indspec->grf_prop.grffile->grfid, CBID_INDUSTRY_CARGO_SUFFIX, callback);
			return;

		} else { // GRF version 8 or higher.
			if (callback == 0x400) return;
			if (callback == 0x401) {
				suffix.display = CSD_CARGO;
				return;
			}
			if (callback < 0x400) {
				StartTextRefStackUsage(indspec->grf_prop.grffile, 6);
				GetString(suffix.text, GetGRFStringID(indspec->grf_prop.grffile->grfid, 0xD000 + callback), lastof(suffix.text));
				StopTextRefStackUsage();
				suffix.display = CSD_CARGO_AMOUNT_TEXT;
				return;
			}
			if (callback >= 0x800 && callback < 0xC00) {
				StartTextRefStackUsage(indspec->grf_prop.grffile, 6);
				GetString(suffix.text, GetGRFStringID(indspec->grf_prop.grffile->grfid, 0xD000 - 0x800 + callback), lastof(suffix.text));
				StopTextRefStackUsage();
				suffix.display = CSD_CARGO_TEXT;
				return;
			}
			ErrorUnknownCallbackResult(indspec->grf_prop.grffile->grfid, CBID_INDUSTRY_CARGO_SUFFIX, callback);
			return;
		}
	}
}

enum CargoSuffixInOut {
	CARGOSUFFIX_OUT = 0,
	CARGOSUFFIX_IN  = 1,
};

/**
 * Gets all strings to display after the cargoes of industries (using callback 37)
 */
template <typename TC, typename TS>
static inline void GetAllCargoSuffixes(CargoSuffixInOut use_input, CargoSuffixType cst, const Industry *ind, IndustryType ind_type, const IndustrySpec *indspec, const TC &cargoes, TS &suffixes)
{
	static_assert(lengthof(cargoes) <= lengthof(suffixes));

	if (indspec->behaviour & INDUSTRYBEH_CARGOTYPES_UNLIMITED) {
		/* Reworked behaviour with new many-in-many-out scheme */
		for (uint j = 0; j < lengthof(suffixes); j++) {
			if (cargoes[j] != CT_INVALID) {
				byte local_id = indspec->grf_prop.grffile->cargo_map[cargoes[j]]; // should we check the value for valid?
				uint cargotype = local_id << 16 | use_input;
				GetCargoSuffix(cargotype, cst, ind, ind_type, indspec, suffixes[j]);
			} else {
				suffixes[j].text[0] = '\0';
				suffixes[j].display = CSD_CARGO;
			}
		}
	} else {
		/* Compatible behaviour with old 3-in-2-out scheme */
		for (uint j = 0; j < lengthof(suffixes); j++) {
			suffixes[j].text[0] = '\0';
			suffixes[j].display = CSD_CARGO;
		}
		switch (use_input) {
			case CARGOSUFFIX_OUT:
				if (cargoes[0] != CT_INVALID) GetCargoSuffix(3, cst, ind, ind_type, indspec, suffixes[0]);
				if (cargoes[1] != CT_INVALID) GetCargoSuffix(4, cst, ind, ind_type, indspec, suffixes[1]);
				break;
			case CARGOSUFFIX_IN:
				if (cargoes[0] != CT_INVALID) GetCargoSuffix(0, cst, ind, ind_type, indspec, suffixes[0]);
				if (cargoes[1] != CT_INVALID) GetCargoSuffix(1, cst, ind, ind_type, indspec, suffixes[1]);
				if (cargoes[2] != CT_INVALID) GetCargoSuffix(2, cst, ind, ind_type, indspec, suffixes[2]);
				break;
			default:
				NOT_REACHED();
		}
	}
}

std::array<IndustryType, NUM_INDUSTRYTYPES> _sorted_industry_types; ///< Industry types sorted by name.

/** Sort industry types by their name. */
static bool IndustryTypeNameSorter(const IndustryType &a, const IndustryType &b)
{
	static char industry_name[2][64];

	const IndustrySpec *indsp1 = GetIndustrySpec(a);
	GetString(industry_name[0], indsp1->name, lastof(industry_name[0]));

	const IndustrySpec *indsp2 = GetIndustrySpec(b);
	GetString(industry_name[1], indsp2->name, lastof(industry_name[1]));

	int r = strnatcmp(industry_name[0], industry_name[1]); // Sort by name (natural sorting).

	/* If the names are equal, sort by industry type. */
	return (r != 0) ? r < 0 : (a < b);
}

/**
 * Initialize the list of sorted industry types.
 */
void SortIndustryTypes()
{
	/* Add each industry type to the list. */
	for (IndustryType i = 0; i < NUM_INDUSTRYTYPES; i++) {
		_sorted_industry_types[i] = i;
	}

	/* Sort industry types by name. */
	std::sort(_sorted_industry_types.begin(), _sorted_industry_types.end(), IndustryTypeNameSorter);
}

// ----------------------------------------------------------------------------
// Verbatim slice from OpenTTD src/industry_gui.cpp:776-1215 (IndustryViewWindow).
// ----------------------------------------------------------------------------

static void UpdateIndustryProduction(Industry *i);

static inline bool IsProductionAlterable(const Industry *i)
{
	const IndustrySpec *is = GetIndustrySpec(i->type);
	bool has_prod = false;
	for (size_t j = 0; j < lengthof(is->production_rate); j++) {
		if (is->production_rate[j] != 0) {
			has_prod = true;
			break;
		}
	}
	return ((_game_mode == GM_EDITOR || _cheats.setup_prod.value) &&
			(has_prod || is->IsRawIndustry()) &&
			!_networking);
}

class IndustryViewWindow : public Window
{
	/** Modes for changing production */
	enum Editability {
		EA_NONE,              ///< Not alterable
		EA_MULTIPLIER,        ///< Allow changing the production multiplier
		EA_RATE,              ///< Allow changing the production rates
	};

	/** Specific lines in the info panel */
	enum InfoLine {
		IL_NONE,              ///< No line
		IL_MULTIPLIER,        ///< Production multiplier
		IL_RATE1,             ///< Production rate of cargo 1
		IL_RATE2,             ///< Production rate of cargo 2
	};

	Editability editable;     ///< Mode for changing production
	InfoLine editbox_line;    ///< The line clicked to open the edit box
	InfoLine clicked_line;    ///< The line of the button that has been clicked
	byte clicked_button;      ///< The button that has been clicked (to raise)
	int production_offset_y;  ///< The offset of the production texts/buttons
	int info_height;          ///< Height needed for the #WID_IV_INFO panel
	int cheat_line_height;    ///< Height of each line for the #WID_IV_INFO panel

public:
	IndustryViewWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc)
	{
		this->flags |= WF_DISABLE_VP_SCROLL;
		this->editbox_line = IL_NONE;
		this->clicked_line = IL_NONE;
		this->clicked_button = 0;
		this->info_height = WidgetDimensions::scaled.framerect.Vertical() + 2 * FONT_HEIGHT_NORMAL; // Info panel has at least two lines text.

		this->InitNested(window_number);
		NWidgetViewport *nvp = this->GetWidget<NWidgetViewport>(WID_IV_VIEWPORT);
		nvp->InitializeViewport(this, Industry::Get(window_number)->location.GetCenterTile(), ScaleZoomGUI(ZOOM_LVL_INDUSTRY));

		this->InvalidateData();
	}

	void OnInit() override
	{
		/* This only used when the cheat to alter industry production is enabled */
		this->cheat_line_height = std::max(SETTING_BUTTON_HEIGHT + WidgetDimensions::scaled.vsep_normal, FONT_HEIGHT_NORMAL);
	}

	void OnPaint() override
	{
		this->DrawWidgets();

		if (this->IsShaded()) return; // Don't draw anything when the window is shaded.

		const Rect r = this->GetWidget<NWidgetBase>(WID_IV_INFO)->GetCurrentRect();
		int expected = this->DrawInfo(r);
		if (expected != r.bottom) {
			this->info_height = expected - r.top + 1;
			this->ReInit();
			return;
		}
	}

	/**
	 * Draw the text in the #WID_IV_INFO panel.
	 * @param r Rectangle of the panel.
	 * @return Expected position of the bottom edge of the panel.
	 */
	int DrawInfo(const Rect &r)
	{
		bool rtl = _current_text_dir == TD_RTL;
		Industry *i = Industry::Get(this->window_number);
		const IndustrySpec *ind = GetIndustrySpec(i->type);
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		bool first = true;
		bool has_accept = false;

		if (i->prod_level == PRODLEVEL_CLOSURE) {
			DrawString(ir, STR_INDUSTRY_VIEW_INDUSTRY_ANNOUNCED_CLOSURE);
			ir.top += FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_wide;
		}

		CargoSuffix cargo_suffix[lengthof(i->accepts_cargo)];
		GetAllCargoSuffixes(CARGOSUFFIX_IN, CST_VIEW, i, i->type, ind, i->accepts_cargo, cargo_suffix);
		bool stockpiling = HasBit(ind->callback_mask, CBM_IND_PRODUCTION_CARGO_ARRIVAL) || HasBit(ind->callback_mask, CBM_IND_PRODUCTION_256_TICKS);

		for (byte j = 0; j < lengthof(i->accepts_cargo); j++) {
			if (i->accepts_cargo[j] == CT_INVALID) continue;
			has_accept = true;
			if (first) {
				DrawString(ir, STR_INDUSTRY_VIEW_REQUIRES);
				ir.top += FONT_HEIGHT_NORMAL;
				first = false;
			}
			SetDParam(0, CargoSpec::Get(i->accepts_cargo[j])->name);
			SetDParam(1, i->accepts_cargo[j]);
			SetDParam(2, i->incoming_cargo_waiting[j]);
			SetDParamStr(3, "");
			StringID str = STR_NULL;
			switch (cargo_suffix[j].display) {
				case CSD_CARGO_AMOUNT_TEXT:
					SetDParamStr(3, cargo_suffix[j].text);
					FALLTHROUGH;
				case CSD_CARGO_AMOUNT:
					str = stockpiling ? STR_INDUSTRY_VIEW_ACCEPT_CARGO_AMOUNT : STR_INDUSTRY_VIEW_ACCEPT_CARGO;
					break;

				case CSD_CARGO_TEXT:
					SetDParamStr(3, cargo_suffix[j].text);
					FALLTHROUGH;
				case CSD_CARGO:
					str = STR_INDUSTRY_VIEW_ACCEPT_CARGO;
					break;

				default:
					NOT_REACHED();
			}
			DrawString(ir.Indent(WidgetDimensions::scaled.hsep_indent, rtl), str);
			ir.top += FONT_HEIGHT_NORMAL;
		}

		GetAllCargoSuffixes(CARGOSUFFIX_OUT, CST_VIEW, i, i->type, ind, i->produced_cargo, cargo_suffix);
		int line_height = this->editable == EA_RATE ? this->cheat_line_height : FONT_HEIGHT_NORMAL;
		int text_y_offset = (line_height - FONT_HEIGHT_NORMAL) / 2;
		int button_y_offset = (line_height - SETTING_BUTTON_HEIGHT) / 2;
		first = true;
		for (byte j = 0; j < lengthof(i->produced_cargo); j++) {
			if (i->produced_cargo[j] == CT_INVALID) continue;
			if (first) {
				if (has_accept) ir.top += WidgetDimensions::scaled.vsep_wide;
				DrawString(ir, STR_INDUSTRY_VIEW_PRODUCTION_LAST_MONTH_TITLE);
				ir.top += FONT_HEIGHT_NORMAL;
				if (this->editable == EA_RATE) this->production_offset_y = ir.top;
				first = false;
			}

			SetDParam(0, i->produced_cargo[j]);
			SetDParam(1, i->last_month_production[j]);
			SetDParamStr(2, cargo_suffix[j].text);
			SetDParam(3, ToPercent8(i->last_month_pct_transported[j]));
			DrawString(ir.Indent(WidgetDimensions::scaled.hsep_indent + (this->editable == EA_RATE ? SETTING_BUTTON_WIDTH + WidgetDimensions::scaled.hsep_normal : 0), rtl).Translate(0, text_y_offset), STR_INDUSTRY_VIEW_TRANSPORTED);
			/* Let's put out those buttons.. */
			if (this->editable == EA_RATE) {
				DrawArrowButtons(ir.Indent(WidgetDimensions::scaled.hsep_indent, rtl).WithWidth(SETTING_BUTTON_WIDTH, rtl).left, ir.top + button_y_offset, COLOUR_YELLOW, (this->clicked_line == IL_RATE1 + j) ? this->clicked_button : 0,
						i->production_rate[j] > 0, i->production_rate[j] < 255);
			}
			ir.top += line_height;
		}

		/* Display production multiplier if editable */
		if (this->editable == EA_MULTIPLIER) {
			line_height = this->cheat_line_height;
			text_y_offset = (line_height - FONT_HEIGHT_NORMAL) / 2;
			button_y_offset = (line_height - SETTING_BUTTON_HEIGHT) / 2;
			ir.top += WidgetDimensions::scaled.vsep_wide;
			this->production_offset_y = ir.top;
			SetDParam(0, RoundDivSU(i->prod_level * 100, PRODLEVEL_DEFAULT));
			DrawString(ir.Indent(WidgetDimensions::scaled.hsep_indent + SETTING_BUTTON_WIDTH + WidgetDimensions::scaled.hsep_normal, rtl).Translate(0, text_y_offset), STR_INDUSTRY_VIEW_PRODUCTION_LEVEL);
			DrawArrowButtons(ir.Indent(WidgetDimensions::scaled.hsep_indent, rtl).WithWidth(SETTING_BUTTON_WIDTH, rtl).left, ir.top + button_y_offset, COLOUR_YELLOW, (this->clicked_line == IL_MULTIPLIER) ? this->clicked_button : 0,
					i->prod_level > PRODLEVEL_MINIMUM, i->prod_level < PRODLEVEL_MAXIMUM);
			ir.top += line_height;
		}

		/* Get the extra message for the GUI */
		if (HasBit(ind->callback_mask, CBM_IND_WINDOW_MORE_TEXT)) {
			uint16 callback_res = GetIndustryCallback(CBID_INDUSTRY_WINDOW_MORE_TEXT, 0, 0, i, i->type, i->location.tile);
			if (callback_res != CALLBACK_FAILED && callback_res != 0x400) {
				if (callback_res > 0x400) {
					ErrorUnknownCallbackResult(ind->grf_prop.grffile->grfid, CBID_INDUSTRY_WINDOW_MORE_TEXT, callback_res);
				} else {
					StringID message = GetGRFStringID(ind->grf_prop.grffile->grfid, 0xD000 + callback_res);
					if (message != STR_NULL && message != STR_UNDEFINED) {
						ir.top += WidgetDimensions::scaled.vsep_wide;

						StartTextRefStackUsage(ind->grf_prop.grffile, 6);
						/* Use all the available space left from where we stand up to the
						 * end of the window. We ALSO enlarge the window if needed, so we
						 * can 'go' wild with the bottom of the window. */
						ir.top = DrawStringMultiLine(ir.left, ir.right, ir.top, UINT16_MAX, message, TC_BLACK);
						StopTextRefStackUsage();
					}
				}
			}
		}

		if (!i->text.empty()) {
			SetDParamStr(0, i->text);
			ir.top += WidgetDimensions::scaled.vsep_wide;
			ir.top = DrawStringMultiLine(ir.left, ir.right, ir.top, UINT16_MAX, STR_JUST_RAW_STRING, TC_BLACK);
		}

		/* Return required bottom position, the last pixel row plus some padding. */
		return ir.top - 1 + WidgetDimensions::scaled.framerect.bottom;
	}

	void SetStringParameters(int widget) const override
	{
		if (widget == WID_IV_CAPTION) SetDParam(0, this->window_number);
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		if (widget == WID_IV_INFO) size->height = this->info_height;
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_IV_INFO: {
				Industry *i = Industry::Get(this->window_number);
				InfoLine line = IL_NONE;

				switch (this->editable) {
					case EA_NONE: break;

					case EA_MULTIPLIER:
						if (IsInsideBS(pt.y, this->production_offset_y, this->cheat_line_height)) line = IL_MULTIPLIER;
						break;

					case EA_RATE:
						if (pt.y >= this->production_offset_y) {
							int row = (pt.y - this->production_offset_y) / this->cheat_line_height;
							for (uint j = 0; j < lengthof(i->produced_cargo); j++) {
								if (i->produced_cargo[j] == CT_INVALID) continue;
								row--;
								if (row < 0) {
									line = (InfoLine)(IL_RATE1 + j);
									break;
								}
							}
						}
						break;
				}
				if (line == IL_NONE) return;

				bool rtl = _current_text_dir == TD_RTL;
				Rect r = this->GetWidget<NWidgetBase>(widget)->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect).Indent(WidgetDimensions::scaled.hsep_indent, rtl);

				if (r.WithWidth(SETTING_BUTTON_WIDTH, rtl).Contains(pt)) {
					/* Clicked buttons, decrease or increase production */
					bool decrease = r.WithWidth(SETTING_BUTTON_WIDTH / 2, rtl).Contains(pt);
					switch (this->editable) {
						case EA_MULTIPLIER:
							if (decrease) {
								if (i->prod_level <= PRODLEVEL_MINIMUM) return;
								i->prod_level = std::max<uint>(i->prod_level / 2, PRODLEVEL_MINIMUM);
							} else {
								if (i->prod_level >= PRODLEVEL_MAXIMUM) return;
								i->prod_level = std::min<uint>(i->prod_level * 2, PRODLEVEL_MAXIMUM);
							}
							break;

						case EA_RATE:
							if (decrease) {
								if (i->production_rate[line - IL_RATE1] <= 0) return;
								i->production_rate[line - IL_RATE1] = std::max(i->production_rate[line - IL_RATE1] / 2, 0);
							} else {
								if (i->production_rate[line - IL_RATE1] >= 255) return;
								/* a zero production industry is unlikely to give anything but zero, so push it a little bit */
								int new_prod = i->production_rate[line - IL_RATE1] == 0 ? 1 : i->production_rate[line - IL_RATE1] * 2;
								i->production_rate[line - IL_RATE1] = std::min<uint>(new_prod, 255);
							}
							break;

						default: NOT_REACHED();
					}

					UpdateIndustryProduction(i);
					this->SetDirty();
					this->SetTimeout();
					this->clicked_line = line;
					this->clicked_button = (decrease ^ rtl) ? 1 : 2;
				} else if (r.Indent(SETTING_BUTTON_WIDTH + WidgetDimensions::scaled.hsep_normal, rtl).Contains(pt)) {
					/* clicked the text */
					this->editbox_line = line;
					switch (this->editable) {
						case EA_MULTIPLIER:
							SetDParam(0, RoundDivSU(i->prod_level * 100, PRODLEVEL_DEFAULT));
							ShowQueryString(STR_JUST_INT, STR_CONFIG_GAME_PRODUCTION_LEVEL, 10, this, CS_ALPHANUMERAL, QSF_NONE);
							break;

						case EA_RATE:
							SetDParam(0, i->production_rate[line - IL_RATE1] * 8);
							ShowQueryString(STR_JUST_INT, STR_CONFIG_GAME_PRODUCTION, 10, this, CS_ALPHANUMERAL, QSF_NONE);
							break;

						default: NOT_REACHED();
					}
				}
				break;
			}

			case WID_IV_GOTO: {
				Industry *i = Industry::Get(this->window_number);
				if (_ctrl_pressed) {
					ShowExtraViewportWindow(i->location.GetCenterTile());
				} else {
					ScrollMainWindowToTile(i->location.GetCenterTile());
				}
				break;
			}

			case WID_IV_DISPLAY: {
				Industry *i = Industry::Get(this->window_number);
				ShowIndustryCargoesWindow(i->type);
				break;
			}
		}
	}

	void OnTimeout() override
	{
		this->clicked_line = IL_NONE;
		this->clicked_button = 0;
		this->SetDirty();
	}

	void OnResize() override
	{
		if (this->viewport != nullptr) {
			NWidgetViewport *nvp = this->GetWidget<NWidgetViewport>(WID_IV_VIEWPORT);
			nvp->UpdateViewportCoordinates(this);

			ScrollWindowToTile(Industry::Get(this->window_number)->location.GetCenterTile(), this, true); // Re-center viewport.
		}
	}

	void OnQueryTextFinished(char *str) override
	{
		if (StrEmpty(str)) return;

		Industry *i = Industry::Get(this->window_number);
		uint value = atoi(str);
		switch (this->editbox_line) {
			case IL_NONE: NOT_REACHED();

			case IL_MULTIPLIER:
				i->prod_level = ClampU(RoundDivSU(value * PRODLEVEL_DEFAULT, 100), PRODLEVEL_MINIMUM, PRODLEVEL_MAXIMUM);
				break;

			default:
				i->production_rate[this->editbox_line - IL_RATE1] = ClampU(RoundDivSU(value, 8), 0, 255);
				break;
		}
		UpdateIndustryProduction(i);
		this->SetDirty();
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		const Industry *i = Industry::Get(this->window_number);
		if (IsProductionAlterable(i)) {
			const IndustrySpec *ind = GetIndustrySpec(i->type);
			this->editable = ind->UsesOriginalEconomy() ? EA_MULTIPLIER : EA_RATE;
		} else {
			this->editable = EA_NONE;
		}
	}

	bool IsNewGRFInspectable() const override
	{
		return ::IsNewGRFInspectable(GSF_INDUSTRIES, this->window_number);
	}

	void ShowNewGRFInspectWindow() const override
	{
		::ShowNewGRFInspectWindow(GSF_INDUSTRIES, this->window_number);
	}
};

static void UpdateIndustryProduction(Industry *i)
{
	const IndustrySpec *indspec = GetIndustrySpec(i->type);
	if (indspec->UsesOriginalEconomy()) i->RecomputeProductionMultipliers();

	for (byte j = 0; j < lengthof(i->produced_cargo); j++) {
		if (i->produced_cargo[j] != CT_INVALID) {
			i->last_month_production[j] = 8 * i->production_rate[j];
		}
	}
}

/** Widget definition of the view industry gui */
static const NWidgetPart _nested_industry_view_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_CREAM),
		NWidget(WWT_CAPTION, COLOUR_CREAM, WID_IV_CAPTION), SetDataTip(STR_INDUSTRY_VIEW_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_PUSHIMGBTN, COLOUR_CREAM, WID_IV_GOTO), SetMinimalSize(12, 14), SetDataTip(SPR_GOTO_LOCATION, STR_INDUSTRY_VIEW_LOCATION_TOOLTIP),
		// R1: dropped DEBUGBOX/SHADEBOX/DEFSIZEBOX/STICKYBOX — their GUI sprites aren't in the loaded
		// base GRF. Closebox + caption + the goto pushimgbtn (SPR_GOTO_LOCATION is in the base GRF) stay.
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_CREAM),
		NWidget(WWT_INSET, COLOUR_CREAM), SetPadding(2, 2, 2, 2),
			NWidget(NWID_VIEWPORT, INVALID_COLOUR, WID_IV_VIEWPORT), SetMinimalSize(254, 86), SetFill(1, 0), SetResize(1, 1),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_CREAM, WID_IV_INFO), SetMinimalSize(260, 0), SetMinimalTextLines(2, WidgetDimensions::unscaled.framerect.Vertical()), SetResize(1, 0),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_CREAM, WID_IV_DISPLAY), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_INDUSTRY_DISPLAY_CHAIN, STR_INDUSTRY_DISPLAY_CHAIN_TOOLTIP),
		NWidget(WWT_RESIZEBOX, COLOUR_CREAM),
	EndContainer(),
};

/** Window definition of the view industry gui */
static WindowDesc _industry_view_desc(
	WDP_AUTO, "view_industry", 260, 120,
	WC_INDUSTRY_VIEW, WC_NONE,
	0,
	_nested_industry_view_widgets, lengthof(_nested_industry_view_widgets)
);

void ShowIndustryViewWindow(int industry)
{
	AllocateWindowDescFront<IndustryViewWindow>(&_industry_view_desc, industry);
}

// ----------------------------------------------------------------------------
// Verbatim slice from OpenTTD src/industry_gui.cpp:1217-1858 (IndustryDirectoryWindow).
// ----------------------------------------------------------------------------

/** Widget definition of the industry directory gui */
static const NWidgetPart _nested_industry_directory_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN), SetDataTip(STR_INDUSTRY_DIRECTORY_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		// R1: dropped SHADEBOX/DEFSIZEBOX/STICKYBOX — their GUI sprites aren't in the loaded base GRF.
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_TEXTBTN, COLOUR_BROWN, WID_ID_DROPDOWN_ORDER), SetDataTip(STR_BUTTON_SORT_BY, STR_TOOLTIP_SORT_ORDER),
				NWidget(WWT_DROPDOWN, COLOUR_BROWN, WID_ID_DROPDOWN_CRITERIA), SetDataTip(STR_JUST_STRING, STR_TOOLTIP_SORT_CRITERIA),
				NWidget(WWT_DROPDOWN, COLOUR_BROWN, WID_ID_FILTER_BY_ACC_CARGO), SetMinimalSize(225, 12), SetFill(0, 1), SetDataTip(STR_INDUSTRY_DIRECTORY_ACCEPTED_CARGO_FILTER, STR_TOOLTIP_FILTER_CRITERIA),
				NWidget(WWT_DROPDOWN, COLOUR_BROWN, WID_ID_FILTER_BY_PROD_CARGO), SetMinimalSize(225, 12), SetFill(0, 1), SetDataTip(STR_INDUSTRY_DIRECTORY_PRODUCED_CARGO_FILTER, STR_TOOLTIP_FILTER_CRITERIA),
				NWidget(WWT_PANEL, COLOUR_BROWN), SetResize(1, 0), EndContainer(),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_BROWN, WID_ID_INDUSTRY_LIST), SetDataTip(0x0, STR_INDUSTRY_DIRECTORY_LIST_CAPTION), SetResize(1, 1), SetScrollbar(WID_ID_SCROLLBAR), EndContainer(),
		EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_BROWN, WID_ID_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_BROWN),
		EndContainer(),
	EndContainer(),
};

typedef GUIList<const Industry *, const std::pair<CargoID, CargoID> &> GUIIndustryList;

/** Special cargo filter criteria */
enum CargoFilterSpecialType {
	CF_ANY  = CT_NO_REFIT, ///< Show all industries (i.e. no filtering)
	CF_NONE = CT_INVALID,  ///< Show only industries which do not produce/accept cargo
};

/**
 * Check whether an industry accepts and produces a certain cargo pair.
 */
static bool CDECL CargoFilter(const Industry * const *industry, const std::pair<CargoID, CargoID> &cargoes)
{
	auto accepted_cargo = cargoes.first;
	auto produced_cargo = cargoes.second;

	bool accepted_cargo_matches;

	switch (accepted_cargo) {
		case CF_ANY:
			accepted_cargo_matches = true;
			break;

		case CF_NONE:
			accepted_cargo_matches = std::all_of(std::begin((*industry)->accepts_cargo), std::end((*industry)->accepts_cargo), [](CargoID cargo) {
				return cargo == CT_INVALID;
			});
			break;

		default:
			const auto &ac = (*industry)->accepts_cargo;
			accepted_cargo_matches = std::find(std::begin(ac), std::end(ac), accepted_cargo) != std::end(ac);
			break;
	}

	bool produced_cargo_matches;

	switch (produced_cargo) {
		case CF_ANY:
			produced_cargo_matches = true;
			break;

		case CF_NONE:
			produced_cargo_matches = std::all_of(std::begin((*industry)->produced_cargo), std::end((*industry)->produced_cargo), [](CargoID cargo) {
				return cargo == CT_INVALID;
			});
			break;

		default:
			const auto &pc = (*industry)->produced_cargo;
			produced_cargo_matches = std::find(std::begin(pc), std::end(pc), produced_cargo) != std::end(pc);
			break;
	}

	return accepted_cargo_matches && produced_cargo_matches;
}

static GUIIndustryList::FilterFunction * const _filter_funcs[] = { &CargoFilter };


/**
 * The list of industries.
 */
class IndustryDirectoryWindow : public Window {
protected:
	/* Runtime saved values */
	static Listing last_sorting;

	/* Constants for sorting industries */
	static const StringID sorter_names[];
	static GUIIndustryList::SortFunction * const sorter_funcs[];

	GUIIndustryList industries;
	Scrollbar *vscroll;

	CargoID cargo_filter[NUM_CARGO + 2];        ///< Available cargo filters; CargoID or CF_ANY or CF_NONE
	StringID cargo_filter_texts[NUM_CARGO + 3]; ///< Texts for filter_cargo, terminated by INVALID_STRING_ID
	byte produced_cargo_filter_criteria;        ///< Selected produced cargo filter index
	byte accepted_cargo_filter_criteria;        ///< Selected accepted cargo filter index
	static CargoID produced_cargo_filter;

	enum class SorterType : uint8 {
		ByName,        ///< Sorter type to sort by name
		ByType,        ///< Sorter type to sort by type
		ByProduction,  ///< Sorter type to sort by production amount
		ByTransported, ///< Sorter type to sort by transported percentage
	};

	/**
	 * Set cargo filter list item index.
	 * @param index The index of the cargo to be set
	 */
	void SetProducedCargoFilterIndex(byte index)
	{
		if (this->produced_cargo_filter_criteria != index) {
			this->produced_cargo_filter_criteria = index;
			/* deactivate filter if criteria is 'Show All', activate it otherwise */
			bool is_filtering_necessary = this->cargo_filter[this->produced_cargo_filter_criteria] != CF_ANY || this->cargo_filter[this->accepted_cargo_filter_criteria] != CF_ANY;

			this->industries.SetFilterState(is_filtering_necessary);
			this->industries.SetFilterType(0);
			this->industries.ForceRebuild();
		}
	}

	/**
	 * Set cargo filter list item index.
	 * @param index The index of the cargo to be set
	 */
	void SetAcceptedCargoFilterIndex(byte index)
	{
		if (this->accepted_cargo_filter_criteria != index) {
			this->accepted_cargo_filter_criteria = index;
			/* deactivate filter if criteria is 'Show All', activate it otherwise */
			bool is_filtering_necessary = this->cargo_filter[this->produced_cargo_filter_criteria] != CF_ANY || this->cargo_filter[this->accepted_cargo_filter_criteria] != CF_ANY;

			this->industries.SetFilterState(is_filtering_necessary);
			this->industries.SetFilterType(0);
			this->industries.ForceRebuild();
		}
	}

	/**
	 * Populate the filter list and set the cargo filter criteria.
	 */
	void SetCargoFilterArray()
	{
		byte filter_items = 0;

		/* Add item for disabling filtering. */
		this->cargo_filter[filter_items] = CF_ANY;
		this->cargo_filter_texts[filter_items] = STR_INDUSTRY_DIRECTORY_FILTER_ALL_TYPES;
		this->produced_cargo_filter_criteria = filter_items;
		this->accepted_cargo_filter_criteria = filter_items;
		filter_items++;

		/* Add item for industries not producing anything, e.g. power plants */
		this->cargo_filter[filter_items] = CF_NONE;
		this->cargo_filter_texts[filter_items] = STR_INDUSTRY_DIRECTORY_FILTER_NONE;
		filter_items++;

		/* Collect available cargo types for filtering. */
		for (const CargoSpec *cs : _sorted_standard_cargo_specs) {
			this->cargo_filter[filter_items] = cs->Index();
			this->cargo_filter_texts[filter_items] = cs->name;
			filter_items++;
		}

		/* Terminate the filter list. */
		this->cargo_filter_texts[filter_items] = INVALID_STRING_ID;

		this->industries.SetFilterFuncs(_filter_funcs);

		bool is_filtering_necessary = this->cargo_filter[this->produced_cargo_filter_criteria] != CF_ANY || this->cargo_filter[this->accepted_cargo_filter_criteria] != CF_ANY;

		this->industries.SetFilterState(is_filtering_necessary);
	}

	/** (Re)Build industries list */
	void BuildSortIndustriesList()
	{
		if (this->industries.NeedRebuild()) {
			this->industries.clear();

			for (const Industry *i : Industry::Iterate()) {
				this->industries.push_back(i);
			}

			this->industries.shrink_to_fit();
			this->industries.RebuildDone();
		}

		auto filter = std::make_pair(this->cargo_filter[this->accepted_cargo_filter_criteria],
		                             this->cargo_filter[this->produced_cargo_filter_criteria]);

		this->industries.Filter(filter);

		IndustryDirectoryWindow::produced_cargo_filter = this->cargo_filter[this->produced_cargo_filter_criteria];
		this->industries.Sort();

		this->vscroll->SetCount((uint)this->industries.size()); // Update scrollbar as well.

		this->SetDirty();
	}

	/**
	 * Returns percents of cargo transported if industry produces this cargo, else -1
	 */
	static inline int GetCargoTransportedPercentsIfValid(const Industry *i, uint id)
	{
		assert(id < lengthof(i->produced_cargo));

		if (i->produced_cargo[id] == CT_INVALID) return -1;
		return ToPercent8(i->last_month_pct_transported[id]);
	}

	/**
	 * Returns value representing industry's transported cargo
	 *  percentage for industry sorting
	 */
	static int GetCargoTransportedSortValue(const Industry *i)
	{
		CargoID filter = IndustryDirectoryWindow::produced_cargo_filter;
		if (filter == CF_NONE) return 0;

		int percentage = 0, produced_cargo_count = 0;
		for (uint id = 0; id < lengthof(i->produced_cargo); id++) {
			if (filter == CF_ANY) {
				int transported = GetCargoTransportedPercentsIfValid(i, id);
				if (transported != -1) {
					produced_cargo_count++;
					percentage += transported;
				}
				if (produced_cargo_count == 0 && id == lengthof(i->produced_cargo) - 1 && percentage == 0) {
					return transported;
				}
			} else if (filter == i->produced_cargo[id]) {
				return GetCargoTransportedPercentsIfValid(i, id);
			}
		}

		if (produced_cargo_count == 0) return percentage;
		return percentage / produced_cargo_count;
	}

	/** Sort industries by name */
	static bool IndustryNameSorter(const Industry * const &a, const Industry * const &b)
	{
		int r = strnatcmp(a->GetCachedName(), b->GetCachedName()); // Sort by name (natural sorting).
		if (r == 0) return a->index < b->index;
		return r < 0;
	}

	/** Sort industries by type and name */
	static bool IndustryTypeSorter(const Industry * const &a, const Industry * const &b)
	{
		int it_a = 0;
		while (it_a != NUM_INDUSTRYTYPES && a->type != _sorted_industry_types[it_a]) it_a++;
		int it_b = 0;
		while (it_b != NUM_INDUSTRYTYPES && b->type != _sorted_industry_types[it_b]) it_b++;
		int r = it_a - it_b;
		return (r == 0) ? IndustryNameSorter(a, b) : r < 0;
	}

	/** Sort industries by production and name */
	static bool IndustryProductionSorter(const Industry * const &a, const Industry * const &b)
	{
		CargoID filter = IndustryDirectoryWindow::produced_cargo_filter;
		if (filter == CF_NONE) return IndustryTypeSorter(a, b);

		uint prod_a = 0, prod_b = 0;
		for (uint i = 0; i < lengthof(a->produced_cargo); i++) {
			if (filter == CF_ANY) {
				if (a->produced_cargo[i] != CT_INVALID) prod_a += a->last_month_production[i];
				if (b->produced_cargo[i] != CT_INVALID) prod_b += b->last_month_production[i];
			} else {
				if (a->produced_cargo[i] == filter) prod_a += a->last_month_production[i];
				if (b->produced_cargo[i] == filter) prod_b += b->last_month_production[i];
			}
		}
		int r = prod_a - prod_b;

		return (r == 0) ? IndustryTypeSorter(a, b) : r < 0;
	}

	/** Sort industries by transported cargo and name */
	static bool IndustryTransportedCargoSorter(const Industry * const &a, const Industry * const &b)
	{
		int r = GetCargoTransportedSortValue(a) - GetCargoTransportedSortValue(b);
		return (r == 0) ? IndustryNameSorter(a, b) : r < 0;
	}

	/**
	 * Get the StringID to draw and set the appropriate DParams.
	 * @param i the industry to get the StringID of.
	 * @return the StringID.
	 */
	StringID GetIndustryString(const Industry *i) const
	{
		const IndustrySpec *indsp = GetIndustrySpec(i->type);
		byte p = 0;

		/* Industry name */
		SetDParam(p++, i->index);

		static CargoSuffix cargo_suffix[lengthof(i->produced_cargo)];
		GetAllCargoSuffixes(CARGOSUFFIX_OUT, CST_DIR, i, i->type, indsp, i->produced_cargo, cargo_suffix);

		/* Get industry productions (CargoID, production, suffix, transported) */
		struct CargoInfo {
			CargoID cargo_id;
			uint16 production;
			const char *suffix;
			uint transported;
		};
		std::vector<CargoInfo> cargos;

		for (byte j = 0; j < lengthof(i->produced_cargo); j++) {
			if (i->produced_cargo[j] == CT_INVALID) continue;
			cargos.push_back({ i->produced_cargo[j], i->last_month_production[j], cargo_suffix[j].text, ToPercent8(i->last_month_pct_transported[j]) });
		}

		switch (static_cast<IndustryDirectoryWindow::SorterType>(this->industries.SortType())) {
			case IndustryDirectoryWindow::SorterType::ByName:
			case IndustryDirectoryWindow::SorterType::ByType:
			case IndustryDirectoryWindow::SorterType::ByProduction:
				/* Sort by descending production, then descending transported */
				std::sort(cargos.begin(), cargos.end(), [](const CargoInfo &a, const CargoInfo &b) {
					if (a.production != b.production) return a.production > b.production;
					return a.transported > b.transported;
				});
				break;

			case IndustryDirectoryWindow::SorterType::ByTransported:
				/* Sort by descending transported, then descending production */
				std::sort(cargos.begin(), cargos.end(), [](const CargoInfo &a, const CargoInfo &b) {
					if (a.transported != b.transported) return a.transported > b.transported;
					return a.production > b.production;
				});
				break;
		}

		/* If the produced cargo filter is active then move the filtered cargo to the beginning of the list,
		 * because this is the one the player interested in, and that way it is not hidden in the 'n' more cargos */
		const CargoID cid = this->cargo_filter[this->produced_cargo_filter_criteria];
		if (cid != CF_ANY && cid != CF_NONE) {
			auto filtered_ci = std::find_if(cargos.begin(), cargos.end(), [cid](const CargoInfo &ci) -> bool {
				return ci.cargo_id == cid;
			});
			if (filtered_ci != cargos.end()) {
				std::rotate(cargos.begin(), filtered_ci, filtered_ci + 1);
			}
		}

		/* Display first 3 cargos */
		for (size_t j = 0; j < std::min<size_t>(3, cargos.size()); j++) {
			CargoInfo ci = cargos[j];
			SetDParam(p++, STR_INDUSTRY_DIRECTORY_ITEM_INFO);
			SetDParam(p++, ci.cargo_id);
			SetDParam(p++, ci.production);
			SetDParamStr(p++, ci.suffix);
			SetDParam(p++, ci.transported);
		}

		/* Undisplayed cargos if any */
		SetDParam(p++, cargos.size() - 3);

		/* Drawing the right string */
		switch (cargos.size()) {
			case 0: return STR_INDUSTRY_DIRECTORY_ITEM_NOPROD;
			case 1: return STR_INDUSTRY_DIRECTORY_ITEM_PROD1;
			case 2: return STR_INDUSTRY_DIRECTORY_ITEM_PROD2;
			case 3: return STR_INDUSTRY_DIRECTORY_ITEM_PROD3;
			default: return STR_INDUSTRY_DIRECTORY_ITEM_PRODMORE;
		}
	}

public:
	IndustryDirectoryWindow(WindowDesc *desc, WindowNumber number) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_ID_SCROLLBAR);

		this->industries.SetListing(this->last_sorting);
		this->industries.SetSortFuncs(IndustryDirectoryWindow::sorter_funcs);
		this->industries.ForceRebuild();
		this->BuildSortIndustriesList();

		this->FinishInitNested(0);
	}

	~IndustryDirectoryWindow()
	{
		this->last_sorting = this->industries.GetListing();
	}

	void OnInit() override
	{
		this->SetCargoFilterArray();
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_ID_DROPDOWN_CRITERIA:
				SetDParam(0, IndustryDirectoryWindow::sorter_names[this->industries.SortType()]);
				break;

			case WID_ID_FILTER_BY_ACC_CARGO:
				SetDParam(0, this->cargo_filter_texts[this->accepted_cargo_filter_criteria]);
				break;

			case WID_ID_FILTER_BY_PROD_CARGO:
				SetDParam(0, this->cargo_filter_texts[this->produced_cargo_filter_criteria]);
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_ID_DROPDOWN_ORDER:
				this->DrawSortButtonState(widget, this->industries.IsDescSortOrder() ? SBS_DOWN : SBS_UP);
				break;

			case WID_ID_INDUSTRY_LIST: {
				int n = 0;
				Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
				if (this->industries.size() == 0) {
					DrawString(ir, STR_INDUSTRY_DIRECTORY_NONE);
					break;
				}
				TextColour tc;
				const CargoID acf_cid = this->cargo_filter[this->accepted_cargo_filter_criteria];
				for (uint i = this->vscroll->GetPosition(); i < this->industries.size(); i++) {
					tc = TC_FROMSTRING;
					if (acf_cid != CF_ANY && acf_cid != CF_NONE) {
						Industry *ind = const_cast<Industry *>(this->industries[i]);
						if (IndustryTemporarilyRefusesCargo(ind, acf_cid)) {
							tc = TC_GREY | TC_FORCED;
						}
					}
					DrawString(ir, this->GetIndustryString(this->industries[i]), tc);

					ir.top += this->resize.step_height;
					if (++n == this->vscroll->GetCapacity()) break; // max number of industries in 1 window
				}
				break;
			}
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_ID_DROPDOWN_ORDER: {
				Dimension d = GetStringBoundingBox(this->GetWidget<NWidgetCore>(widget)->widget_data);
				d.width += padding.width + Window::SortButtonWidth() * 2; // Doubled since the string is centred and it also looks better.
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			case WID_ID_DROPDOWN_CRITERIA: {
				Dimension d = {0, 0};
				for (uint i = 0; IndustryDirectoryWindow::sorter_names[i] != INVALID_STRING_ID; i++) {
					d = maxdim(d, GetStringBoundingBox(IndustryDirectoryWindow::sorter_names[i]));
				}
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			case WID_ID_INDUSTRY_LIST: {
				Dimension d = GetStringBoundingBox(STR_INDUSTRY_DIRECTORY_NONE);
				for (uint i = 0; i < this->industries.size(); i++) {
					d = maxdim(d, GetStringBoundingBox(this->GetIndustryString(this->industries[i])));
				}
				resize->height = d.height;
				d.height *= 5;
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}
		}
	}


	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_ID_DROPDOWN_ORDER:
				this->industries.ToggleSortOrder();
				this->SetDirty();
				break;

			case WID_ID_DROPDOWN_CRITERIA:
				ShowDropDownMenu(this, IndustryDirectoryWindow::sorter_names, this->industries.SortType(), WID_ID_DROPDOWN_CRITERIA, 0, 0);
				break;

			case WID_ID_FILTER_BY_ACC_CARGO: // Cargo filter dropdown
				ShowDropDownMenu(this, this->cargo_filter_texts, this->accepted_cargo_filter_criteria, WID_ID_FILTER_BY_ACC_CARGO, 0, 0);
				break;

			case WID_ID_FILTER_BY_PROD_CARGO: // Cargo filter dropdown
				ShowDropDownMenu(this, this->cargo_filter_texts, this->produced_cargo_filter_criteria, WID_ID_FILTER_BY_PROD_CARGO, 0, 0);
				break;

			case WID_ID_INDUSTRY_LIST: {
				uint p = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_ID_INDUSTRY_LIST, WidgetDimensions::scaled.framerect.top);
				if (p < this->industries.size()) {
					if (_ctrl_pressed) {
						ShowExtraViewportWindow(this->industries[p]->location.tile);
					} else {
						ScrollMainWindowToTile(this->industries[p]->location.tile);
					}
				}
				break;
			}
		}
	}

	void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_ID_DROPDOWN_CRITERIA: {
				if (this->industries.SortType() != index) {
					this->industries.SetSortType(index);
					this->BuildSortIndustriesList();
				}
				break;
			}

			case WID_ID_FILTER_BY_ACC_CARGO: {
				this->SetAcceptedCargoFilterIndex(index);
				this->BuildSortIndustriesList();
				break;
			}

			case WID_ID_FILTER_BY_PROD_CARGO: {
				this->SetProducedCargoFilterIndex(index);
				this->BuildSortIndustriesList();
				break;
			}
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_ID_INDUSTRY_LIST);
	}

	void OnPaint() override
	{
		if (this->industries.NeedRebuild()) this->BuildSortIndustriesList();
		this->DrawWidgets();
	}

	void OnHundredthTick() override
	{
		this->industries.ForceResort();
		this->BuildSortIndustriesList();
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		switch (data) {
			case IDIWD_FORCE_REBUILD:
				/* This needs to be done in command-scope to enforce rebuilding before resorting invalid data */
				this->industries.ForceRebuild();
				break;

			case IDIWD_PRODUCTION_CHANGE:
				if (this->industries.SortType() == 2) this->industries.ForceResort();
				break;

			default:
				this->industries.ForceResort();
				break;
		}
	}
};

Listing IndustryDirectoryWindow::last_sorting = {false, 0};

/* Available station sorting functions. */
GUIIndustryList::SortFunction * const IndustryDirectoryWindow::sorter_funcs[] = {
	&IndustryNameSorter,
	&IndustryTypeSorter,
	&IndustryProductionSorter,
	&IndustryTransportedCargoSorter
};

/* Names of the sorting functions */
const StringID IndustryDirectoryWindow::sorter_names[] = {
	STR_SORT_BY_NAME,
	STR_SORT_BY_TYPE,
	STR_SORT_BY_PRODUCTION,
	STR_SORT_BY_TRANSPORTED,
	INVALID_STRING_ID
};

CargoID IndustryDirectoryWindow::produced_cargo_filter = CF_ANY;


/** Window definition of the industry directory gui */
static WindowDesc _industry_directory_desc(
	WDP_AUTO, "list_industries", 428, 190,
	WC_INDUSTRY_DIRECTORY, WC_NONE,
	0,
	_nested_industry_directory_widgets, lengthof(_nested_industry_directory_widgets)
);

void ShowIndustryDirectory()
{
	AllocateWindowDescFront<IndustryDirectoryWindow>(&_industry_directory_desc, 0);
}

// ----------------------------------------------------------------------------
// R1 stubs: the uncompiled industry / NewGRF-industry cascade (industry_cmd.cpp,
// newgrf_industries.cpp, newgrf_debug.cpp). All are inert:
//   * GetIndustrySpec returns a single zeroed IndustrySpec — callback_mask is 0, so every
//     NewGRF callback path in the slices above is dead, and the windows draw the REAL pooled
//     Industry object's cargo/production arrays. This is the graceful-degradation the port wants.
//   * The IndustrySpec member helpers report "not raw / original economy" (safe, disables the
//     production-editing arrows).
//   * The industry NewGRF callback helpers no-op.
//   * The NewGRF-inspect helpers report "not inspectable".
//   * ShowIndustryCargoesWindow (the display-chain button target) is a no-op — the cargoes
//     window itself is not extracted.
// ----------------------------------------------------------------------------

static IndustrySpec _m1_industry_spec_stub;   ///< One zeroed spec handed out for every industry type.

const IndustrySpec *GetIndustrySpec(IndustryType) { return &_m1_industry_spec_stub; }

IndustrySpec::~IndustrySpec() {}
bool IndustrySpec::IsRawIndustry() const { return false; }
bool IndustrySpec::IsProcessingIndustry() const { return false; }
Money IndustrySpec::GetConstructionCost() const { return 0; }
Money IndustrySpec::GetRemovalCost() const { return 0; }
bool IndustrySpec::UsesOriginalEconomy() const { return true; }

uint16 GetIndustryCallback(CallbackID, uint32, uint32, Industry *, IndustryType, TileIndex) { return CALLBACK_FAILED; }
uint32 GetIndustryProbabilityCallback(IndustryType, IndustryAvailabilityCallType, uint32 default_prob) { return default_prob; }
bool IndustryTemporarilyRefusesCargo(Industry *, CargoID) { return false; }

static void ShowIndustryCargoesWindow(IndustryType) {}

/* R1-91 link closers surfaced at the full link (real homes are uncompiled TUs). All inert: our
 * zeroed IndustrySpec (callback_mask=0) makes every NewGRF path dead, and the production-arrow /
 * inspect buttons are non-functional but present. */
void Industry::RecomputeProductionMultipliers() {}
void Industry::FillCachedName() const {}
bool IsNewGRFInspectable(GrfSpecFeature, uint) { return false; }
void ShowNewGRFInspectWindow(GrfSpecFeature, uint, const uint32) {}
void DrawArrowButtons(int, int, Colours, byte, bool, bool) {}
