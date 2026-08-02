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
 * m1_rvstub_econ.cpp — the economy/cargo half of the link surface that the REAL
 * road-vehicle TUs drag in. Now that roadveh_cmd.cpp / vehicle.cpp / order_cmd.cpp are
 * compiled for real, the linker wants the whole loading-and-paying apparatus that a real
 * bus arriving at a real station would touch: economy.cpp's load/unload state machine,
 * cargopacket.cpp's CargoList movers, newgrf_station.cpp's animation triggers,
 * linkgraph's IncreaseStats. Those four subsystems are exactly the cascade this port has
 * spent its whole life staying out of (see m1_economy.cpp, m1_station.cpp), so they are
 * not coming back in through the vehicle door.
 *
 * TWO of these are REAL, because they sit on the path a moving bus actually walks:
 *
 *  - Station::GetPrimaryRoadStop(const RoadVehicle *) — roadveh_cmd.cpp asks for it every
 *    time a bus reaches a stop, and vehicle.cpp uses a non-null answer as "this station can
 *    serve you". A stub returning nullptr would quietly make every R1 station unusable, so
 *    this walks the station's real bus_stops list (m1_station.cpp builds it) exactly like
 *    station.cpp does.
 *  - SubtractMoneyFromCompanyFract — roadveh_cmd.cpp charges the per-tick running cost
 *    through it. Money is a thing the player watches (R1-117 put the balance on screen), so
 *    it must really move; it delegates to the port's own SubtractMoneyFromCompany.
 *
 * The other thirteen are HONEST stubs, not unfinished work. R1 does not use the game's
 * loading machinery at all: cargo is generated, carried and delivered by hand in
 * m1_station.cpp (r1_station_add_cargo / r1_deliver_cargo) against a simplified fare, and
 * the vehicle's own VehicleCargoList is never populated. If PrepareUnload/LoadUnloadStation
 * ran for real they would fight that hand-rolled accounting — pay twice, or move packets
 * that R1 has already accounted for. So they do nothing, deliberately: the cargo lists stay
 * empty, and every mover below is asked to move cargo out of an empty list, which is
 * genuinely "moved nothing". The NewGRF triggers and the linkgraph stats describe features
 * that are absent (no station GRFs are loaded, there is no link graph), and the floating
 * cost/income text is a cosmetic effect the R1 viewport does not draw.
 */
#include "stdafx.h"

#include "station_base.h"      /* Station, RoadStop, BaseStation */
#include "roadveh.h"           /* RoadVehicle, IsBus() */
#include "cargopacket.h"       /* VehicleCargoList, StationCargoList, CargoPacketList */
#include "economy_func.h"      /* GetPrice, PrepareUnload, LoadUnloadStation, _price */
#include "economy_type.h"      /* Price, PR_END, Money */
#include "station_func.h"      /* IncreaseStats */
#include "newgrf_station.h"    /* TriggerStationAnimation / TriggerStationRandomisation */
#include "texteff.hpp"         /* ShowCostOrIncomeAnimation */
#include "company_func.h"      /* SubtractMoneyFromCompanyFract, SubtractMoneyFromCompany, _current_company */
#include "command_type.h"      /* CommandCost — command_func.h would add a file-static CMD_ERROR
                                * (and its dynamic initialiser) we have no use for */

#include "safeguards.h"

/* ---------------------------------------------------------------- the real two ------ */

/**
 * Real, from station.cpp:178. Returns the first bus stop of this station.
 *
 * The real loop then filters on two things we can guarantee away: a road-type match
 * (R1 lays exactly one road type, so every stop is compatible with every bus) and the
 * articulated-vehicle-vs-standard-stop rule (R1 has no articulated vehicles). Dropping
 * those two tests also drops HasTileAnyRoadType / HasArticulatedPart, i.e. the
 * newgrf_engine articulated-part cascade. R1 likewise builds no tram or truck stops, so
 * the ROADSTOP_BUS/ROADSTOP_TRUCK split collapses to bus_stops — but the IsBus() test is
 * kept honest anyway, so a truck asking here gets truck_stops (always nullptr today)
 * instead of being silently handed a bus stop.
 */
RoadStop *Station::GetPrimaryRoadStop(const RoadVehicle *v) const
{
	return this->GetPrimaryRoadStop(v->IsBus() ? ROADSTOP_BUS : ROADSTOP_TRUCK);
}

