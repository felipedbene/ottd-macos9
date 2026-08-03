# Handoff: fix Step 4 drag-pan corruption (`pr/graphics-step4-scroll`, reverted from main)

Task brief for an external coding agent. Everything here is verifiable by **compiling**;
the drag-pan validation runs on our side (QEMU slot pool + QMP input injection). Do not
run the test harness.

<!-- agent: 2026-08-02 — Starting. Read handoff + notes-step4-drag-verdict.md. On main (post-revert). Branch pr/graphics-step4-scroll exists locally+remote. Next: inspect branch sed/macsys_scroll vs upstream DoSetViewportPosition post-GfxScroll path, then rebase onto main and fix. -->
<!-- agent: Diagnosis vs brief. (1) Brief's prime suspect is WRONG on the success path: sed is `if (!macsys_scroll(...)) { RedrawScreenRect(...); return; }` — early-return only on FAILURE; on success the edge-strip RedrawScreenRects still run. (2) Upstream DoSetViewportPosition ALREADY clips scroll rects around overlapping front windows before GfxScroll — so "clip" shape is already there. (3) Real bug: macsys_scroll CopyBits used full-rect src→offset dst, so dst OVERSHOOTS the clipped rect into toolbar/cash-bar/adjacent strips; scroll_fb correctly stayed in-bounds (Blitter_8bppBase::ScrollBuffer geometry). Fix chosen: clip CopyBits to the retained overlap (shape closer to upstream ScrollBuffer than blit-then-repair). -->
<!-- agent: Rebase note: `git rebase main` dropped the feature commit (65141f4 is already in main's merge history), so cherry-picked 65141f4 onto main tip instead. Applied overlap-only CopyBits fix to ottd-b2 + ottd-r1 macclassic_sys.c. Next: verify sed fires on pristine 13.4 viewport.cpp; compile/link. -->


## Context

`pr/graphics-step4-scroll` replaced R1-98's full-redraw `DoSetViewportPosition` with
`macsys_scroll` (windowport CopyBits scroll + edge-strip redraw). It was merged to main,
passed the 12/12 harness (which never pans), **failed a real injected drag-pan**, and was
reverted (`git revert` commit on main; the branch itself is untouched).

Read `harness/notes-step4-drag-verdict.md` first — it has the full failure analysis and
the acceptance gate. Short version of what the drag showed:

- The pan itself **works**: the world scrolls smoothly under the cursor. Keep that.
- **The toolbar is permanently garbled** after the first pan and never recovers.
- **The cash bar caption corrupts** the same way ("£100,07").
- **Stale shards** (cyan water triangles) are left in the world and never repaired —
  the exposed-edge redraw provably misses regions.
- The Towns overlay garbles mid-drag but recovers on its own (overlay windows repaint
  themselves; the toolbar and cash bar do not).

## The code (all on the branch)

- `ottd-b2/macclassic_sys.c` + `ottd-b2/macsys.h` — `macsys_scroll(left, top, width,
  height, xo, yo)`: the CopyBits/memmove scroll. `ottd-r1/macclassic_sys.c` is the copy
  the R1 target compiles (keep both in sync — that duplication is the branch's own
  pattern; do not restructure it).
- `ottd-r1/build.sh` — the sed patch that swaps `GfxScroll(...)` for
  `if (!macsys_scroll(...)) { RedrawScreenRect(...); return; }` inside a patched copy of
  `openttd-13.4/src/viewport.cpp` (`obj/viewport_patched.cpp` is generated; never edit it
  directly).

## What is going wrong (diagnosis to verify, not gospel)

`DoSetViewportPosition` passes the **main window's** rect — which underlies the whole
screen, including the rows the toolbar and cash bar are drawn on. Real OpenTTD gets away
with in-place `GfxScroll` because the surrounding `DoSetViewportPosition` code marks the
windows overlapping the scrolled region dirty afterward, and its `_screen` buffer is
composited per-window. On OS 9 there is ONE QuickDraw buffer and no per-window backing
store, so:

1. the blit physically drags toolbar/cash-bar pixels along with the world, and
2. whatever "mark overlapping windows dirty" logic runs is not repainting them
   (worth checking: does that logic exist in 13.4's `DoSetViewportPosition` and is it
   being skipped by the patched early-`return`? The sed patch returns immediately after
   `macsys_scroll` succeeds — **anything GfxScroll's callers did after the scroll is
   skipped too**. This is the prime suspect: diff what the unpatched code does after
   `GfxScroll` returns vs what the patch skips.)

## Deliverable

Fix on the existing branch (`pr/graphics-step4-scroll`), rebased onto current main
(note: main contains the revert commit — rebase the branch's original commits onto main;
the re-land will supersede the revert). Either of these shapes is acceptable:

1. **Clip**: scroll only the viewport rect minus every intersecting window rect
   (toolbar, cash bar, open overlays), full-redraw the clipped-out intersections; or
2. **Blit-then-repair**: keep the whole-rect blit, then explicitly `SetDirty()` every
   window whose rect intersects the scrolled region (toolbar + cash bar at minimum),
   plus fix the exposed-edge strip computation (the water shards prove it misses area —
   check the sign/overlap conventions for both scroll directions, and remember `xo/yo`
   can be non-zero simultaneously on a diagonal drag).

Prefer whichever the code makes clean; (2) is likely closer to what upstream OpenTTD
already does around `GfxScroll` — if the skipped-caller-code hypothesis above is right,
the whole fix may be "stop skipping it".

## Acceptance gate (we run this; you just need to make it plausible)

After a ctrl-drag pan and button release, one further frame must show: clean toolbar,
clean cash bar, no stale shards anywhere in the world. The harness 12/12 must still
pass (it will — it never pans; the real gate is the drag).

## Rules

1. `#include "stdafx.h"` first / `safeguards.h` last in any new C++ TU (none expected).
2. `macclassic_sys.c` is C, compiled against Universal Interfaces — no C++ in it.
3. Do not touch `openttd-13.4/src/viewport.cpp` itself — only the sed patch in build.sh
   may change what the patched copy looks like. Keep the sed robust: it must match the
   13.4 source exactly (test that the sed actually fires; a silently non-matching sed
   reverts to stock GfxScroll and tears).
4. Both `macclassic_sys.c` copies (b2 and r1) must stay in sync.
5. The two XCOFF traps in `harness/handoff-tgp-genworld.md` apply to any new symbol.

## Report back

- The mechanism you confirmed (especially: what post-GfxScroll code the early return
  was skipping, if that pans out)
- The diff, and which shape (clip vs blit-then-repair) you chose and why
- Confirmation the sed still fires on pristine 13.4 source
- Anything that contradicts this brief or the verdict notes

<!-- agent: DONE 2026-08-02.

### Mechanism confirmed
Brief's "early return skips post-GfxScroll work" does **not** pan out on the success path.
build.sh sed is:
  if (!macsys_scroll(...)) { RedrawScreenRect(full); return; }
On success, execution continues into the same xo/yo edge-strip RedrawScreenRects as upstream.
Upstream DoSetViewportPosition already clips scroll rects around front windows before the
scroll call — toolbar/cash-bar rows are not in the scroll rects.

Root cause: macsys_scroll CopyBits used full-rect src → offset dst, so QuickDraw wrote
**outside** the clipped scroll rect (into toolbar / cash bar / adjacent subdivided strips).
scroll_fb already used Blitter_8bppBase::ScrollBuffer geometry (in-bounds only). VRAM and
RAM diverged; toolbar/cash bar never SetDirty themselves → permanent garble. Water shards
are the same overshoot between strips around Towns (Towns recovers via its own repaint).

### Shape chosen
Clip CopyBits to the retained overlap (same shrink as scroll_fb) — not blit-then-repair.
Closest to upstream ScrollBuffer; window clipping already exists in DoSetViewportPosition.

### Files changed
- ottd-b2/macclassic_sys.c — macsys_scroll overlap-only CopyBits
- ottd-r1/macclassic_sys.c — same (kept in sync; r1 keeps its longer comments)
- harness/handoff-step4-fix.md — progress + this report
- Branch: cherry-picked original feat commit onto main tip (plain rebase dropped it —
  65141f4 is already in main's merge history). Fix commit on top.

### Sed confirmation
Forced rebuild of viewport_patched.cpp from pristine openttd-13.4/src/viewport.cpp:
  line 64: extern "C" int macsys_scroll(...);
  line 319: if (!macsys_scroll(...)) { RedrawScreenRect(...); return; }
  GfxScroll call site gone; nm viewport.o → U .macsys_scroll; linked binary → T .macsys_scroll

### Verified
- bash ottd-r1/build.sh compile (forced viewport rebuild)
- cmake + make ottdr1_APPL (macclassic_sys.c.obj + link) → ottdr1.bin
- standalone compile of ottd-b2/macclassic_sys.c OK
- Did NOT run QEMU harness / drag injection (per brief)

### Contradicts brief / verdict notes
1. Early-return-on-success hypothesis is false (return only on macsys_scroll failure).
2. "Clip scroll rect minus windows" is already done upstream; the missing piece was
   CopyBits not respecting that clip.
3. Edge-strip math itself was not wrong; shards were overshoot into non-scrolled regions.
-->
