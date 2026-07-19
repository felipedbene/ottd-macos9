// M1: out-of-line no-op definitions for two member functions that date.cpp
// references only from the MAX_YEAR calendar-wrap dead branch (never taken for
// the 1950s run). Defining them here avoids compiling vehicle.cpp / linkgraph.
#include "stdafx.h"
#include "vehicle_base.h"
#include "linkgraph/linkgraph.h"

void Vehicle::ShiftDates(int) {}
void LinkGraph::ShiftDates(int) {}
