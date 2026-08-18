# The Linker That Put Two Variables at One Address

*How a GNU binutils XCOFF bug made every build of our OpenTTD → Mac OS 9 port die silently in Classic — while an emulator hid it from us for weeks — and the one-night hunt that ended with a TOC dump showing two live objects at the same address.*

<!-- DRAFT — TODO items are marked inline. Forensic kit referenced here is still
     in-tree (ottd-m1/zzz_probe.cpp, the *_mark.cpp markers, the ab_econ rename
     in ottd-r1/build.sh); remove after this article ships. -->

---

We are porting OpenTTD 13.4 to Mac OS 9 on PowerPC — real iron, not just emulators. The toolchain is Retro68: GCC cross-compiling to XCOFF, GNU binutils' `ld` linking it, and Retro68's MakePEF converting the result into the PEF container that the classic Mac OS Code Fragment Manager (CFM) actually loads. Our CI is a pool of QEMU guests running Mac OS 9.2 that boot the game, drive it, and read back UDP telemetry. An iteration is 81 seconds. It had been green for weeks.

Then we pointed the same binary at real hardware.

## The symptom: swallowed whole

The new rig is an iMac G4 — the desk-lamp one, hostname `pixalito` — running Tiger 10.4.11. It can't boot OS 9 natively (it's a USB 2.0 model), so the game runs under the Classic environment: Mac OS 9 as a guest process on OS X, sharing the address space rules of `TruBlueEnvironment`.

An old build from July 22 ran *beautifully* on it. Windowed, correct 8-bit colour, 3–14 ms per frame, towns growing, a five-bus fleet earning money, full telemetry streaming to the log sink. It ran for hours.

Every build after R1-140 did this:

```
$ open OTTDR1.APPL
$ # ...nothing.
```

No crash dialog. No process. No log line, not even the first one. No entry in the Classic environment's process list. `open` returned success and the machine sat there. Sometimes a launch attempt took the whole Classic environment down with it — silently — which means a two-minute full OS 9 reboot before you can try again.

The same binaries were 12/12 green on the QEMU pool.

That's fifteen days of builds — some forty-six tagged versions, including the entire "real vehicle stack" milestone — all broken on the only environment that is the actual point of the project, and nothing in the pipeline knew.

<!-- TODO: the brief for this article says "five weeks"; git dates the last-good
     build f40094c and first-bad ae770f8 (R1-140) both at 2026-08-02, and the
     hunt at 2026-08-17 — fifteen days. Confirm with Felipe which count is
     right before publishing. -->

Git bisect over deployed binaries gave us a clean bracket fast: `f40094c` (R1-124-era) launches, `ae770f8` (R1-140, "link the REAL OpenTTD vehicle stack") does not. R1-140 pulled `roadveh_cmd.cpp`, `vehicle.cpp`, `engine.cpp` and `order_cmd.cpp` into the link — a big, boring growth step. Nothing in it touches startup.

What follows is one night of theories, each of which fit the evidence partially, and each of which got a funeral.

## Theory 1: it's too big (RIP)

The obvious suspect for "the loader won't even start it": some size or metadata limit in CFM's fragment preparation. The binary had grown past 5 MB. Classic is a guest inside another OS; maybe its loader has ceilings the real OS 9 doesn't.

So we built a probe series: minimal Retro68 apps with the SIZE resource (the classic Mac memory-partition declaration) swept across the suspect range, with 2 MB of data and 6 MB of text to match the real app's shape. All of them launched fine under Classic. We diffed the PEF headers of a good build against a bad one — import counts, resource layout, entry point fields — identical in kind. We tried the real app with SIZE at 24/24 MB and at a minimal 6 MB: both refused identically, which also killed the sub-theory that this was our old friend the memory-partition bug (an earlier crash in this port really *was* the SIZE resource — see below — which is exactly why we suspected it again).

Size and metadata: exonerated.

## Getting eyes inside a silent launch

You can't debug what produces no output. The breakthrough in *observability* — not yet in understanding — was a constructor with priority 101, the earliest user priority GCC allows, compiled into the app:

```c
/* r1main.c — runs before essentially everything else */
__attribute__((constructor(101))) static void r1_premain_probe(void)
{
    FILE *f = fopen("premain.txt", "a");
    if (f) { fputs("premain: C runtime alive\n", f); fclose(f); }
}
```

Under Classic, `premain.txt` appeared. **The app was starting.** The C runtime came up, the file system worked — and then, somewhere in the static-constructor chain, before `main`, before the first log line, it died.

