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

// R1 road pathfinder: a self-contained breadth-first search over the connected
// MP_ROAD tile graph, used to give the R1 buses a REAL route between two station
// tiles (following their attached order chain) instead of the fixed pre-traced
// ping-pong route. Deliberately tiny compile surface — it depends only on the
// road-bit / tile / direction primitives that r1_trace_route already proved out
// (GetAnyRoadBits + DiagDirToRoadBits + TileAddByDiagDir + IsTileType), so it
// links cleanly against the merged binary. No YAPF, no NPF, no roadveh_cmd.
//
// BFS (not A*) is deliberate: the R1 map is 64x64 = 4096 tiles, so a full
// breadth-first sweep visits at most a few thousand nodes and always yields a
// SHORTEST (fewest-tiles) path — cheaper to reason about than a heuristic search
// and plenty fast on a 233 MHz G3. The three working arrays are heap-allocated at
// MapSize() (~4096 entries here) and freed before return.

#include "stdafx.h"

#include "map_func.h"        // MapSize, TileAddByDiagDir, TileX/TileY (via includes)
#include "tile_map.h"        // IsTileType, MP_ROAD / MP_STATION (tile_type.h)
#include "road_map.h"        // GetAnyRoadBits
#include "road_func.h"       // DiagDirToRoadBits
#include "road.h"            // RTT_ROAD (RoadTramType)
#include "direction_type.h"  // DiagDirection, DIAGDIR_BEGIN/END
#include "core/alloc_func.hpp" // CallocT (heap working arrays; zero-init)

#include <cstdlib>           // free

#include "safeguards.h"

// Breadth-first search over connected road tiles.
//
// Returns the number of tiles written to out[] — the path from `from_tile` to
// `to_tile`, INCLUSIVE of both endpoints (out[0] == from_tile, out[n-1] ==
// to_tile) — or 0 if:
//   * from/to are off the map, or
//   * no connected road path exists, or
//   * the shortest path is longer than max_out tiles (see TRUNCATION note).
//
// On any 0-return, out[] is left UNTOUCHED, so a caller can safely path into an
// existing route buffer and keep the old route on failure.
//
// Neighbour rule (mirrors r1_trace_route exactly): from a tile, for each of the
// up-to-4 DiagDirections d, the neighbour nb = TileAddByDiagDir(tile, d) is
// walkable iff the tile's road bits permit leaving toward d (GetAnyRoadBits &
// DiagDirToRoadBits(d)) AND nb is on-map AND (nb is an MP_ROAD tile OR nb is the
// destination). Accepting the destination unconditionally lets the path END on an
// MP_STATION bus-stop tile (the order's station tile), which is not MP_ROAD.
// GetAnyRoadBits() itself handles MP_STATION road-stop tiles, so a search that
// STARTS on a converted bus-stop tile can still leave it.
extern "C" int r1_road_path(unsigned from_tile, unsigned to_tile, unsigned *out, int max_out)
{
	if (out == nullptr || max_out <= 0) return 0;

	const uint size = MapSize();
	const uint32 from = (uint32)from_tile;
	const uint32 to   = (uint32)to_tile;

	if (from >= size || to >= size) return 0;

	// Trivial path: start == destination -> a single tile.
	if (from == to) { out[0] = from; return 1; }

	// Working arrays, indexed by raw tile id. calloc zero-inits: seen[]==0 means
	// unvisited; parent[] is only read for tiles we have marked seen, so its zero
	// fill is harmless (tile 0 is a valid parent value but never read unseen).
	uint32 *parent = CallocT<uint32>(size);
	unsigned char *seen = CallocT<unsigned char>(size);
	uint32 *queue = CallocT<uint32>(size);   // FIFO ring is unnecessary: each tile enqueues once
	if (parent == nullptr || seen == nullptr || queue == nullptr) {
		free(parent); free(seen); free(queue);
		return 0;
	}

	uint qhead = 0, qtail = 0;
	seen[from] = 1;
	parent[from] = from;          // self-parent marks the search root (stops reconstruction)
	queue[qtail++] = from;

	bool found = false;
	while (qhead < qtail) {
		uint32 cur = queue[qhead++];
		if (cur == to) { found = true; break; }

		RoadBits rb = GetAnyRoadBits(TileIndex{ cur }, RTT_ROAD, false);
		for (DiagDirection d = DIAGDIR_BEGIN; d < DIAGDIR_END; d = (DiagDirection)(d + 1)) {
			if (!(rb & DiagDirToRoadBits(d))) continue;
			TileIndex nbt = TileAddByDiagDir(TileIndex{ cur }, d);
			uint32 nb = nbt.value;
			if (nb >= size) continue;                       // off-map / INVALID_TILE
			if (seen[nb]) continue;
			// Walkable iff it's road, or it's the destination (which may be a
			// non-road MP_STATION bus-stop tile).
			if (nb != to && !IsTileType(nbt, MP_ROAD)) continue;
			seen[nb] = 1;
			parent[nb] = cur;
			if (nb == to) { found = true; qhead = qtail; break; }  // stop the outer while
			queue[qtail++] = nb;
		}
	}

	int n = 0;
	if (found && seen[to]) {
		// Measure the path length first (walk parents to -> from) so we can reject
		// an over-long path WITHOUT writing a truncated route into out[].
		uint32 t = to;
		int count = 1;
		while (t != from) { t = parent[t]; count++; }

		// TRUNCATION policy: refuse to write anything if the path does not fit.
		// The caller then keeps its old route (the ping-pong fallback), which is
		// safer than handing back a route that stops short of the station.
		if (count <= max_out) {
			// Reconstruct reversed (to->from), then flip into out[] as from->to.
			t = to;
			int k = count - 1;
			while (true) {
				out[k] = t;
				if (t == from) break;
				t = parent[t];
				k--;
			}
			n = count;
		}
	}

	free(parent); free(seen); free(queue);
	return n;
}

