# Graphics acceleration on the clamshell iBook G3 — what the ATI Rage Mobility can and can't do for this port

**Phase 1 analysis.** Grounded in the current pipeline: `ottd-b2/macclassic_v.cpp`
(OpenTTD-side driver) and `ottd-b2/macclassic_sys.c` (Toolbox side).

## TL;DR

On this hardware the GPU is **not your lever**. The clamshell's ATI Rage Mobility
is a fixed-function 2D blitter (Rage Pro class) with a video scaler and a weak
3D core. The part of OpenTTD's graphics that costs almost all the time —
**per-pixel indexed sprite compositing with recolour tables and RLE
transparency** — is exactly the thing that class of hardware *cannot* do. That
work stays on the G3, and the G3 (PowerPC 750) has **no AltiVec**, so there's
not even SIMD to fall back on.

What *can* ride the hardware is the cheap tail:

| Layer | Share of a frame* | HW-accelerable? | Status |
|---|---|---|---|
| Sprite compositing → 8bpp buffer (the blitter) | **~85–95%** | ❌ No — fixed-function 2D can't do indexed recolour/RLE-transparency blits | CPU (G3, scalar) |
| Final `CopyBits` buffer → screen (present) | ~3–8% | ⚠️ Partly, at same depth; cheap either way | CPU QuickDraw today |
| Palette animation (water/lights) | <1% | ✅ Yes — **already 100% hardware** (RAMDAC) | `SetEntries`, done |
| Viewport scroll (retained region copy on pan) | spiky, pan-only | ✅ Yes — a VRAM→VRAM blit | CPU today |
| Solid fills / clears | small | ✅ Yes — hardware rectangle fill | CPU today |
| Mouse cursor | per-frame, tiny | ✅ Yes — **already hardware** (Mac arrow) | `UseSystemCursor()=true` |

\* Rough split for a typical viewport frame; measure to confirm.

**Bottom line:** you can realistically hand the Rage single-digit-percent of
today's frame time (present + fills), reclaim the scroll-copy during panning,
and you *already* get palette animation and the cursor for free. The 85–95%
that hurts is untouchable by this GPU. Real speedups on the clamshell come from
**CPU-side** work (dirty-rect discipline, less overdraw, a faster 8bpp blitter,
string caching), not from offloading to hardware.

---

## The hardware

**Clamshell iBook G3** (1999–2001), all variants:
- **CPU:** PowerPC 750 (G3), ~300–466 MHz. **No AltiVec.** Small L1 + backside L2.
- **GPU:** ATI Rage Mobility (Rage Pro-derived), **~4 MB VRAM**.
  - 2D engine: hardware BitBlt (screen↔screen and host→screen), rectangle
    fills, ROPs, monochrome→colour expansion (text), **hardware cursor**.
  - Video: YUV overlay + hardware scaler (built for DVD playback).
  - 3D (RAVE / QuickDraw 3D): textured/Gouraud triangles, **low fillrate**,
    living inside the 4 MB VRAM budget.
  - CLUT: hardware RAMDAC (this is what makes `SetEntries` palette animation free).

The critical limitation: the 2D blitter copies **flat rectangles** with one ROP
and at most a colour-key or mono-expand. It has **no programmable per-pixel
path** — no indexed recolour LUT, no per-run transparency decode. OpenTTD sprites
need all of that.

## Why sprite compositing can't be offloaded

The generation path (grounded in code):

- `macclassic_v.cpp:81` points `_screen.dst_ptr` at `macsys_fb()` — a plain
  main-memory buffer (`NewPtr`, `macclassic_sys.c:139`).
- OpenTTD's 8bpp software blitter (`8bpp_*`) composites every sprite into that
  buffer: terrain tiles, foundations, buildings, vehicles, trees, text glyphs,
  GUI. For each sprite it does a per-pixel, palette-**indexed** copy with:
  - **RLE/chunked transparency** (skip transparent runs),
  - **recolour remapping** through 256-entry LUTs (company colours, the
    transparency/shadow tables),
  - overdraw in painter's order via the sprite sorter.

None of that maps onto a fixed-function rectangle blitter. To do it on the Rage
you'd need a programmable per-pixel stage the chip doesn't have. So this ~85–95%
stays on the G3, scalar.

### The "use the 3D engine as a 2D compositor" idea — considered, rejected