/**
 * Real money, from company_cmd.cpp:258 (that TU drags network_base.h — the reason
 * m1_company.cpp/m1_economy.cpp exist). Callers pass a cost in 1/256ths of a pound so that
 * a per-tick running cost of less than a penny still accumulates; the real function keeps
 * the leftover in Company::money_fraction.
 *
 * We deliberately do NOT reimplement that accumulator here: the port already has one
 * working money mutator (SubtractMoneyFromCompany in m1_economy.cpp, which owns the expense
 * buckets and the window invalidation), and two independent paths into a company's balance
 * is how accounting drifts. So we shift the fraction off and charge the whole pennies to it.
 * The sub-penny remainder is DROPPED rather than carried — over a session that undercharges
 * running costs by well under a pound per vehicle, which is invisible next to the daily
 * running cost m1_economy.cpp already levies in EnginesDailyLoop.
 */
void SubtractMoneyFromCompanyFract(CompanyID company, const CommandCost &cst)
{
	Money cost = cst.GetCost() >> 8;
	if (cost == 0) return;

	CompanyID save = _current_company;
	_current_company = company;
	SubtractMoneyFromCompany(CommandCost(cst.GetExpensesType(), cost));
	_current_company = save;
}

/* ------------------------------------------------------------- the honest thirteen -- */

/**
 * Real shape, from economy.cpp:961, minus the NewGRF branch. The price-base multipliers
 * only exist for cargo/vehicle GRFs, and this port loads no NewGRFs at all (only the base
 * graphics set), so grf_file is always nullptr on every path that reaches us — reading
 * GRFFile::price_base_multipliers would mean pulling in newgrf.h for a value that is
 * always zero. What is left is the plain scaled base price, which is the honest answer.
 */
Money GetPrice(Price index, uint cost_factor, const GRFFile *grf_file, int shift)
{
	if (index >= PR_END) return 0;

	Money cost = _price[index] * cost_factor;
	if (shift >= 0) {
		cost <<= shift;
	} else {
		cost >>= -shift;
	}
	return cost;
}

/* There is no link graph in this port (linkgraph/ is not compiled and no GoodsEntry ever
 * gets a node), so there are no edge capacities to raise. */
void IncreaseStats(Station *, const Vehicle *, StationID, uint32) {}

/* economy.cpp's load/unload state machine. R1 moves cargo by hand in m1_station.cpp and
 * pays for it through r1_deliver_cargo; letting the real machinery run alongside that would
 * double-count. Vehicle cargo lists stay empty and no vehicle ever enters LOADING. */
void PrepareUnload(Vehicle *) {}
void LoadUnloadStation(Station *) {}

/* misc_gui.cpp's floating "-£123" text. It needs the text-effect pool and the string
 * system's DParam plumbing; the R1 viewport draws no text effects, so the money still
 * moves, it just doesn't fly off the bus. */
void ShowCostOrIncomeAnimation(int, int, int, Money) {}

/* Repaints the RAIL station tiles when their cargo display changes. R1 stations are road
 * stops with no custom station GRF (speclist is always empty) and no train_station area, so
 * the real function's very first checks would return immediately anyway. */
void Station::MarkTilesDirty(bool) const {}

/* Station NewGRF callbacks. No station GRF is loaded, so there is nothing to animate and
 * no random bits to re-roll. */
void TriggerStationAnimation(BaseStation *, TileIndex, StationAnimationTrigger, CargoID) {}
void TriggerStationRandomisation(Station *, TileIndex, StationRandomTrigger, CargoID) {}

/* The CargoList movers live in cargopacket.cpp / cargoaction.cpp, which we keep out of the
 * build on purpose: they instantiate the CargoAction functor family and drag FlowStat and
 * the link-graph types with them. Every one of these operates on a vehicle cargo list that
 * R1 never fills, so "moved 0 of max_move" is the truthful result, not a placeholder. */
uint VehicleCargoList::Return(uint, StationCargoList *, StationID) { return 0; }
uint VehicleCargoList::Truncate(uint) { return 0; }

/* Ages the packets one day and stamps the transfer location on them — both are per-packet
 * walks over that same always-empty list. */
void VehicleCargoList::AgeCargo() {}
void VehicleCargoList::SetTransferLoadPlace(TileIndex) {}

/* Pool teardown hook for the vehicle cargo list. Specialised for exactly the one
 * instantiation the vehicle pool needs, rather than instantiating the whole class template
 * (which would define every other CargoList member and collide with the port's other TUs).
 * Clearing the list is all the real one does; the packets themselves belong to the
 * CargoPacket pool being torn down. */
template <>
void CargoList<VehicleCargoList, CargoPacketList>::OnCleanPool()
{
	this->packets.clear();
}