// ============================================================================
// RUNG 7: trackdir-aware RAIL BFS — the brain behind the Yapf* train shims in
// m1_trainstub.cpp. Unlike the road BFS above (tile graph), rail must respect
// track pieces and entry sides: a corner track connects exactly two tile edges,
// so the search state is (tile, trackdir). Successors follow the game's own
// GetTileTrackStatus for every tile type (plain rail, junctions, depots and
// rail-station platforms all answer through their tile procs), masked by
// DiagdirReachesTrackdirs — precisely the reachability rule TrainController
// itself applies at tile boundaries.
// State packing: (tile << 4) | trackdir. 64x64 map -> 65536 states; the
// visited bitmap is 8 KB and a full sweep is trivially fast on the G3/G4.
// ============================================================================
#include "tile_cmd.h"       // GetTileTrackStatus
#include "track_func.h"     // TrackdirToExitdir, DiagdirReachesTrackdirs, ...

static const int R1_RAIL_DEPTH_LIMIT = 512;   // tiles; the R1 network is tiny

// Shortest path length (tile steps) from moving state (tile, td) to dest_tile,
// or -1 when unreachable. Reaching dest by ANY connected trackdir counts.
extern "C" int r1_rail_dist(unsigned tile, int td, unsigned dest_tile)
{
	uint32 size = (uint32)MapSize();
	if (tile >= size || dest_tile >= size) return -1;
	if (tile == dest_tile) return 0;

	uint32 nstates = size << 4;
	uint8 *seen = CallocT<uint8>((nstates + 7) / 8);
	uint32 *queue = CallocT<uint32>(nstates);   // state | (depth << 20) packed below
	if (seen == nullptr || queue == nullptr) { free(seen); free(queue); return -1; }

	// queue entries: state in low 20 bits (16 used), depth in high 12 bits.
	uint32 qhead = 0, qtail = 0;
	uint32 s0 = ((uint32)tile << 4) | (uint32)(td & 0xF);
	seen[s0 >> 3] |= 1 << (s0 & 7);
	queue[qtail++] = s0;
	int result = -1;

	while (qhead < qtail) {
		uint32 packed = queue[qhead++];
		uint32 state = packed & 0xFFFFF;
		int depth = (int)(packed >> 20);
		if (depth >= R1_RAIL_DEPTH_LIMIT) continue;

		TileIndex cur{ state >> 4 };
		Trackdir cur_td = (Trackdir)(state & 0xF);

		DiagDirection exitdir = TrackdirToExitdir(cur_td);
		TileIndex nbt = TileAddByDiagDir(cur, exitdir);
		uint32 nb = nbt.value;
		if (nb >= size) continue;                      // off-map

		TrackStatus ts = GetTileTrackStatus(nbt, TRANSPORT_RAIL, 0, ReverseDiagDir(exitdir));
		TrackdirBits tds = TrackStatusToTrackdirBits(ts) & DiagdirReachesTrackdirs(exitdir);
		if (tds == TRACKDIR_BIT_NONE) continue;        // wall / incompatible entry side

		if (nb == dest_tile) { result = depth + 1; break; }

		for (int t2 = 0; t2 < 16; t2++) {
			if (!(tds & (1 << t2))) continue;
			uint32 s = (nb << 4) | (uint32)t2;
			if (seen[s >> 3] & (1 << (s & 7))) continue;
			seen[s >> 3] |= 1 << (s & 7);
			queue[qtail++] = s | ((uint32)(depth + 1) << 20);
		}
	}

	free(seen); free(queue);
	return result;
}

// Pick the best trackdir out of `candidates` (a TrackdirBits mask, already
// entry-side filtered by the caller) on `tile` toward dest_tile. Returns 1 and
// writes *best_td when some candidate reaches the destination; 0 otherwise
// (*best_td then holds the first candidate as a keep-driving fallback).
extern "C" int r1_rail_choose_trackdir(unsigned tile, unsigned candidates, unsigned dest_tile, int *best_td)
{
	int best = -1, best_dist = 0x7FFFFFFF, first = -1;
	for (int td = 0; td < 16; td++) {
		if (!(candidates & (1u << td))) continue;
		if (first < 0) first = td;
		int d = r1_rail_dist(tile, td, dest_tile);
		if (d >= 0 && d < best_dist) { best_dist = d; best = td; }
	}
	if (best >= 0) { *best_td = best; return 1; }
	*best_td = (first >= 0) ? first : 0;
	return 0;
}