You *could* imagine uploading sprites as textures and drawing textured quads
(the modern 2D-on-GPU trick). On the Rage Mobility it's a net loss:
- **VRAM budget:** the OpenGFX sprite set is far larger than 4 MB; you'd be
  streaming textures over the bus constantly.
- **Semantics break:** palette-indexed sprites, recolour tables, and CLUT-based
  palette animation don't survive the trip to RGBA textures. You'd lose the
  free water shimmer and company-colour recolouring, or reimplement them per-quad.
- **Fillrate:** Rage Pro textured fill is low; hundreds of tiny quads per frame
  with per-quad QD3D/RAVE setup overhead would be slower than the CPU blitter.
- **Cost:** a full renderer rewrite for a slowdown.

## What *is* worth taking from the hardware

1. **Keep the screen at 8-bit — don't lose the acceleration you already have.**
   `macclassic_sys.c:205` only calls `SetEntries` when the main device is ≤8bpp,
   and `CopyBits` is cheapest when source and screen are the **same depth**. If
   the panel/VNC session runs in thousands/millions of colours, you lose *both*:
   `CopyBits` becomes a CPU CLUT-expand + dither, and hardware palette animation
   stops (the water goes static). This is the single most important "don't
   regress" point. (Note: `MiniVNC-1.5-truecolor` on the transfer share — make
   sure the *game* session is 256 colours.)

2. **Palette animation — already fully hardware.** `CheckPaletteAnim`
   (`macclassic_v.cpp:142`) → `PushPalette` → `SetEntries` drives the RAMDAC
   directly; water/lights shimmer with **no framebuffer redraw**. Nothing to do;
   this is the one thing already 100% offloaded.

3. **Hardware scroll blit during panning (real win, pan-only).** When the
   viewport pans, the retained region can be moved with a single **VRAM→VRAM
   BitBlt**, leaving only the newly-exposed edge strip for the CPU blitter.
   Requires the framebuffer to live where the accelerator can blit it
   (an accelerated offscreen GWorld / DrawSprocket back buffer), not the current
   bare `NewPtr`. Medium effort, helps exactly the case that feels laggy today.

4. **Hardware rectangle fills** for clears and solid GUI panel backgrounds —
   small but free once you're issuing QuickDraw fills against an accelerated port.

5. **Hardware cursor — already free.** `UseSystemCursor()` returns true
   (`macclassic_v.cpp:48`), so the arrow is the Rage's hardware cursor; no
   per-frame cost.

### A caveat on "accelerated present"

`macsys_blit` (`macclassic_sys.c:183`) `CopyBits` from a **main-memory** PixMap.
Whether the ATI driver accelerates that host-source blit or falls back to CPU
QuickDraw depends on the driver; a bare RAM PixMap often *isn't* accelerated. You
could force it by sourcing from a VRAM GWorld — but then CPU compositing into
VRAM is punishingly slow (uncached VRAM writes), which would cost far more than
it saves. The pragmatic answer: **composite in RAM, present with CopyBits** (as
now). Present is only a few percent of the frame, so it isn't worth contorting.

## Where the real frame time goes (CPU-side levers, for contrast)

Since the GPU can't help the hot path, the wins live here:
- **Dirty-rect discipline.** `Paint()` (`macclassic_v.cpp:125`) falls back to a
  full 640×480 blit when >16 rects are dirty, and the R1 scene forces a
  whole-screen redraw every 8th tick. Both re-composite (CPU) far more than
  needed. Tightening this is likely the biggest single win.
- **Overdraw / sprite count** in the viewport sorter.
- **A faster 8bpp blitter** (better inner loop; the G3 has no SIMD but there's
  headroom in the scalar path and in cache behaviour).
- **String/label caching** (town names are re-laid-out every frame in
  `ViewportDrawStrings`).

## Recommendation for Phase 2

If Phase 2 is "make it faster," rank by payoff on *this* hardware:
1. CPU-side dirty-rect + overdraw reduction (biggest win, no GPU needed).
2. Hardware scroll-blit for panning (the one GPU offload with real payoff).
3. Lock the session to 8-bit to protect the free palette animation + cheap present.
4. Hardware fills for clears/panels (minor).

Confirm the exact clamshell variant (VRAM 4 vs any 8 MB unit, Rage Mobility vs
Rage Mobility 128) if we go down the DrawSprocket/VRAM path — it changes the
back-buffer budget, but not the conclusion above.

---

# Phase 2 — implementation log

