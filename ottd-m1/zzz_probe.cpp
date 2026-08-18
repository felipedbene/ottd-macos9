/*
 * This file is part of ottd-macos9 — a port of OpenTTD to Mac OS 9 / PowerPC.
 * Copyright (c) 2026 Felipe De Bene.
 * GPL v2 — see LICENSE/NOTICE in the repository root.
 */

/* zzz_probe.cpp (R1-186 Classic forensics) — sorts LAST in the obj glob, so
 * its ctor runs FIRST (cdtor order is reverse link order), before any game
 * ctor. Economy.o's TU init (_GLOBAL__I_65535_0__price_base_specs) is where
 * Classic silently dies with 30MB of heap free, so this probe:
 *   1. locates that init's CODE through its transition vector,
 *   2. parses its first instructions for `lwz rX, d(r2)` TOC loads and dumps
 *      each slot's raw value to premain.txt (reads only — safe even if the
 *      slots are unrelocated garbage),
 *   3. then CALLS the init itself. If economy's init is content-guilty it now
 *      dies first in the chain; if it survives here but the chain still dies
 *      later, the kill is positional (whatever runs 90 ctors deep).
 * Remove after the hunt. */
#include <cstdio>

extern "C" void r1_econ_init(void) __asm__("_GLOBAL__I_65535_0__price_base_specs");

__attribute__((constructor)) static void zzz_probe_ctor(void)
{
    unsigned long toc;
    __asm__("mr %0,2" : "=r"(toc));

    /* A function pointer's value is its transition vector; word 0 is the code
     * address (word 1 the callee TOC). */
    void (*fp)(void) = r1_econ_init;
    const unsigned long *tv = (const unsigned long *)fp;
    const unsigned long *code = (const unsigned long *)tv[0];

    FILE *f = fopen("premain.txt", "a");
    if (f == NULL) return;
    fprintf(f, "zzz probe: toc=0x%lx econ tv=0x%lx code=0x%lx tv_toc=0x%lx\n",
            toc, (unsigned long)tv, (unsigned long)code, tv[1]);

    /* Scan the init's first 40 instructions for TOC loads (lwz rX, d(r2):
     * major opcode 32, RA=2) and dump each referenced slot. */
    int seen[16];
    int nseen = 0;
    for (int i = 0; i < 40; i++) {
        unsigned long insn = code[i];
        if ((insn & 0xFC000000UL) == 0x80000000UL && ((insn >> 16) & 0x1F) == 2) {
            int d = (int)(short)(insn & 0xFFFF);
            int dup = 0;
            for (int k = 0; k < nseen; k++) if (seen[k] == d) dup = 1;
            if (dup || nseen >= 16) continue;
            seen[nseen++] = d;
            unsigned long slotval = *(const unsigned long *)(toc + d);
            fprintf(f, "zzz probe: toc[%+d] = 0x%lx\n", d, slotval);
        }
        if (insn == 0x4E800020UL) break; /* blr */
    }
    fputs("zzz probe: dump done, calling econ init...\n", f);
    fclose(f); /* flush BEFORE the call in case it kills the app */

    r1_econ_init();

    f = fopen("premain.txt", "a");
    if (f != NULL) { fputs("zzz probe: econ init SURVIVED early call\n", f); fclose(f); }
}
