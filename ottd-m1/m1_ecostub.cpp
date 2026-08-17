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
 * m1_ecostub.cpp — the leaf surface of the REAL economy.cpp (R1-176 rung 4).
 *
 * economy.cpp is compiled for real now (R1_REAL_ECONOMY). What it still cannot
 * find are entry points of subsystems that genuinely do not exist in this port:
 * no AI, no network, no subsidies, no signals, no vehicle groups, no
 * autoreplace, no cargo-delivery monitoring (admin port), no company HQ objects,
 * no NewGRF cargo callbacks, no feeder-income text effects. Each stub says
 * "nothing happened" in the way its caller can survive. The three cargo-list
 * movers follow the port's standing cargo model: no CargoPacket is ever
 * allocated (m1_station.cpp moves cargo by hand), so every mover honestly moves
 * zero out of an empty list.
 *
 * IntSqrt is NOT here: the real core/math_func.cpp is linked instead.
 */
#include "stdafx.h"

#include "station_base.h"
#include "cargopacket.h"
#include "cargomonitor.h"
#include "subsidy_func.h"
#include "signal_func.h"
#include "company_base.h"
#include "company_func.h"
#include "newgrf_cargo.h"
#include "autoreplace_func.h"
#include "group.h"
#include "texteff.hpp"
#include "object.h"
#include "station_func.h"
#include "network/network_type.h"   /* ClientID (enum only) */
#include "ai/ai.hpp"
/* vehicle_cmd.h / company_cmd.h drag the command-serialization templates
 * (EndianBufferWriter) — hand-declare the two signatures instead. The enums come
 * from company_type.h (via company_base.h) and network_type.h above. */
CommandCost CmdCompanyCtrl(DoCommandFlag flags, CompanyCtrlAction cca, CompanyID company_id, CompanyRemoveReason reason, ClientID client_id);
CommandCost CmdChangeServiceInt(DoCommandFlag flags, VehicleID veh_id, uint16 serv_int, bool is_custom, bool is_percent);

#include "safeguards.h"

/* ---- cargo movers: the lists are permanently empty ------------------------- */
bool VehicleCargoList::Stage(bool, StationID, StationIDStack, uint8, const GoodsEntry *, CargoPayment *) { return false; }
uint StationCargoList::Reserve(uint, VehicleCargoList *, TileIndex, StationIDStack) { return 0; }

/* ---- cargo-delivery monitoring (admin/AI querying) -------------------------- */
void AddCargoDelivery(CargoID, CompanyID, uint32, SourceType, SourceID, const Station *, IndustryID) {}
void ClearCargoDeliveryMonitoring(CompanyID) {}
void ClearCargoPickupMonitoring(CompanyID) {}

/* ---- subsidies: none exist -------------------------------------------------- */
bool CheckSubsidised(CargoID, CompanyID, SourceType, SourceID, const Station *) { return false; }
void RebuildSubsidisedSourceAndDestinationCache() {}

/* ---- rail signals: R1 lays plain rail, no signals --------------------------- */
void AddTrackToSignalBuffer(TileIndex, Track, Owner) {}
void UpdateSignalsInBuffer() {}

/* ---- company administrivia (bankruptcy path — no company ever goes broke
 *      with the R1 money model, but the code must link) ----------------------- */
CommandCost CmdCompanyCtrl(DoCommandFlag, CompanyCtrlAction, CompanyID, CompanyRemoveReason, ClientID) { return CommandCost(); }
CommandCost CmdChangeServiceInt(DoCommandFlag, VehicleID, uint16, bool, bool) { return CommandCost(); }
void CompanyAdminUpdate(const Company *) {}
bool MayCompanyTakeOver(CompanyID, CompanyID) { return false; }
void SetLocalCompany(CompanyID) {}
void NetworkClientsToSpectators(CompanyID) {}
void RemoveAllEngineReplacement(EngineRenewList *) {}
void RemoveAllGroupsForCompany(const CompanyID) {}
void UpdateCompanyHQ(TileIndex, uint) {}

/* ---- company infrastructure totals: honest sums over the real counters ------ */
uint32 CompanyInfrastructure::GetRoadTotal() const
{
	uint32 total = 0;
	for (RoadType rt = ROADTYPE_BEGIN; rt != ROADTYPE_END; rt++) {
		if (GetRoadTramType(rt) == RTT_ROAD) total += this->road[rt];
	}
	return total;
}

uint32 CompanyInfrastructure::GetTramTotal() const
{
	uint32 total = 0;
	for (RoadType rt = ROADTYPE_BEGIN; rt != ROADTYPE_END; rt++) {
		if (GetRoadTramType(rt) == RTT_TRAM) total += this->road[rt];
	}
	return total;
}

/* Default service interval from company settings, real shape (vehicle.cpp). */
int CompanyServiceInterval(const Company *c, VehicleType type)
{
	const VehicleDefaultSettings *vds = &c->settings.vehicle;
	switch (type) {
		default: NOT_REACHED();
		case VEH_TRAIN:    return vds->servint_trains;
		case VEH_ROAD:     return vds->servint_roadveh;
		case VEH_AIRCRAFT: return vds->servint_aircraft;
		case VEH_SHIP:     return vds->servint_ships;
	}
}

/* ---- absent cosmetics / NewGRF ---------------------------------------------- */
uint16 GetCargoCallback(CallbackID, uint32, uint32, const CargoSpec *) { return CALLBACK_FAILED; }
void ShowFeederIncomeAnimation(int, int, int, Money, Money) {}
Money AirportMaintenanceCost(Owner) { return 0; }

/* IntSqrt, verbatim from core/math_func.cpp:77-97. The REAL math_func.cpp
 * cannot be linked: its object file segfaults binutils' xcofflink all by
 * itself (Retro68 binutils 2.46, xcofflink.c — reproduced 2026-08-17 with a
 * one-object link). One small function copied beats one linker crash. */
uint32 IntSqrt(uint32 num)
{
	uint32 res = 0;
	uint32 bit = 1UL << 30; // Second to top bit number.

	/* 'bit' starts at the highest power of four <= the argument. */
	while (bit > num) bit >>= 2;

	while (bit != 0) {
		if (num >= res + bit) {
			num -= res + bit;
			res = (res >> 1) + bit;
		} else {
			res >>= 1;
		}
		bit >>= 2;
	}

	/* Arithmetic rounding to nearest integer. */
	if (num > res) res++;

	return res;
}

/* No AI, no script VM — companion of m1_rvstub_infra.cpp's AI::NewEvent. */
/* static */ void AI::Stop(CompanyID company) {}