So we bracketed the constructor chain with marker translation units. Retro68's collect2 runs constructors in reverse link order, and our link order is an alphabetical glob over `obj/*.o` — so a file's *name* decides when its constructor runs. A marker is ten lines:

```cpp
/* abz_mark.cpp — ctor-chain bisection marker; logs heap state too */
#include <cstdio>
extern "C" long b2_maxblock(void);
extern "C" long b2_freemem(void);
__attribute__((constructor)) static void abz_mark_ctor(void)
{
    FILE *f = fopen("premain.txt", "a");
    if (f) { fprintf(f, "abz mb=%ld fm=%ld\n", b2_maxblock(), b2_freemem()); fclose(f); }
}
```

Fourteen of these, named to interleave through the glob (`aa_mark`, `ac_mark`, `bz_mark`, … `zz_marker`), turned the silent death into a bisection: after each launch attempt, `premain.txt` shows exactly which bracket the app died in.

## Theory 2: heap exhaustion during construction (RIP)

This port had already survived one layout-sensitive launch crash — the "rung 2" bug, where the Memory Manager's next-block write fell off the last mapped page because the SIZE partition was too small. Static construction allocates; maybe Classic's partition geometry differs from QEMU's just enough that construction runs out of heap here and not there.

That's why the markers log `MaxBlock()` and `FreeMem()`. The verdict from `premain.txt`:

```
premain: C runtime alive
...
abz mb=31473664 fm=31894528
<death — the next marker never prints>
```

<!-- TODO: reconstructed from the marker source and the commit message
     ("markers now log MaxBlock/FreeMem: 30MB free at death"); paste the real
     premain.txt capture from the pixalito session if it still exists. -->

Thirty megabytes free, and the largest allocatable block was essentially all of it. Whatever was killing the app, it was not asking the heap for anything it couldn't have.

Heap: exonerated. And the death bracket had a name — the marker output stopped right at `economy.o`'s translation-unit constructor (`_GLOBAL__I_65535_0__price_base_specs`, the initialiser GCC emits for economy.cpp's file-scope statics).

But here the evidence turned strange. Adding *one more tiny marker TU* — no code changes, just another ten-line file in the glob — **moved the death earlier in the chain.** And renaming `economy.o` to `ab_econ.o` (which relocates its constructor to a different position in the reverse-alphabetical order) made the death *follow it to its new position*. The crash tracked layout, not content.

## Theory 3: the CFM relocation cliff (RIP, with honours)

Layout-sensitive, position-in-stream-dependent, loader-adjacent — that pattern fit a genuinely seductive theory. A PEF binary's data section is patched at load time by CFM, driven by a bytecode relocation stream. Count the relocation instructions in our builds and you get a chilling bracket: the last good build had 72,952 relocation instructions; R1-140, the first bad one, had 75,526. And current builds were at 176,209 — MakePEF was emitting a naive `SetPosition` pair for every single one of the binary's 58,690 pointers, about 3.4 chunks per relocation.

Hypothesis: Classic's CFM silently stops applying relocations somewhere around 73–75k instructions. Data past the cliff keeps garbage in its TOC pointers, and the first static constructor to touch an unrelocated slot dies. It explains the position sensitivity perfectly: whichever constructor's data happens to sit past the cliff is the one that dies, and any layout change reshuffles the victim.

The theory earned a real fix. We taught MakePEF to sort relocations by address and emit the compact PEF encodings the format has offered since 1994 — run-length `RelocBySectDWithSkip` (DDAT) chunks, `TVector8` runs for function descriptors, `VTable8`, single-chunk `IncrPosition` gaps:

```cpp
/* MakePEF.cc — each relocation independently adds a base address to one
 * word, so application order is free. Collect, sort by address, and emit
 * the compact PEF encodings (runs, transition vectors, DDAT, 1-chunk
 * position increments) instead of a SetPosition pair per pointer. */
std::stable_sort(elems.begin(), elems.end(),
    [](const Elem& a, const Elem& b) { return a.vaddr < b.vaddr; });
```

176,209 chunks became 59,098 — a 3× reduction, comfortably below the alleged cliff. We wrote a scratch verifier that decoded both the old stream and the new one and instantiated both section images: byte-exact identical applied-relocation sets. This was not a hack; it is simply what a correct PEF producer should emit.

We deployed it.

**It died exactly the same way.**

