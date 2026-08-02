# harness/ — the autonomous test loop

Bun-blog-style loop (https://bun.com/blog/bun-in-rust): every gate is mechanical, so an
agent can iterate on the Mac OS 9 port without a human booting VMs or eyeballing screens.

```
harness/loop-pool.sh <slot>     # ONE iteration on one pool slot
harness/fanout.sh <n>           # n builds tested CONCURRENTLY on n slots, one wave
```

Everything runs on **mactrash-can** (`felipe@10.0.1.53`), a Fedora box with 6 real cores.
CT-100 is retired: `loop.sh`, `vm.sh` and `remote/run-os9.sh` are gone, and with them the AFP
share, its single-app lock, and the unmount/halt/remount dance the old loop needed.
Each slot is an independent `qemu-system-ppc` Mac OS 9 guest — own disk, own payload CD, own
QMP/VNC port — so N builds are under test at once. Artifacts land in `harness/artifacts/<TAG>/`
(sink slice, verdict, PNG).

```
slot N   disk   slotN.qcow2      COLD: fresh COW overlay on golden-os9.qcow2
                                 WARM: reflink copy of golden-warm.qcow2 + -loadvm warm
         cd     payload-slotN.iso   HFS CD, volume "SLOT", carrying THIS slot's build
         QMP    127.0.0.1:$((4444 + N))
         VNC    :N
         MAC    52:54:00:9a:bc:0N
```

## Cold vs warm

| | boot to launchable desktop | how the build arrives | status |
|---|---|---|---|
| **COLD** (default) | ~65–85 s (`BOOT_WAIT=120` for headroom) | ISO attached at QEMU start | proven 1 and 3 slots |
| **WARM=1** | ~1 s resume + ~25 s mount (`WARM_SETTLE=40`) | ISO inserted over QMP after the resume | **one slot only** |

`WARM=1` needs `pool.sh warm-create` once — it boots a seed guest off a copy of the golden
image, tidies the desktop, unmounts the AFP share, ejects the CD, and saves a `warm` vmstate
snapshot into `golden-warm.qcow2`. Every slot then reflink-copies that file and resumes it.

**Warm is single-slot only.** One warm slot is 12/12, reproduced three times, and turns a
~150 s cold boot into ~25 s. Several slots resuming together wedge the guests: within about a
minute the framebuffer freezes byte-for-byte, the menu clock stops, an inserted CD never
appears on the desktop — and QEMU still burns 95% CPU, so `query-status` happily says
`running`. Every 3-slot warm wave hit it; slot 1 survives only because it is launched before
the freeze and the running app has its own event loop. **Root cause not found.** Cold waves
are unaffected (3/3, twice), so use the cold path to fan out.

## Pieces

| Script | Does |
|---|---|
| `deploy.sh [--no-bump] [--no-share] [--tag T]` | bumps `B2_BUILD_TAG` in `ottd-r1/r1main.c`, builds, verifies the tag is actually **in the binary**. `--no-share` skips AFP entirely (the pool path). |
| `payload.sh <slot> [app]` | builds slot N's HFS CD (`SLOT:openttd - latest` + `ogfx1_base.grf` + `english.lng`) with `ditto` so both forks survive, verifies fork sizes + `APPL` FinderInfo against the mounted image, scp's it to the pool |
| `pool.sh <cmd> <slot>` | `install start stop status qmp key launch screendump insert savevm loadvm warm-create list` — QMP over ssh via a real python3 client on the remote |
| `loop-pool.sh <slot>` | one iteration: deploy → payload → start → launch → assert → screendump |
| `fanout.sh <n> \| --apps a.APPL,b.APPL` | n builds, n slots, one wave, summary table, exit = worst verdict |
| `assert.py --tag T [--follow]` | 12 assertions on the UDP sink slice for THIS tag: banner, sink UP, world facts vs `expected.json`, engine live, ≥N heartbeats with strictly-increasing ticks, liveness variance (frozen-sim detector), `money==expect` on every FIN line, zero fault lines |
| `expected.json` | known-good world facts; edit when a feature legitimately changes the world |

Sink access: `kubectl -n gopher-spot logs deploy/log-sink` (assert.py falls back from context
`debene` to the current context).

## Run attribution: every line is tagged

The sink NATs **every** sender to one address (`10.0.10.5`), so with N slots reporting at once
the source IP identifies nothing. `netlog.c` therefore stamps each line `[<tag>] ` from
`ottd_log_set_tag(B2_BUILD_TAG)`, and `assert.py` filters the stream by tag.

Without this, slicing "banner to next banner" silently interleaves three runs: a 3-slot wave
produced ticks `[1, 384, 128]` and "houses decreased" for a slot that was in fact healthy.
Untagged lines are kept only when *nothing* in the stream is tagged, so old fixtures still work.

Per-slot builds are `<base>-s<N>` (`R1-131-s1`, `-s2`, `-s3`) — same source, one tag each. To
fan out N genuinely different features, build each in its own worktree and pass the binaries:
`fanout.sh --apps /path/a.APPL,/path/b.APPL` (the tag is read back out of each `.APPL`).

## Gotchas (fought and won)

- **`netlog.o` was built by nothing and tracked by nothing.** `netlog.c` needs Apple's Universal
  Interfaces (`toolchain/otsdk/CIncludes`) while `add_application()` builds against multiversal,
  which has no Open Transport — so no cmake target compiles it, and `.gitignore`'s `*.o` keeps it
  out of the repo. A fresh clone therefore linked against a file that did not exist, and an edit to
  the `.c` silently linked the *previous* object (or died with `undefined reference to
  .ottd_log_set_tag` once the edit added a symbol — which is exactly how this was found).
  `ottd-b1/build-netlog.sh` builds it (`-std=gnu11`: `otsdk/MacTypes.h` enumerates `false`, a
  keyword from C23 on) and `ottd-r1/build.sh` now invokes it whenever the object is missing or
  older than the source.
- **`blockdev-change-medium` takes `"device"`, not `"id"`.** `pay0` is the `-drive` alias and the
  `ide-cd` device has no qdev id, so the `id` form answers `DeviceNotFound`. That error, once
  discarded to `/dev/null`, is the whole reason the pool was believed to need a cold boot per
  build — OS 9 mounts an inserted CD fine, the insert had simply never happened.
- **`snapshot-save` addresses block NODES, not `-drive` ids.** Passing `hd0` created a job that
  *concluded* — with `error: "No block device node 'hd0'"`. Since success also looks like
  "concluded", the poll called it saved and `warm-create` produced an image with no snapshot in
  it. The root disk now carries `node-name=os9root`, and `job_wait` fails on a job's `error`.
- **A stalled sink looks exactly like a hang.** `assert.py` reports `HANG: no heartbeat for 90s`
  when telemetry stops — but the guest may be perfectly healthy. One "hung" slot's screendump
  showed OpenTTD alive at year 1950, population 1632, hundreds of ticks past its last heartbeat;
  only its UDP log lines had stopped arriving. `netlog`'s Open Transport send is non-blocking and
  drops under burst. **Always check the screendump before believing a HANG verdict** — this is the
  same screen-vs-telemetry gap the Vision-OCR roadmap item closes.
- **Type-select is a timing budget, not a string.** The Finder resets its type-select buffer after
  ~1.3 s and `sendkeys` spaces chords 0.3 s apart, so a 7-character prefix (2.1 s) *always* resets
  mid-word and selects the wrong thing — silently. `warm-create` typed `vintage` and left the share
  mounted; the launch recipe typed `openttd` and got away with it only because every partial match
  still lands on the right file. Use the shortest unique prefix (`sl`, `op`, `v`).
- **Cmd-E is Eject, Cmd-Y is Put Away.** Cmd-E does nothing to a *server* volume, so the AFP share
  survived two attempts to unmount it from the warm seed.
- **`send-key` hold-time is guest time.** QEMU's 100 ms default can pass without a contended guest
  polling its USB HID device, and the keystroke is never seen. `sendkeys` sets `hold-time: 200`.
- **Retro68 `_open_r` hardcoded `fsRdWrPerm`** (computed `permission` then ignored it), so every
  `fopen(..., "rb")` failed on read-only media: the CD payload listed fine via `PBGetCatInfo` but
  gave "Cannot open file 'ogfx1_base.grf'". Fixed in `Retro68/libretro/syscalls.c`; rebuild with
  `cd Retro68-build/build-target-ppc/libretro && make retrocrt`.
- **make's mtime granularity is one second**, so back-to-back `--tag` builds produced *byte-identical*
  binaries — three "different" fan-out builds were the same build. `deploy.sh set_tag` now deletes
  the whole chain (`r1main.c.obj`, `.xcoff`, `.pef`, `.bin`, `.APPL`, `.ad`, `%.ad`).
- **`strings … | grep -q` + `set -o pipefail`** fails the pipeline via SIGPIPE on the producer.
  The tag gate counts into a variable instead: `grep -cxF "$TAG" || true`.
- **OS 9 will not mount a bare hdiutil HFS+ hard disk** (no `Apple_Driver_ATA` partition — only
  Drive Setup writes one). It mounts an HFS **CD-ROM** with no driver at all. Hence the payload CD.
- **`cp` drops the resource fork** onto an HFS image (`rsrc=0`); `ditto` preserves it.
- **QMP screendump returns before the PPM is written** — `pool.sh` waits 2 s before scp; `sips`
  converts QEMU's PPM natively.
- **`meta_l` is Command** on this guest. The Finder drops keystrokes sent back-to-back, so
  `sendkeys` sleeps 0.35 s between chords, and type-select needs the target window *open* before
  the next chord group (hence the 3 s pause in `launch`).
- **Money is not monotonic** between heartbeats (daily costs); the FIN gate is `money==expect`
  (strict) + net trend, not per-beat growth.
- **On-screen text must go through a real Window** (`DrawWidget`/`DrawString`): `r1_viewport_draw`
  is dead code under `-DR1_MERGE`, so a viewport-drawn HUD never blits. The 12 gates test
  *telemetry*, not pixels — a correct-but-invisible HUD passes green (exactly the "screen ≠
  telemetry" gap the Vision-OCR roadmap item closes). See the R1-117 cash bar.

## Build speed

`ottd-r1/build.sh compile` builds 60 independent TUs. It used to run them one at a time — 41 s
with 11 of 12 cores idle. It now runs an `xargs -P` job pool (`/bin/bash` on macOS is 3.2, so no
`wait -n`), ordered longest-first:

| | before | after |
|---|---|---|
| full rebuild (60 TUs) | 41 s | **7 s** |
| incremental, 1 TU | 5 s | 5 s |
| tag-only relink (per fan-out slot) | 1 s | 1 s |

All 60 objects are byte-identical to the serial build, and a compile error still aborts with a
non-zero exit (xargs returns 123 when any child fails). `JOBS=1` restores serial ordered output.

Ordering matters as much as the pool: `town_cmd.cpp` and `r1_scene.cpp` cost ~4.5 s each against
≤2 s for everything else, so the longest TU *is* the critical path. In source order `r1_scene` was
queued 57th of 60 and ran alone at the tail (9 s); sorting by source size starts both poles in the
first wave (7 s, against a 4.5 s floor). Going below that needs a PCH or splitting those TUs.

## Concurrency

Each TCG guest is ~1 host core; mactrash-can has 6 real cores (12 threads), so n≤6 runs at full
speed. Measured: 3 concurrent VMs ≈ 80% CPU each, load 2.26/12.

## Roadmap (v2)
1. **Vision-OCR cross-check**: macOS Vision (`VNRecognizeTextRequest`, pyobjc) OCRs the
   screendump's `Money:`/`Population:` panel and asserts screen == sink telemetry — validates
   engine→framebuffer, which telemetry alone cannot.
2. **CLIP on MPS**: zero-shot scene gate ("isometric game map" vs "black screen" vs "error
   dialog"), <1s local; LLM vision only for render-touching changes.
3. The article.
