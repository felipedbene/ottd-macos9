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

// RUNG 7 (for real): a rail NETWORK generator. Grows a connected graph of track
// from a seed main line: an L-shaped leg to a flat halt near EVERY town, plus a
// closure loop between two far halts so the graph contains a CYCLE — which is the
// whole point, because only a cycle gives the pathfinder a genuine choice between
// two routes to the same station. Junctions are not special-cased: every tile's
// track bits are DERIVED from the set of laid neighbours it connects to (via the
// game's own DiagdirReachesTracks geometry), so wherever legs cross or join, the
// correct 3-way / 4-way / crossing piece appears automatically.
//
// Compile surface is deliberately small — tile/track/direction primitives only,
// the same family m1_pathfind and m1_rail_draw already link. It writes track via
// r1_place_rail_bits (m1_rail_draw) and platforms via r1_place_rail_station
// (m1_station_draw), and returns the served StationIDs to the scene.

#include "stdafx.h"

#include "map_func.h"
#include "tile_map.h"          // IsTileType, MP_CLEAR/MP_RAILWAY/MP_STATION/MP_VOID
#include "slope_func.h"        // GetTileSlope, IsSteepSlope
#include "track_func.h"        // DiagdirReachesTracks, DiagDirToAxis
#include "direction_func.h"    // DiagdirBetweenTiles, ReverseDiagDir
#include "town.h"              // Town::Iterate
#include "core/alloc_func.hpp" // CallocT

#include <cstdlib>             // free

#include "safeguards.h"

extern "C" void ottd_log(const char *fmt, ...);
extern "C" void r1_place_rail_bits(unsigned tile, unsigned bits);
extern "C" void r1_place_rail_station(unsigned tile, unsigned station_index, int axis);
extern "C" unsigned r1_town_station_index(unsigned townid);

/* A tile is rail-able for routing iff it is empty ground or already track. The
 * halt endpoints (MP_STATION) are handled as path terminals, never overwritten. */
static inline bool r1_railable(TileIndex t)
{
	return t.value < MapSize() && (IsTileType(t, MP_CLEAR) || IsTileType(t, MP_RAILWAY));
}

/* Obstacle-avoiding router: a DIRECTION-AWARE BFS from `a` to `b` over rail-able
 * ground/track, threading around houses/roads/water. Rail can climb a slope
 * running straight (inclined foundation) but cannot TURN on one, so the search
 * state is (tile, entry-direction): a straight step (exit == entry) is allowed
 * on any non-steep tile, but a turn (exit != entry) only on a FLAT tile — which
 * keeps every laid corner a valid rail piece even across hilly ground. State
 * packing: tile*4 + dir; dir 0..3 = DiagDirection entered by, 4 = source (any
 * first move, respecting flatness for turns). */
