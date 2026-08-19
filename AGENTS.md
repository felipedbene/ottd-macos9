# AGENTS.md

## Cursor Cloud specific instructions

This repo is an experimental port of OpenTTD to **classic Mac OS 9 / PowerPC** built with the
**Retro68** cross-compiler. Read `README.md` and `harness/README.md` for the full picture. The
notes below are only the non-obvious things needed to develop and test in a Cursor Cloud Linux VM.

### What can and cannot run in this Linux VM

- The shipping "products" are the `ottd-*` and `hello-*` subprojects. They compile to **PowerPC
  Mac OS 9 binaries** (`*.APPL`/`*.pef`/`*.bin`) and require the **Retro68 toolchain + Open
  Transport SDK + OpenTTD 13.4 source**, none of which are vendored (see `.gitignore`). They
  **cannot be built or run on this Linux VM**, and their outputs only run on Mac OS 9 hardware or
  a QEMU Mac OS 9 guest. Do not attempt to `cmake`/`build.sh` these here — the `build.sh` scripts
  also hardcode the original author's Mac paths (`/Users/felipe/...`).
- The `harness/*.sh` full test loop needs SSH to an external QEMU Mac OS 9 pool host plus a
  `kubectl` UDP log sink (namespace `gopher-spot`). That end-to-end loop is **not runnable here**.

### What IS runnable here (host-side render simulators + harness assertion logic)

Toolchain is preinstalled (system `g++`, `cmake`, `make`, `python3`). All Python tools are
**stdlib-only** (no `pip install`, no PIL despite the README wording — PNGs are written via `zlib`).

1. Portable isometric compositor core (`agent-compositor/`, mirrored in `ottd-landscape/`):
   ```sh
   cd agent-compositor
   g++ -std=c++17 -O2 harness_dump.cpp landscape_render.cpp -o harness_dump && ./harness_dump
   g++ -std=c++17 -O2 harness_mine.cpp landscape_render.cpp -o harness_mine && ./harness_mine
   ```
   Prints the back-to-front tile draw list using the real OpenTTD projection/slope math.
   Gotcha: `harness_dump` and `harness_mine` are **committed binaries**; rebuilding shows them as
   modified in `git status`. Restore with `git checkout -- harness_dump harness_mine` before
   committing so you don't commit host-arch binaries.

2. Full PNG render pipeline (`agent-compositor/render_scene.py`) and tile decoder
   (`agent-tiles/gen_tiles.py`). These decode real **OpenGFX** sprites, so they need two external
   assets that are NOT in the repo, and the scripts reference them via **hardcoded absolute paths**:
   - `ogfx1_base.grf` from OpenGFX 8.0 (`https://cdn.openttd.org/opengfx-releases/8.0/opengfx-8.0-all.zip`),
     expected at `/private/tmp/claude-501/-Users-felipe/ab1a35e1-2df4-4903-b156-bc7b1362f482/scratchpad/opengfx/opengfx-8.0/ogfx1_base.grf`.
   - OpenTTD 13.4 `palettes.h` (`https://raw.githubusercontent.com/OpenTTD/OpenTTD/13.4/src/table/palettes.h`),
     expected at `/Users/felipe/ottd-macos9/openttd-13.4/src/table/palettes.h`.
   Rather than editing these committed scripts, create those exact paths (e.g. `sudo mkdir -p` +
   symlink to the downloaded files). `render_scene.py` first runs `./harness_dump`, so build it
   first. Note: `render_scene.py` **overwrites the committed `agent-compositor/preview/scene.png`** —
   copy the result elsewhere and `git checkout` the preview afterward. `gen_tiles.py` writes to
   `/Users/felipe/ottd-macos9/agent-tiles/previews` (outside the repo), so point that at a temp dir.

3. Harness assertion engine offline (`harness/assert.py`) — validates telemetry against
   `harness/expected.json` without kubectl using the `--from-file` mode:
   ```sh
   python3 harness/assert.py --tag R1-105 --beats 3 --from-file <sink-log>
   ```
   The expected sink line format is documented in the top of `assert.py` (`<ipv4> [<tag>] <msg>`).

### Lint / test / build summary

- No linter, no formal unit-test framework, and no dependency manifest are configured in this repo.
- "Build/run" on Linux == the host-side compositor above. "Test" on Linux == `assert.py --from-file`
  and the `harness_dump`/`harness_mine` smoke drivers. Everything else targets PPC / Mac OS 9.
