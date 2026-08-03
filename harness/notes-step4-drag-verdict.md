# Step 4 (macsys_scroll) drag-pan verdict — REVERTED from main, branch kept

2026-08-02. `pr/graphics-step4-scroll` was merged, HW-validated, and **reverted**
(revert commit on main; the branch is untouched). The 12/12 harness verdict PASSES
with it — the harness never pans, so it cannot see this — the failure is visual and
only shows up under an actual drag.

## How it was tested (repeatable)

The QEMU slot pool can inject a real drag-pan: the OS 9 guest's one-button mouse
maps ctrl+click to right-click in `macclassic_v.cpp` (OpenTTD's own idiom), so a
pan is `ctrl down → btn down → rel moves → release` over QMP `input-send-event`
(the ADB mouse is relative-only; pin the cursor at top-left with a big negative
move, then walk it to a known position). The injection must land in the first
~25s after the banner (see the netlog freeze note below) and must START from a
point in the raw viewport — a drag started over the Towns overlay window goes to
that window, not the viewport, and nothing pans.

Evidence in `harness/artifacts/step4-drag/` (gitignored, session copy):
d4_before / d4_mid / d4_mid2 / d4_after.

## Result

- **The pan itself works.** The real drag-scroll path drives `macsys_scroll` and
  the world moves smoothly under the cursor. This part of Step 4 is proven.
- **Toolbar permanently garbled** after the first pan: the blit scrolls pixels
  through the toolbar's screen rows and nothing ever marks the toolbar dirty
  again (d4_mid → d4_after, top rows).
- **Cash bar caption corrupted** the same way ("£100,07").
- **Stale shards in the world** (cyan water triangles, lower-left in d4_mid2 and
  still in d4_after): the exposed-edge repair misses regions, so the damage is
  not self-healing.
- The Towns overlay garbles mid-drag but recovers — overlay windows repaint
  themselves; the toolbar and cash bar do not.

## Why

`macsys_scroll(left, top, width, height, xo, yo)` is called with the viewport's
rect but the CopyBits scroll visibly moves pixels belonging to OTHER windows
(toolbar, cash bar) and its edge-strip redraw does not cover everything it
exposed. R1-98's full-redraw `DoSetViewportPosition` existed precisely to avoid
this: one QuickDraw buffer, no per-window backing store, so any in-place scroll
must either clip to the visible-viewport-minus-overlaps region or redraw every
window that intersects the scrolled rect afterward.

## What a fix needs (for the next attempt on the branch)

1. Clip the blit to the viewport rect *minus* every overlapping window rect
   (toolbar, cash bar, any open overlay), or
2. keep the whole-rect blit but then `MarkWholeScreenDirty`-equivalent for every
   window intersecting the scrolled rect (toolbar + cash bar at minimum), and
3. fix the exposed-edge computation — the current strips demonstrably miss
   regions (the water shards).
4. Re-run the drag test above; the gate is: after `btn up`, one more frame must
   show a clean toolbar, clean cash bar, and no stale shards.

# Separate pre-existing finding: the app freezes ~tick 768, telemetry dies ~tick 512

Discovered while trying to drag post-verdict: EVERY run (R1-138, 140, 141, 143,
144 — pre- and post-merge alike) goes silent on the sink after ~130-132 lines
(last beat = tick 512), yet the game demonstrably keeps running ~2 more beats
(the frozen screen always shows money 99816 = the tick-768 value; the last
logged FIN is 99862 = tick 512) and then hard-freezes: no repaint, clicks do not
wake it. So there are TWO stages: (1) UDP lines stop arriving ~line 130,
(2) ~250 ticks later the app freezes solid. Deterministic across builds.

Suspects: OT UDP send path (`statsd.c` sd_send_to / netlog throttle) exhausting
something after ~130 sends, then wedging OT entirely. NOT caused by today's
merges — R1-140 (pre-merge baseline) behaves identically. The harness never saw
it because the 12/12 verdict completes within the first ~60s.

Impact: any interactive HW session dies after ~1 minute of telemetry; "HANG"
verdicts blamed on lost telemetry may have been THIS. Worth its own bug hunt:
instrument g_ok/g_fail/g_lasterr from statsd.c on-screen (not via the sink,
which is the thing dying).