static int r1_route_bfs(TileIndex a, TileIndex b, TileIndex *path, int maxp)
{
	uint32 size = (uint32)MapSize();
	if (a.value >= size || b.value >= size) return 0;
	uint32 nstate = size * 5;                 // 4 entry dirs + source slot
	uint32 *parent = CallocT<uint32>(nstate);
	uint8 *seen = CallocT<uint8>((nstate + 7) / 8);
	uint32 *queue = CallocT<uint32>(nstate);
	if (parent == nullptr || seen == nullptr || queue == nullptr) {
		free(parent); free(seen); free(queue); return 0;
	}
	auto sset = [&](uint32 s) { seen[s >> 3] |= 1 << (s & 7); };
	auto sget = [&](uint32 s) -> bool { return (seen[s >> 3] >> (s & 7)) & 1; };

	uint32 qh = 0, qt = 0;
	uint32 s0 = a.value * 5 + 4;              // source: entry-dir sentinel
	sset(s0); parent[s0] = s0; queue[qt++] = s0;
	uint32 found_state = 0xFFFFFFFF;

	while (qh < qt) {
		uint32 st = queue[qh++];
		TileIndex cur{ st / 5 };
		int entry = (int)(st % 5);            // 4 = source
		if (cur == b) { found_state = st; break; }
		bool cur_flat = (GetTileSlope(cur) == SLOPE_FLAT);
		for (DiagDirection e = DIAGDIR_BEGIN; e < DIAGDIR_END; e = (DiagDirection)(e + 1)) {
			if (entry != 4 && (int)e == (entry ^ 2)) continue;     // no U-turn (opposite of entry)
			bool turning = (entry != 4) && ((int)e != entry);
			if (turning && !cur_flat) continue;                    // can't change axis on a slope
			TileIndex nb = TileAddByDiagDir(cur, e);
			if (nb.value >= size) continue;
			if (nb != b) {
				if (!r1_railable(nb)) continue;
				if (IsSteepSlope(GetTileSlope(nb))) continue;      // straights ok on gentle, not steep
			}
			uint32 ns = nb.value * 5 + (uint32)e;
			if (sget(ns)) continue;
			sset(ns); parent[ns] = st;
			if (nb == b) { found_state = ns; qh = qt; break; }
			queue[qt++] = ns;
		}
	}

	int n = 0;
	if (found_state != 0xFFFFFFFF) {
		uint32 s = found_state; int count = 1;
		while (s != parent[s]) { s = parent[s]; count++; }
		if (count <= maxp) {
			s = found_state; int k = count - 1;
			while (true) { path[k] = TileIndex{ s / 5 }; if (s == parent[s]) break; s = parent[s]; k--; }
			n = count;
		}
	}
	free(parent); free(seen); free(queue);
	return n;
}

/* Track bits for a tile from its connected-edge set (4-bit mask of DiagDirections
 * that have a laid neighbour): OR the connecting track over every pair of edges.
 * 2 opposite edges -> straight; 2 adjacent -> corner; 3 -> 3-way; 4 -> crossing. */
static TrackBits r1_edges_to_trackbits(uint8 edges)
{
	TrackBits bits = TRACK_BIT_NONE;
	for (int e1 = 0; e1 < 4; e1++) {
		if (!(edges & (1 << e1))) continue;
		for (int e2 = e1 + 1; e2 < 4; e2++) {
			if (!(edges & (1 << e2))) continue;
			bits = (TrackBits)(bits | (DiagdirReachesTracks((DiagDirection)e1) &
			                           DiagdirReachesTracks((DiagDirection)e2)));
		}
	}
	return bits;
}

/* Multi-source direction-aware flood from every current network tile. Fills
 * best_state[tile] with the min-depth state that reached each tile (0xFFFFFFFF
 * = unreached) and parent[state] for reconstruction. Same movement rule as the
 * single-target router: straights climb gentle slopes, turns only on flat. This
 * is how a town's halt is chosen — NOT at the (walled) town centre, but at the
 * nearest tile the rails can actually reach. */