## Step 1 — targeted bus dirty (DONE, awaiting HW validation)

**The biggest single win.** `r1_tick` used to fire `MarkWholeScreenDirty()` every
8th tick purely to animate the bus, forcing a full re-composite of the entire
zoomed map + a re-layout of every town-name string ~4×/second. Replaced with a
native `MarkAllViewportsDirty` around the bus's own projected bounding box —
identical visible cadence, but only the bus's ~2-tile screen region re-composites.

Changed files:
- `ottd-r1/r1_scene.cpp`: new `r1_bus_mark_dirty()` (projects a generous world box
  around the bus through the zoom-independent `RemapCoords`, unions with the
  previous frame's box to erase the old sprite, calls `MarkAllViewportsDirty`);
  `r1_tick:595` now calls it instead of `MarkWholeScreenDirty()`.
- `ottd-b2/macclassic_v.cpp`: `MainLoop` now times `Tick()` work (excluding the
  pacing sleep) and logs `R1 perf: 256 frames work=… (~N.NN ms/frame)` every 256
  frames, so the win is measurable on hardware.

Verified here: both TUs compile clean with the Retro68 toolchain; the only new
external symbol, `MarkAllViewportsDirty`, is defined by `viewport.cpp` (already in
the R1 link) and is **not** stubbed anywhere → no dup, no link segfault.

**HW validation (one deploy):**
1. Rebuild the two objects and relink: `cd ottd-r1 && bash build.sh compile`
   then `cd build && cmake .. && make ottdr1_APPL` → `ottdr1.bin`.
   (Also rebuild `ottd-b2/macclassic_v.o` with the B2 flags so the perf log links.)
2. Bump `B2_BUILD_TAG` in `r1main.c`, deploy, decode fresh on the Mac.
3. Watch the sink: `kubectl -n gopher-spot logs deploy/log-sink | grep 'R1 perf'`.
   Compare `ms/frame` to a baseline build (revert the one line to
   `MarkWholeScreenDirty()` for the A/B). Expect a clear drop.
4. Eyeball: the bus still animates on its road; town growth still repaints;
   water still shimmers (palette anim is independent).
   **Regression to watch for (can't test off-hardware): bus smearing** (old image
   not erased) or the bus vanishing → the dirty box is too small; widen `MX`/`MZ_*`
   in `r1_bus_mark_dirty`.

## Step 2 — Paint() rect coalescing (DONE, in R1-65 bundle)

`macclassic_v.cpp`: `MAX_RECTS` 16→32, and on overflow `Paint()` now blits the
**union bounding box** of all dirty rects (tracked in `MakeDirty`) instead of the
whole 640×480. Also fixes a latent waste: overflow frames used to present the
entire screen even when the dirty content was a small cluster.

## Step 3 — 8-bit depth guard (DONE, in R1-65 bundle)

`ottd-r1/macclassic_sys.c` (the copy the R1 build actually compiles — *not* the b2
copy): logs the main-device depth at window creation. `screen depth=8bpp (OK…)`
or a `WARN` telling you to drop the session to 256 colours. No display-state
mutation (safe); it just surfaces the regression that kills free palette animation
+ cheap present.

## R1-65 bundle — built, deployed, RUN on hardware

Steps 1+2+3 built with the Retro68 toolchain into `ottdr1.bin` (clean link, no dup
segfault), tag `R1-65`, deployed to `/Volumes/vintage/ottdr1-R1-65.bin`.

**On-hardware results (clamshell, over MiniVNC):**
- Subjectively smoother ("feels better") — Step 1 removed the every-8th-tick
  whole-map re-composite.
- `R1 perf`: ~51.7 ms/frame at startup, settling to ~42.9 ms/frame — the new
  measured redraw floor (no R1-64 baseline; old build had no perf log).
- **KEY FINDING (Step 3 diagnostic): the session is running at 32bpp, not 8-bit.**
  So right now (a) every `CopyBits` present is an 8→32 CLUT-expand on the CPU, and
  (b) hardware palette animation is disabled (`SetEntries` is skipped at >8bpp).
  Both are pure loss. The MiniVNC-truecolor session is the likely cause.
  **Highest-value next move, zero code: drop the display/VNC session to 256 colours**
  and re-measure `R1 perf`. Expected: lower ms/frame + water shimmer restored for
  free. This is likely worth more than Step 4 (hardware scroll-blit).

## R1-66 — bus smoothness + corrected perf reading

- **Bus fix:** R1-65 repainted the bus on the old every-8th-tick cadence, which
  *sampled* the 1-sub-step/tick motion coarsely → read as irregular/"faster at the
  ends" near the route reversals. R1-66 repaints the bus EVERY tick (now cheap via
  the targeted dirty) → smooth 16-frames/tile motion, and consecutive dirty boxes
  are ~1px apart (safest against smearing). `r1_scene.cpp`, deployed as
  `ottdr1-R1-66.bin`.
