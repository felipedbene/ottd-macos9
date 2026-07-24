# harness/ — the autonomous test loop

Bun-blog-style loop (https://bun.com/blog/bun-in-rust): every gate is mechanical, so an
agent can iterate on the Mac OS 9 port without a human booting VMs or eyeballing screens.

```
harness/loop.sh            # ONE iteration: halt → deploy → start → sink assertions → screendump
```

One iteration ≈ 6–9 min unattended: **halt** the VM (frees the AFP lock on the running app),
incremental build (8s for a 1-file change, 82s full) + deploy 1.7s, **start** (OS 9 cold boot +
auto-launch ~4–5 min under TCG), then 3 heartbeats of telemetry judged by 12 assertions.
Artifacts land in `harness/artifacts/<TAG>/` (sink slice, verdict, PNG).

The loop is **halt → deploy → start**, NOT deploy-then-reset: a *running* guest holds
`openttd - latest` open over AFP, so deploy's mv-over-the-app fails "Resource busy" until the
guest is gone. `loop.sh` unmounts this Mac's AFP session cleanly first (so `release_share_lock`
can't sweep it up and drop `/Volumes/vintage`), halts the VM, then remounts the share at the
exact path before deploying.

## Pieces

| Script | Does | Verify story |
|---|---|---|
| `deploy.sh [--no-bump]` | bumps `B2_BUILD_TAG` (R1-NNN+1) in `ottd-r1/r1main.c`, runs the mtime-gated `build.sh compile` + cmake `ottdr1_APPL`, archives `ottdr1-<TAG>.bin` on the share and installs `ottdr1-latest` (real app: data+rsrc forks + APPL FinderInfo survive plain `cp` to the FreeNAS AFP share) | prints ✓ per fork/size check |
| `vm.sh install\|start\|stop\|halt\|reset\|status\|screendump <png>` | drives QEMU on CT 100 (`root@10.0.2.8`, key `~/.ssh/id_openclaw`) over QMP tcp:127.0.0.1:4444; QEMU runs as user `felipe` in tmux session `os9` via `/home/felipe/run-os9.sh` (in-repo copy: `remote/run-os9.sh`). `halt` = stop + wait for qemu to die + `release_share_lock` (VM left OFF, the pre-deploy step); `reset` = halt + start | `status` → `{"running": true}`; screendump → PPM → sips → PNG |
| `assert.py --tag R1-NNN [--follow] [--from-file f]` | slices the UDP log sink by the boot banner for THIS tag; 12 assertions: banner, sink UP, world facts vs `expected.json`, engine live, ≥N heartbeats with strictly-increasing tick, liveness variance (frozen-sim detector), `money==expect` on every FIN line + net trend, zero fault lines | exit 0/1/2; validated 12/12 on real R1-105 data; fault + frozen fixtures FAIL correctly |
| `expected.json` | known-good world facts (corners `"3/4"` until the river-router bug is fixed) | edit when a feature legitimately changes the world |

Sink access: `kubectl -n gopher-spot logs deploy/log-sink` (assert.py falls back from context
`debene` to the current context). Lines are prefixed with the guest IP (`10.0.10.5 `).

## One-time setup already done
- CT 100's `run-os9.sh` gained `-qmp tcp:127.0.0.1:4444,server=on,wait=off` (both boot modes);
  QEMU moved from nohup to tmux (`vm.sh install` + `vm.sh start` re-provision this).
- Mac OS 9: alias to `share:ottdr1-latest` in `System Folder:Startup Items` → every reboot
  mounts the (guest, password-less) share and launches the newest build.

## Gotchas (fought and won)
- **busybox `nc` closes on stdin EOF** → QMP needs `( printf '<cap>\n<cmd>\n'; sleep 2 ) | nc`,
  and every connection starts with the `qmp_capabilities` handshake.
- **Never `pgrep -f qemu` over ssh** — the pattern matches the remote shell itself and kills your
  own session. `vm.sh stop` uses QMP `quit`, then `tmux kill-session`.
- **`build.sh all` always exits 1** — its probe `link` segfaults on the expected `b2_scene_init`
  dup (b2_scene.o vs r1_scene.o). The real link is cmake's (excludes b2_scene.o). deploy.sh
  therefore calls `build.sh compile`; real compile errors still abort (`set -e` intact).
- **mtime gating**: a TU rebuilds when its `.o` is older than source, `build.sh`, or
  `compat/libc_compat.h`. `FORCE=1 bash build.sh all` bypasses. Header changes beyond
  libc_compat are NOT tracked — `FORCE=1` after touching shared headers.
- **Money is not monotonic** between heartbeats (daily costs); the FIN gate is
  `money==expect` (strict) + net trend, not per-beat growth.
- **QMP screendump returns before the PPM is written** — vm.sh waits 2s before scp; sips
  converts QEMU's PPM natively.
- A stale previously-launched app produces confusingly identical traces — that's why deploy.sh
  ALWAYS bumps the tag and assert.py keys on the banner: stale runs are rejected by construction.
- **AFP lock + mount churn**: `release_share_lock` kills the FreeNAS afpd session holding an fd on
  the app's `.AppleDouble`. Because this Mac *writes* the app during deploy, its OWN afpd session
  can hold that fd and get killed too — dropping `/Volumes/vintage`, and the unclean drop makes the
  remount auto-rename to `/Volumes/vintage-1` (deploy needs the exact path). `loop.sh` fixes this by
  unmounting cleanly *before* halt and clearing stray `vintage-N` mounts before remounting.
- **`loop.sh` with no args**: forward `"$@"` (not `"${1:-}"`, which passes an empty-string arg that
  `deploy.sh` rejects as unknown). `loop.sh --no-bump` re-runs the current tag.
- **On-screen text must go through a real Window** (`DrawWidget`/`DrawString`): `r1_viewport_draw` is
  dead code under `-DR1_MERGE` (its only caller is `#ifndef R1_MERGE`), so a viewport-drawn HUD never
  blits. The 12 gates test *telemetry*, not pixels — a correct-but-invisible HUD passes green (exactly
  the "screen ≠ telemetry" gap the Vision-OCR roadmap item closes). See the R1-117 cash bar.

## Roadmap (v2)
1. **Vision-OCR cross-check**: macOS Vision (`VNRecognizeTextRequest`, pyobjc) OCRs the
   screendump's `Money:`/`Population:` panel and asserts screen == sink telemetry — validates
   engine→framebuffer, which telemetry alone cannot.
2. **CLIP on MPS**: zero-shot scene gate ("isometric game map" vs "black screen" vs "error
   dialog"), <1s local; LLM vision only for render-touching changes.
3. The article.