static void r1_flood(const uint8 *innet, uint32 *best_state, uint32 *parent)
{
	uint32 size = (uint32)MapSize();
	uint32 nstate = size * 5;
	for (uint32 t = 0; t < size; t++) best_state[t] = 0xFFFFFFFF;
	uint8 *seen = CallocT<uint8>((nstate + 7) / 8);
	uint32 *queue = CallocT<uint32>(nstate);
	if (seen == nullptr || queue == nullptr) { free(seen); free(queue); return; }
	auto sset = [&](uint32 s) { seen[s >> 3] |= 1 << (s & 7); };
	auto sget = [&](uint32 s) -> bool { return (seen[s >> 3] >> (s & 7)) & 1; };

	uint32 qh = 0, qt = 0;
	for (uint32 t = 0; t < size; t++) {
		if (!innet[t]) continue;
		uint32 s = t * 5 + 4;
		sset(s); parent[s] = s; best_state[t] = s; queue[qt++] = s;
	}
	while (qh < qt) {
		uint32 st = queue[qh++];
		TileIndex cur{ st / 5 };
		int entry = (int)(st % 5);
		bool cur_flat = (GetTileSlope(cur) == SLOPE_FLAT);
		for (DiagDirection e = DIAGDIR_BEGIN; e < DIAGDIR_END; e = (DiagDirection)(e + 1)) {
			if (entry != 4 && (int)e == (entry ^ 2)) continue;
			if (entry != 4 && (int)e != entry && !cur_flat) continue;
			TileIndex nb = TileAddByDiagDir(cur, e);
			if (nb.value >= size || !r1_railable(nb) || IsSteepSlope(GetTileSlope(nb))) continue;
			uint32 ns = nb.value * 5 + (uint32)e;
			if (sget(ns)) continue;
			sset(ns); parent[ns] = st;
			if (best_state[nb.value] == 0xFFFFFFFF) best_state[nb.value] = ns;
			queue[qt++] = ns;
		}
	}
	free(seen); free(queue);
}

/* Reconstruct the tile path for a reached state back to its network source.
 * Writes source..tile into path[], returns length (tile is path[n-1]). */
static int r1_reconstruct(uint32 state, const uint32 *parent, TileIndex *path, int maxp)
{
	uint32 s = state; int count = 1;
	while (s != parent[s]) { s = parent[s]; count++; }
	if (count > maxp) return 0;
	s = state; int k = count - 1;
	while (true) { path[k] = TileIndex{ s / 5 }; if (s == parent[s]) break; s = parent[s]; k--; }
	return count;
}

/* Build the network. seed[]/nseed = the pre-laid main-line tiles. Places a
 * platform per reachable town, lays all legs + one closure loop, writes track,
 * and returns the served StationIDs in a nearest-neighbour tour order (out_st /
 * up to max_st). Returns the count. */