The relocation-count cliff was a correlate, not a cause. The chunk count grew with the binary; the binary grew with the bug. The compression stays in Retro68 (commit `e47a4c23cc`) because it's correct practice — but as a cure it was a beautifully engineered fix for the wrong disease.

## The probe that cracked it

At this point the surviving facts were: economy.o's constructor is where death occurs; the death is positional, not content-driven; the heap is fine; the relocations are (now provably) fine. Time to stop theorising about the loader and *watch the dying constructor's actual inputs*.

Enter `zzz_probe.cpp`. Named to sort last in the glob, so its constructor runs **first** — before any game constructor. It does three things:

```cpp
extern "C" void r1_econ_init(void)
    __asm__("_GLOBAL__I_65535_0__price_base_specs");

__attribute__((constructor)) static void zzz_probe_ctor(void)
{
    unsigned long toc;
    __asm__("mr %0,2" : "=r"(toc));

    /* A function pointer's value is its transition vector;
     * word 0 is the code address (word 1 the callee TOC). */
    void (*fp)(void) = r1_econ_init;
    const unsigned long *tv   = (const unsigned long *)fp;
    const unsigned long *code = (const unsigned long *)tv[0];

    /* Scan the init's first 40 instructions for TOC loads
     * (lwz rX, d(r2)) and dump each referenced slot — reads only. */
    for (int i = 0; i < 40; i++) {
        unsigned long insn = code[i];
        if ((insn & 0xFC000000UL) == 0x80000000UL
            && ((insn >> 16) & 0x1F) == 2) {
            int d = (int)(short)(insn & 0xFFFF);
            unsigned long slotval = *(const unsigned long *)(toc + d);
            fprintf(f, "zzz probe: toc[%+d] = 0x%lx\n", d, slotval);
        }
        if (insn == 0x4E800020UL) break; /* blr */
    }
    fclose(f);          /* flush BEFORE the call in case it kills the app */

    r1_econ_init();     /* call the dying constructor FIRST in the chain */
}
```

First, it locates the dying constructor's actual machine code through its transition vector and disassembles just enough of it — PowerPC `lwz rX, d(r2)` instructions, loads relative to the TOC pointer in r2 — to find every TOC slot the constructor will read. It dumps each slot's raw value to `premain.txt`. Reads only: safe even if the slots are garbage.

Second — and this is the crux — it *calls the economy constructor itself*, first in the chain, ninety-odd positions before its natural slot.

The results, both decisive:

1. **Every TOC slot held a correctly relocated pointer.** The loader had done its job. The relocation theory's corpse was now formally identified.
2. **`zzz probe: econ init SURVIVED early call`** — and then the app *still died later in the chain*, at the same constructor's natural (now second, guarded-idempotent) position's neighbourhood.

The identical code, with identical, correctly-relocated inputs, succeeds at position 1 and kills the process at position ~90. The only thing that differs between those two moments is *what the other constructors have done in between*. Which means economy's constructor wasn't the murderer at all. It was writing to memory that, ninety constructors later, **belonged to someone else** — and the someone else was the one who died.

That is not a loader bug. That is two variables at one address.

## The proof, at the linker level

Now we knew what to look for, and the linked XCOFF image confessed in one `objdump -t` session. OpenTTD's object-pool registry is a classic Meyers singleton, defined in a header:

```cpp
struct PoolBase {
    static PoolVector *GetPools()
    {
        static PoolVector *pools = new PoolVector();
        return pools;
    }
    ...
};
```

Because it's defined in-class, `GetPools` is an inline — *weak* — function: every TU that uses it emits its own copy of the function, its local static `pools`, and the guard variable, and the linker is supposed to pick one of each and discard the rest. GNU binutils' XCOFF backend discards the duplicates all right — and **loses the surviving allocation with them**. The kept references to `_ZZN8PoolBase8GetPoolsEvE5pools` don't dangle or error; they resolve, silently, into whatever *neighbouring csect* occupies that address.

The TOC dump showed exactly that. The TOC entry for `PoolBase::GetPools()::pools` and the TOC entry for economy.o's `CMD_ERROR` both pointed at **`0xfe598`**:

```
$ powerpc-apple-macos-objdump -t ottdr1.xcoff | grep -A1 'GetPools\|CMD_ERROR'
[...](sec  2)(scl 107) (nx 1) 0x000fe598 _ZZN8PoolBase8GetPoolsEvE5pools   <- TOC entry
[...](sec  2)(scl 107) (nx 1) 0x000fe598 _ZL9CMD_ERROR                     <- TOC entry
```

