# Handoff: the ~130-line netlog freeze (telemetry dies at tick 512, app at tick 768)

<!-- agent: started 2026-08-02; audited netlog.c + statsd.c; implementing A (file tee + NETSTAT) and B1/B2 (OTLook drain loop); NOT running QEMU harness -->

Task brief for an external coding agent. Everything you deliver is verifiable by
**compiling**; the runtime validation loop (QEMU slot pool) is run on our side. Do not
run the test harness.

## The bug (measured, deterministic, pre-existing)

Every R1 run — five different builds checked (R1-138/140/141/143/144), pre- and
post-merge alike — dies in **two stages**:

1. **~130–132 UDP datagrams after launch, the sink stops receiving.** The last
   heartbeat that arrives is always `tick=512`.
2. The game demonstrably keeps running ~2 more beats **silently** (the frozen
   screen always shows money `99816`, which is the tick-768 value; the last
   *logged* FIN is `99862` = tick 512) and then **hard-freezes**: no repaint, no
   input wakes it, forever.

Full observation log: `harness/notes-step4-drag-verdict.md` (bottom section).

The most interesting fact is the gap: **logging dies first, the game survives it
by ~256 ticks, then the whole app wedges.** Whatever breaks at ~line 130 is
survivable for a while; whatever happens at ~tick 768 is not. They may or may not
be the same root cause — design the instrumentation to tell them apart.

## The code

- `ottd-b1/netlog.c` — the log front-end. Note `NETLOG_THROTTLE` (line ~39): after
  every UDP send it **busy-waits one TickCount() tick** "so OT drains". Note also:
  when `g_net` is up, lines go **only** to UDP (`statsd_log`) — the file tee is
  dead code on the happy path, which is exactly why we have zero visibility into
  the freeze.
- `mac-minivnc/mac-cpp-source/statsd.c` (guarded by `-DSTATSD_OT`) — the OT UDP
  emitter. `statsd_open` opens ONE endpoint, `OTSetSynchronous` + `OTSetNonBlocking`,
  `OTBind(ep, NULL, NULL)`. `sd_send_to` does `OTSndUData`; on `kOTLookErr` it
  clears exactly one `T_UDERR` and retries once. Counters `g_ok/g_fail/g_lasterr`
  exist but are never surfaced anywhere.

## Deliverables

**A. Instrumentation (must land regardless of whether you also fix it).**
Make the failure observable without the sink (the sink is the dying channel):

1. In `netlog.c`: re-enable the file tee alongside UDP (`file_line` on every line,
   not just when `g_net == 0`). The file is on the guest disk; we read it after a
   freeze. Keep it cheap — open/append/close per line is fine (it already works
   that way for the fallback path).
2. Every 32 lines, also write a diagnostic line **to the file only**:
   `NETSTAT: ok=<g_ok> fail=<g_fail> lasterr=<g_lasterr>` — you will need a small
   getter added to statsd.c (`statsd_stats(long *ok, long *fail, long *err)`);
   the counters already exist as statics.

This alone will split the two stages: if the file keeps growing past line 130
while the sink is silent, the send path is dropping (stage 1) and the freeze
(stage 2) is elsewhere; if the file ALSO stops at ~130, the freeze is in/behind
`ottd_log` itself.

**B. Root-cause candidates to check in the code (audit, and fix what you find).**
In rough order of suspicion:

1. **OT event pile-up.** `sd_send_to` only ever clears `T_UDERR`. A synchronous
   non-blocking endpoint that gets any OTHER pending event (`T_GODATA` after flow
   control, etc.) may return `kOTLookErr` on every subsequent send — and after
   enough ignored events OT can wedge the endpoint. Handle `kOTLookErr` by
   draining **whatever** `OTLook` reports (loop, not once), not just `T_UDERR`.
2. **Flow control.** `kOTFlowErr` is counted in `g_fail` and the datagram is
   dropped — but is the endpoint then required to consume `T_GODATA` before the
   next send works? If yes, that's stage 1 exactly.
3. **The throttle spin** (`while (TickCount() == t) {}` in netlog.c): TickCount
   advances at interrupt time, so this cannot wedge on its own — but at ~35
   lines/beat it costs ~0.5 s of busy-wait per beat. Consider whether it is still
   needed at all if (1)/(2) are fixed properly; it was a bring-up hack.
4. **OT memory/handle leak** in the sync send path — anything allocated per send
   that is never freed would fit "dies after a fixed count".

If you identify the cause statically, fix it in the same patch, with a comment
explaining the mechanism in the style of the existing comments in statsd.c
(the T_UDERR comment there is the house voice: name the trap, then the fix).

## Build commands (use verbatim; both objects are CHECKED-IN prebuilts)

netlog.o (script already exists — run after any netlog.c edit):

```
bash /Users/felipe/ottd-macos9/ottd-b1/build-netlog.sh
```