extern "C" int r1_build_rail_network(const unsigned *seed, int nseed,
                                     unsigned depot_tile,
                                     unsigned *out_st, int max_st)
{
	uint32 size = (uint32)MapSize();
	uint8 *edges = CallocT<uint8>(size);   // per-tile connected-edge mask
	uint8 *innet = CallocT<uint8>(size);   // tile participates in the network
	if (edges == nullptr || innet == nullptr) { free(edges); free(innet); return 0; }

	auto add_edge = [&](TileIndex a, TileIndex b) {
		DiagDirection d = DiagdirBetweenTiles(a, b);
		if (d == INVALID_DIAGDIR) return;
		edges[a.value] |= 1 << d;
		edges[b.value] |= 1 << ReverseDiagDir(d);
		innet[a.value] = innet[b.value] = 1;
	};
	auto lay_path = [&](TileIndex *p, int n) {
		for (int i = 0; i + 1 < n; i++) add_edge(p[i], p[i + 1]);
	};

	// Seed the accumulator with the existing main line.
	for (int i = 0; i + 1 < nseed; i++)
		add_edge(TileIndex{ seed[i] }, TileIndex{ seed[i + 1] });

	TileIndex avoid{ depot_tile };
	TileIndex halts[16]; unsigned halt_st[16]; int nh = 0;
	TileIndex path[400];

	uint32 *best_state = CallocT<uint32>(size);
	uint32 *parent = CallocT<uint32>(size * 5);
	uint8 *is_halt = CallocT<uint8>(size);
	if (best_state == nullptr || parent == nullptr || is_halt == nullptr) {
		free(edges); free(innet); free(best_state); free(parent); free(is_halt); return 0;
	}

	// One leg per town, RE-FLOODING after each so later towns can attach to
	// earlier legs. The halt is the nearest FLAT reachable tile to the town
	// centre (found by the flood), never the walled-in centre itself.
	for (const Town *tn : Town::Iterate()) {
		if (nh >= 16) break;
		unsigned sid = r1_town_station_index((unsigned)tn->index);
		if (sid == 0xFFFF) continue;

		r1_flood(innet, best_state, parent);

		TileIndex halt = INVALID_TILE; uint bestd = UINT_MAX;
		for (uint32 t = 0; t < size; t++) {
			if (best_state[t] == 0xFFFFFFFF) continue;               // unreachable
			if (!IsTileType(TileIndex{ t }, MP_CLEAR)) continue;     // platform needs open ground
			if (GetTileSlope(TileIndex{ t }) != SLOPE_FLAT) continue;
			if (is_halt[t] || DistanceManhattan(TileIndex{ t }, avoid) < 2) continue;
			uint d = DistanceManhattan(TileIndex{ t }, tn->xy);
			if (d < bestd) { bestd = d; halt = TileIndex{ t }; }
		}
		if (halt == INVALID_TILE) { ottd_log("R1-NET: town %u unreachable — skipped", (uint)tn->index); continue; }

		int n = r1_reconstruct(best_state[halt.value], parent, path, 400);
		if (n < 2) { ottd_log("R1-NET: town %u halt path degenerate — skipped", (uint)tn->index); continue; }

		lay_path(path, n);                       // path = network-source .. halt
		is_halt[halt.value] = 1;
		// platform axis = axis of the last segment entering the halt.
		DiagDirection dlast = DiagdirBetweenTiles(path[n - 1], path[n - 2]);
		r1_place_rail_station((unsigned)halt.value, sid, (int)DiagDirToAxis(dlast));
		halts[nh] = halt; halt_st[nh] = sid; nh++;
	}
	free(best_state); free(parent); free(is_halt);

	// Closure loop: connect the two farthest halts with a second L-path, giving
	// the graph a cycle (two routes between the stations it links).
	if (nh >= 3) {
		int ia = 0, ib = 1; uint far = 0;
		for (int i = 0; i < nh; i++)
			for (int j = i + 1; j < nh; j++) {
				uint d = DistanceManhattan(halts[i], halts[j]);
				if (d > far) { far = d; ia = i; ib = j; }
			}
		int n = r1_route_bfs(halts[ia], halts[ib], path, 160);
		if (n != 0) { lay_path(path, n); ottd_log("R1-NET: closure loop laid (%d tiles)", n); }
		else ottd_log("R1-NET: closure loop unroutable — network is a tree");
	}

	// Materialise: every network tile that is not a station gets its derived bits.
	int laid = 0;
	for (uint32 t = 0; t < size; t++) {
		if (!edges[t] || IsTileType(TileIndex{ t }, MP_STATION)) continue;
		TrackBits bits = r1_edges_to_trackbits(edges[t]);
		if (IsSteepSlope(GetTileSlope(TileIndex{ t }))) bits = (TrackBits)(bits & TRACK_BIT_CROSS);
		if (bits == TRACK_BIT_NONE) continue;
		r1_place_rail_bits(t, (unsigned)bits);
		laid++;
	}

	// Nearest-neighbour tour of the halts, so the order chain reads as a loop.
	int nst = 0;
	if (nh > 0) {
		bool used[16] = { false };
		int cur = 0; used[0] = true; out_st[nst++] = halt_st[0];
		for (int step = 1; step < nh && nst < max_st; step++) {
			int nxt = -1; uint bd = UINT_MAX;
			for (int j = 0; j < nh; j++) {
				if (used[j]) continue;
				uint d = DistanceManhattan(halts[cur], halts[j]);
				if (d < bd) { bd = d; nxt = j; }
			}
			if (nxt < 0) break;
			used[nxt] = true; cur = nxt; out_st[nst++] = halt_st[nxt];
		}
	}

	ottd_log("R1-NET: %d towns served, %d track tiles laid, cycle=%s",
	         nh, laid, (nh >= 3) ? "yes" : "no");
	free(edges); free(innet);
	return nst;
}