<!-- TODO: reconstructed excerpt in the real objdump -t output format; the
     address 0xfe598 is the measured value (memory note + commit 4173994).
     To reproduce the literal lines, rebuild the pre-fix tree (the .xcoff
     itself was never committed) and paste the actual dump. -->

`CMD_ERROR` is a header-defined static (`static const CommandCost CMD_ERROR = CommandCost(INVALID_STRING_ID);` in `command_func.h`), so economy.cpp owns a copy that its TU constructor dynamically initialises. Follow the collision through the constructor chain and the whole fifteen days of symptoms falls out:

- Early pool constructors run, call `GetPools()`, allocate the registry vector, store the pointer at `0xfe598`, and set the function-local-static guard: *initialised*.
- Ninety constructors later, economy.o's TU init runs and initialises **its own variable** — `CMD_ERROR` — at the *same address*, clobbering the live registry pointer. The overlapping bytes left the pointer's first byte `0xFF`.
- The next pool constructor calls `GetPools()`. The guard says *already initialised*, so it loyally returns the clobbered pointer, and `push_back` dereferences `0xFFxxxxxx`.

And now the position-dependence is obvious rather than spooky. When `zzz_probe` called economy's init *first*, the guard hadn't been set yet — so the very next `GetPools()` simply re-ran the initialiser, allocated a fresh vector, and healed the clobber before anyone dereferenced it. The same write, ninety constructors later, was fatal. Content-innocent, position-guilty. Every earlier theory had been a shadow of this: the crash *was* layout-sensitive, it *did* track link order, it *did* look like the loader had corrupted a pointer.

It also explains the cruellest part — why QEMU never saw it. Under Classic, `0xFFxxxxxx` is unmapped: the dereference kills the process before a single log line, and can take `TruBlueEnvironment` down with it. Under our QEMU guests, the same address reads as garbage without faulting, and the game — by pure luck — survived what it found there and went on to pass a 12/12 harness run. The bug was never "Classic-only". Classic was simply the only environment honest enough to crash.

We audited the rest of the image the same way: **ten** such overlaps. The best of the supporting cast: our R1 window tick counters — function-local statics in inline window callbacks — had been allocated *on top of their own `WindowDesc` structures*, quietly corrupting the window system's own metadata every tick. A long tail of "window flakiness" we'd half-attributed to our own shims turned out to be the same linker bug wearing different clothes.

## The cure

Three parts (commits `4173994` in the port repo, `659166b` in the openttd-13.4 tree, `e47a4c23cc` in Retro68).

**1. Ban the pattern.** No mutable function-local statics in weak functions. Every offender gets hoisted to file scope or to an out-of-line strong definition. The pool registry now lives in exactly one TU:

```cpp
/* pool_func.cpp — out-of-line for the Mac OS 9 port */
/* static */ PoolVector *PoolBase::GetPools()
{
    static PoolVector *pools = new PoolVector();
    return pools;
}
```

with the header reduced to a declaration. Same treatment for `DriverFactoryBase::GetDriverTypeName`, `SmallStack::GetPool` (declared-only in the header, the single used instantiation defined explicitly in one TU), and a handful of statics in our own port code.

**2. Trust, but verify — mechanically, forever.** A rule you enforce by code review is a rule you will break in a header you didn't write. So the build now carries a post-link gate, `ottd-r1/tools/xcoff_weak_static_audit.py`, wired as a CMake `POST_BUILD` step on the game target. Its approach: mangled names make function-local statics *auditable* — every one is `_ZZ...`, every guard is `_ZGV...`. The gate runs `objdump -t` over the linked XCOFF, collects every sized csect in data/bss space, then reads the actual pointer out of every `_ZZ*`/`_ZGV*` TOC entry in the binary and demands that it land **in a csect of its own name**:

```python
f.seek(data_off + addr)
tgt = struct.unpack('>I', f.read(4))[0]
if name in labels.get(tgt, ()):   # resolved to a symbol of its own name
    continue
c = containing(tgt)
if c and c[2] == name:
    continue
...
bad.append('  %s -> %s' % (name, where))
```