statsd_ot.o (no script yet — this is the recipe, verified to compile today;
**add it as `ottd-b1/build-statsd.sh`** in the style of build-netlog.sh, and cp
the result to BOTH `ottd-b1/statsd_ot.o` and `ottd-b2/statsd_ot.o`):

```
cd /Users/felipe/ottd-macos9 && tc=Retro68-build/toolchain && \
$tc/bin/powerpc-apple-macos-gcc -c -O2 -std=gnu11 -w -DSTATSD_OT \
  -I$tc/otsdk/CIncludes mac-minivnc/mac-cpp-source/statsd.c -o ottd-b1/statsd_ot.o
```

Verify with `$tc/bin/powerpc-apple-macos-nm` — the five `.statsd_*` T symbols
must still be exported, plus your new getter.

## Rules

1. Do NOT change the `ottd_log_init/ottd_log/ottd_log_close/ottd_log_set_tag`
   signatures — three link surfaces (b1/b2/r1) consume them as-is.
2. `statsd.c` lives in the vendored MiniVNC tree — keep the `#ifdef STATSD_OT`
   structure intact (the non-OT no-op branch must still compile for MiniVNC).
3. The trap documented in build-netlog.sh is real: **editing the .c changes
   nothing until the .o is rebuilt and copied** — the build links the prebuilt
   object. Rebuild both .o files and say so in your report.
4. No cmake/build.sh changes.

## Report back

- Which stage-1/stage-2 mechanism you found (or ruled out), with the line refs
- The exact diff to netlog.c / statsd.c
- Confirmation both .o files were rebuilt (nm output for the new symbols)
- Anything that contradicts this brief

<!-- agent: compile-only verification done; both .o rebuilt + copied to b1/b2; writing final report -->

## Agent report (2026-08-02)

### Root cause (static)

**Stage 1 (sink dies ~line 130 / tick 512):** `sd_send_to` only cleared `T_UDERR`
once on `kOTLookErr` (`statsd.c` old path). A sync non-blocking UDP endpoint that
leaves **any other** look queued — especially `T_GODATA` after `kOTFlowErr`, or
stray `T_DATA` — returns `kOTLookErr` on every later send. That matches
"datagrams stop arriving after a fixed count while the game keeps running."

**Stage 2 (hard freeze ~tick 768):** not fully provable compile-only. Two
plausible contributors, both addressed:
1. Enough ignored OT looks can wedge the provider (same bug as stage 1, delayed).
2. `NETLOG_THROTTLE` busy-wait (~1 TickCount per line ≈ 0.5 s/beat at ~35
   lines/beat) starves the cooperative Mac event loop. Disabled now that drain
   replaces it.

**Ruled out:** OT per-send memory leak — `TUnitData` is stack-local; buffers are
caller-owned; no `OTAlloc` in the send path.

### Files changed

| File | Change |
|------|--------|
| `mac-minivnc/mac-cpp-source/statsd.c` | `sd_drain_looks()` loop; drain+retry on `kOTLookErr`; `statsd_stats()` getter (+ no-op branch) |
| `mac-minivnc/mac-cpp-source/statsd.h` | declare `statsd_stats` |
| `ottd-b1/netlog.c` | always `file_line` tee; every 32 lines file-only `NETSTAT:`; `NETLOG_THROTTLE 0` |
| `ottd-b1/build-statsd.sh` | **new** — recipe from brief, copies to b1+b2 |
| `ottd-b1/{netlog,statsd_ot}.o` + `ottd-b2/` copies | rebuilt prebuilts |

Signatures `ottd_log_init/ottd_log/ottd_log_close/ottd_log_set_tag` unchanged.
No cmake/build.sh. No Step 4 drag-pan files.

### How verified

```
bash ottd-b1/build-netlog.sh
bash ottd-b1/build-statsd.sh
```

nm `statsd_ot.o` T symbols (five originals + getter):
```
T .statsd_close
T .statsd_fmt
T .statsd_log
T .statsd_log_open
T .statsd_open
T .statsd_send
T .statsd_stats
```
Also `U .OTRcvUData` (new drain dependency). netlog.o: `U .statsd_stats`.
b1/b2 copies `cmp`-identical. QEMU harness **not** run (per brief).

### Runtime split (for your side)

After a freeze, read the guest file next to the app:
- File keeps growing past ~130 + `NETSTAT` lines → stage 1 was send-path drop;
  stage 2 elsewhere (or OT wedge after silent fails).
- File also stops ~130 → freeze is inside/behind `ottd_log`.
- `NETSTAT fail` climbing / `lasterr=-3158` (`kOTLookErr`) / `-3161` (`kOTFlowErr`)
  confirms the OT look/flow theory.

### Contradicts brief?

Nothing material. Notes:
- Stage 2 hard-freeze identity with stage 1 is still a hypothesis until the file
  tee is read post-freeze on hardware/QEMU.
- Throttle was **disabled** (brief said "consider"); drain is the intended
  replacement.
- Brief said "`#ifdef STATSD_OT`"; source actually uses `#if STATSD_OT` —
  structure left intact, no-op branch updated.