- **Corrected 256-colour reading:** the first 98–130 ms/frame samples after the
  8bpp relaunch were STARTUP (window creation + first full map renders). Steady
  state at 8bpp is **~27–32 ms/frame**, essentially the same as steady-state 32bpp
  (~24–35). So the depth switch did **not** change measured Tick() work materially
  (that cost is dominated by sim + full-map recomposite on growth, not the
  present/SetEntries delta). **The real 8bpp win is visual: hardware palette
  animation (water/lights shimmer) is restored for free.** Keep 256 colours.
- **Measurement caveat learned:** `R1 perf` times all of `Tick()` = sim
  (`r1_tick`/growth) + composite + present, so it's not purely "redraw"; and the
  first 2–3 windows after any boot are startup-heavy. Compare steady-state only.

## Known non-fatal issue (pre-existing, not Phase 2)

`GetHighestSlopeCorner` (`slope_func.h:137`) logs `NOT_REACHED` when a redrawn tile
has a slope outside {single-raised-corner, steep}. It's a *stubbed* NOT_REACHED
(logs + continues — the app keeps running), i.e. the long-noted flat↔hill
foundation/slope cosmetic glitch, not a crash and unrelated to the Phase 2 redraw
changes (whole-screen redraws triggered it *more* often). Candidate for a later
correctness pass, not a graphics-acceleration item.

## R1-73..75 — smooth bus (ROOT CAUSE found & fixed)

The bus "engasgando" was chased across R1-65..74. Findings, in order:
- Time-based bus motion (R1-67) + per-frame clamp (R1-68) removed the tick-locked
  jitter but left periodic hitches.
- Present probe (R1-68) proved `CopyBits` is **CPU, ~35 ns/px, not ATI-accelerated**
  (answers the GPU question) — but present is cheap; the cost was elsewhere.
- **THE ROOT CAUSE (R1-74):** `r1_tick` repainted the ENTIRE map (`MarkWholeScreenDirty`)
  on every change in total house count. It was a workaround for the *batched*
  `CMD_EXPAND_TOWN` (30-40 houses/frame → too many per-tile marks). Once growth was
  made incremental (R1-73, via `TOWN_IS_GROWING`), that line fired a full-screen
  recomposite on *every single new house* → the periodic hitch. GrowTown already marks
  each new house/road tile dirty itself (`MarkTileDirtyByTile`, town_cmd.cpp:504/2315),
  so the whole-screen repaint was redundant. **Deleting it made the bus smooth.**
- Also throttled palette animation to every 4th frame (R1-73): `SetEntries` blocks on
  vertical-blank at 8bpp, so per-frame CLUT updates added jitter.

**Result:** full-screen recomposites dropped from ~50% of frames to ~0; frame work
went from ~43-75 ms to **~1 ms/frame** (bus-only frames). Bus is smooth. Real fixes
kept: incremental growth, palette throttle, time-based bus, Paint union-blit
(MAX_RECTS 32), 8-bit session. Debug probes removed in R1-75.

**The reusable lesson:** on a software-rendered viewport, *never* `MarkWholeScreenDirty`
for a localized change — let the engine's own `MarkTileDirtyByTile` do targeted marking.
A single stray whole-screen mark per event is invisible in a frame-time *average* but
reads as a periodic hitch to the eye.

## Staged backlog (next batches, reward-ordered)
- **Step 4 — hardware scroll-blit on pan** (the one real GPU offload): move the
  framebuffer into an accelerated GWorld / DrawSprocket back buffer and use a
  VRAM→VRAM `CopyBits` for the retained region during pans. Bigger effort, real
  payoff *only while panning*. Needs the exact clamshell VRAM budget confirmed.
- **Step 5 — CPU blitter / overdraw** (sparser forests, town-name string caching):
  chips at the hot path the GPU can't touch. Marginal, incremental.