Per-TU `.rw_`/`.ro_`/`.bs_` section blobs are allowed (with `-Os` a TU's small locals legitimately merge into one csect — that's the TU's own storage); read-only statics merged past the end of bss into text blobs are benign. Everything else — a named static resolving *inside somebody else's variable* — fails the build:

```
xcoff weak-static audit FAILED: 1 function-local statics lost their allocation:
  _ZZN8PoolBase8GetPoolsEvE5pools -> inside _ZL9CMD_ERROR @0xfe598+16
Fix: hoist each static to file scope / an out-of-line strong definition.
```

On today's fixed build it reports (real output):

```
xcoff weak-static audit: 70 TOC entries checked, all allocated. OK
```

The docstring is explicit about the social contract: the fix is to hoist the static, *not to silence the gate*.

**3. Keep the wrong fix.** The MakePEF relocation compression stays — 176,209 → 59,098 chunks, byte-exact verified — because a 3× smaller, spec-idiomatic relocation stream is simply the correct thing to emit, whatever it failed to cure.

The night ended with R1-187 running the full game in Classic on the iMac G4: window, five towns growing, buses driving, finances balancing, ~1 ms/frame. The first working post-R1-140 build on the lamp.

## Epilogue: the gate earns its keep in 24 hours

The next day we linked OpenTTD's real train stack — `train_cmd.cpp`, consist management, level crossings — a big link-layout shift. The audit gate went red on the very first build:

```
HandleMouseEvents()::double_click_time lost its allocation
```

`HandleMouseEvents` is not weak. It's a plain strong function in window.cpp. The linker lost a strong function's local static on a layout change — which means the bug is *broader* than the weak-function dedup path we'd root-caused, and the "ban statics in weak functions" rule alone would not have saved us. The gate did, at build time, with a symbol name — instead of three weeks later as a mystery double-click regression on hardware. That one fix was a two-line hoist. The heroic version of that hunt would have been this entire article again.

## Lessons

**1. A deterministic crash that moves when the layout moves is a layout bug.** When the same code with proven-clean inputs dies at position 90 but survives at position 1, stop reading the code and start reading the image. Content-innocent, position-dependent failures point at the toolchain and loader — the parts of the stack we're all trained to suspect last.

**2. Emulators can mask memory bugs by having different unmapped-address geometry.** QEMU didn't hide this bug by being lenient in any configured way; its guest simply happened to have readable bytes where Classic had a fault. "Passes in the emulator, dies on hardware" doesn't mean the hardware is flaky — it may mean the emulator's memory map is accidentally forgiving. If your CI is an emulator, a wild pointer can be green for fifteen days.

**3. If your linker is exotic, audit its output mechanically.** The contract a linker implements — every symbol gets exactly one allocation, every reference resolves to it — is so foundational that nobody checks it. On a mainstream target that trust is earned by millions of users. On XCOFF-for-classic-Mac it is not. The audit gate is ~100 lines of Python over `objdump -t` output, and it converts an entire class of silent memory corruption into a red build with a symbol name.

**4. A permanent gate beats a heroic hunt.** The hunt was one all-nighter of probe apps, marker TUs, instruction-scanning constructors and TOC dumps. The gate is one `POST_BUILD` line. The gate caught the bug's second coming — in a *strong* function, outside the pattern we understood — before it ever reached hardware. Encode the postcondition you proved, not the mechanism you think you found.

## What remains broken upstream

The bug is in GNU binutils' XCOFF backend (`bfd/xcofflink.c` territory) and, as far as we know, remains unfixed upstream; our fixes are workarounds in the program plus a gate over the output. It has company: in the same toolchain we've measured `ld` segfaulting outright when handed one particular innocent object file (`core/math_func.cpp`'s `.o` crashes the link when included on its own — we copy the one function we need into another TU instead), and duplicate strong definitions produce a segfault rather than a diagnostic. <!-- TODO: the brief also mentions an `ld -r` assertion failure in the same code path; I could not verify that from the repo/memory sources — confirm or drop. --> The XCOFF path is an old, sparsely exercised corner of binutils — AIX keeps it alive, but nobody ships a desktop OS on it any more — and it shows. If you're one of the few dozen people on Earth linking XCOFF for classic Macs: don't trust it. Dump it, decode it, gate it.

We'd like to distil the reproducer and report it upstream properly; a minimal case is straightforward now that the mechanism is known — two TUs, one header-inline function with a local static, one neighbouring initialised global, and an `objdump -t | grep` is the whole testcase. <!-- TODO: actually build and attach the minimal reproducer before claiming it reproduces outside this project. -->

---

*The game, meanwhile, runs on the lamp.*
