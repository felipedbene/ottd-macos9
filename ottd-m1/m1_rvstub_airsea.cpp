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
 * m1_rvstub_airsea.cpp — the air/sea/rail half of the link surface that the REAL
 * vehicle TUs drag in. We now compile the game's own roadveh_cmd.cpp, vehicle.cpp,
 * engine.cpp, order_cmd.cpp and friends because R1 wants REAL road vehicles; but
 * those files are written for a game with all four transport modes, so they name
 * aircraft, ship and train entry points in branches that a bus can never take
 * (Vehicle::GetImage dispatch, the order-validity sweep, depot/hangar lookups).
 * The linker does not care that the branches are dead — it wants the addresses.
 *
 * R1 has no aircraft, no ships and no player trains, and porting aircraft_cmd.cpp /
 * ship_cmd.cpp / train_cmd.cpp would drag the airport state machines, the water
 * pathfinder and YAPF onto a 66 MHz G3 for no visible gain. So these are HONEST
 * stubs, not placeholders waiting to be filled in: each one returns the value that
 * means "this subsystem is absent" — no airport, no valid target, no hangar tile,
 * a failed command — rather than pretending to do the work. If one of these ever
 * fires at runtime it is a bug in our reachability assumptions, and the caller sees
 * a clean "nothing there" instead of garbage.
 *
 * Sibling of m1_train.cpp: that file builds a REAL Train object (for the cosmetic
 * R1 train) but deliberately leaves train_cmd.cpp out; the two train symbols here
 * are the leftovers order_cmd.cpp/vehicle.cpp reference from that omission.
 */
#include "stdafx.h"

#include "aircraft.h"          /* Aircraft, HandleAircraftEnterHangar, ... */
#include "ship.h"              /* Ship */
#include "train.h"             /* Train, ConsistChangeFlags */
#include "train_cmd.h"         /* CmdReverseTrainDirection */
#include "newgrf_airport.h"    /* AirportSpec */
#include "station_map.h"       /* IsHangar */
#include "command_func.h"      /* CMD_ERROR */

#include "safeguards.h"

/* ---- Aircraft ------------------------------------------------------------ */

/* No airport was ever built, so there is no tile an aircraft could be ordered to. */
TileIndex Aircraft::GetOrderStationLocation(StationID)
{
	return INVALID_TILE;
}

/* Advancing the airport movement state machine is meaningless without a state
 * machine to advance. */
void AircraftNextAirportPos_and_Order(Aircraft *)
{
}

/* Same for the hangar-entry bookkeeping (servicing, refit-on-arrival, news). */
void HandleAircraftEnterHangar(Aircraft *)
{
}

/* order_cmd.cpp calls this when an aircraft is left without orders. Nothing to fix up. */
void HandleMissingAircraftOrders(Aircraft *)
{
}

/* There is never a reachable target airport. */
Station *GetTargetAirportIfValid(const Aircraft *)
{
	return nullptr;
}

/* ---- Airports ------------------------------------------------------------ */

/* The real dummy spec is a zeroed, disabled airport; value-initialising it gives
 * exactly that, and keeps AirportSpec::Get() able to return something non-null. */
const AirportSpec AirportSpec::dummy = {};

/* We never load the airport specs table, so every type resolves to the dummy —
 * i.e. "an airport that does not exist and is not available". */
const AirportSpec *AirportSpec::Get(byte)
{
	return &AirportSpec::dummy;
}

/* No station tile in an R1 world is a hangar. */
bool IsHangar(TileIndex)
{
	return false;
}

/* ---- Ships --------------------------------------------------------------- */

/* Nothing to draw: leave the caller's sprite sequence untouched. */
void Ship::GetImage(Direction, EngineImageType, VehicleSpriteSeq *) const
{
}

void Ship::UpdateCache()
{
}

void Ship::UpdateDeltaXY()
{
}

/* ---- Trains -------------------------------------------------------------- */

/* Recomputing a consist means walking the articulated/multihead chain and asking
 * NewGRF for lengths and capacities — all of which lives in train_cmd.cpp. The R1
 * train is a single cosmetic unit whose cache is set once at creation.
 * R1_REAL_TRAIN_STACK: train_cmd.cpp provides both for real. */
#ifndef R1_REAL_TRAIN_STACK
void Train::ConsistChanged(ConsistChangeFlags)
{
}

/* Reversing is a player command against a train we do not simulate. */
CommandCost CmdReverseTrainDirection(DoCommandFlag, VehicleID, bool)
{
	return CMD_ERROR;
}
#endif /* !R1_REAL_TRAIN_STACK */
