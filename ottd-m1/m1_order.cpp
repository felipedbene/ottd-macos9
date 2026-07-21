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
 * m1_order.cpp — REAL Order + OrderList pools, WITHOUT compiling order_cmd.cpp
 * (which drags the network/command/timetable/GUI cascade). Same trick as
 * m1_vehicle.cpp / m1_station.cpp: own the pools + the objects' out-of-line
 * bodies here; the heavy command TU stays out. This is the first rung of the
 * "autonomous vehicles" depth track: it attaches a real 2-stop OT_GOTO_STATION
 * order chain (via a pooled OrderList) to each bus Vehicle. INVISIBLE plumbing —
 * no pixel change; success = it links and the sim keeps ticking with real Order
 * objects attached. See [[next-wave-plan]] (DEPTH track).
 */
#include "stdafx.h"
#include "order_base.h"
#include "vehicle_base.h"
#include "core/pool_func.hpp"

#include "safeguards.h"

/* ---- the REAL Order / OrderList pools (no prior symbol anywhere — fresh) ---- */
OrderPool _order_pool("Order");
INSTANTIATE_POOL_METHODS(Order)

OrderListPool _orderlist_pool("OrderList");
INSTANTIATE_POOL_METHODS(OrderList)

/* ---- Order teardown (moved here from m1_road_stubs.cpp — the Order object's
 *      teardown now lives with the Order pool). ---- */
Order::~Order() {}

/* ---- Out-of-line Order bodies we reference (real home: the UNCOMPILED
 *      order_cmd.cpp). Copied verbatim; minimal + self-contained. ---- */
void Order::MakeGoToStation(StationID destination)
{
	this->type = OT_GOTO_STATION;
	this->flags = 0;
	this->dest = destination;
}

/* ---- Out-of-line OrderList bodies dragged in by the OrderList(chain, v) ctor.
 *      Copied verbatim from order_cmd.cpp. Initialize walks the (short) chain and
 *      the shared-vehicle links; our bus has next_shared/previous_shared == nullptr,
 *      so those loops don't iterate. ---- */
void OrderList::RecalculateTimetableDuration()
{
	this->timetable_duration = 0;
	for (Order *o = this->first; o != nullptr; o = o->next) {
		this->timetable_duration += o->GetTimetabledWait() + o->GetTimetabledTravel();
	}
}

void OrderList::Initialize(Order *chain, Vehicle *v)
{
	this->first = chain;
	this->first_shared = v;

	this->num_orders = 0;
	this->num_manual_orders = 0;
	this->num_vehicles = 1;
	this->timetable_duration = 0;

	for (Order *o = this->first; o != nullptr; o = o->next) {
		++this->num_orders;
		if (!o->IsType(OT_IMPLICIT)) ++this->num_manual_orders;
		this->total_duration += o->GetWaitTime() + o->GetTravelTime();
	}

	this->RecalculateTimetableDuration();

	for (Vehicle *u = this->first_shared->PreviousShared(); u != nullptr; u = u->PreviousShared()) {
		++this->num_vehicles;
		this->first_shared = u;
	}

	for (const Vehicle *u = v->NextShared(); u != nullptr; u = u->NextShared()) ++this->num_vehicles;
}

/* ---- Attach a real 2-stop OT_GOTO_STATION chain to one bus Vehicle. ----
 * Allocates 2 pooled Orders (A -> B), builds one pooled OrderList over that chain,
 * points v->orders at it, and seeds v->current_order (by value) with the first stop.
 * Returns true on success, false if either pool is full (chain left untouched). */
extern "C" int r1_attach_bus_orders(void *vehicle, unsigned station_a, unsigned station_b)
{
	Vehicle *v = (Vehicle *)vehicle;
	if (v == nullptr) return 0;

	/* Need 2 Orders + 1 OrderList available. */
	if (!Order::CanAllocateItem(2) || !OrderList::CanAllocateItem()) return 0;

	Order *o_a = new Order();
	o_a->MakeGoToStation((StationID)station_a);

	Order *o_b = new Order();
	o_b->MakeGoToStation((StationID)station_b);

	o_a->next = o_b;
	o_b->next = nullptr;

	OrderList *list = new OrderList(o_a, v);
	v->orders = list;

	/* Seed current_order with a copy of the first stop; dest_tile left as-is (the
	 * caller/pathfinder can resolve the station tile when movement lands). */
	v->current_order.MakeGoToStation((StationID)station_a);

	return 1;
}

/* ---- Validation hook: how many real Orders currently live in the pool. ---- */
extern "C" unsigned r1_order_count(void)
{
	return (unsigned)Order::GetNumItems();
}
