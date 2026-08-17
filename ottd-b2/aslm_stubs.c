/*
 * This file is part of ottd-macos9 — a port of OpenTTD to Mac OS 9 / PowerPC.
 * Copyright (c) 2026 Felipe De Bene.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2, as published
 * by the Free Software Foundation. This program comes with NO WARRANTY. See
 * the LICENSE and NOTICE files in the repository root for the full terms.
 */

/*
 * aslm_stubs.c — resolve the dead ASLM/CFront references inside Apple's
 * OpenTransportAppPPC.o (ClientUtilLinkedFiles.cp) so the link no longer
 * depends on XCOFF csect garbage collection to discard them.
 *
 * Until now the link succeeded ONLY because -bgc (on by default; our old
 * --gc-sections flag was a no-op) pruned the csects carrying these unresolved
 * refs. That same gc is the lead mechanism behind the layout-sensitive rung-2
 * Type 2 crash: one live csect wrongly pruned makes its symbol resolve into a
 * neighbouring csect, and the victim moves with layout. With every ref
 * defined here the link runs under -bnogc — nothing is pruned, so a wrong
 * prune is impossible by construction.
 *
 * These ASLM entry points are genuinely dead at runtime: under -bgc they were
 * discarded wholesale and the app ran for months. The stubs only need to
 * exist; each fails safe (error return / no-op) should anything ever reach
 * one.
 */

/* Apple Shared Library Manager client-utility surface. Exact prototypes are
 * irrelevant to the XCOFF linker (C, no mangling) and the PPC ABI passes the
 * ignored arguments in registers, so argument-less definitions are safe. */
long SetSelfAsClient(void) { return -1; }
long SetCurrentClient(void) { return -1; }
void *LoadClass(void) { return 0; }
void UnloadClass(void) {}
long InitLibraryManager(void) { return -1; }
void CleanupLibraryManager(void) {}
long FragGetSectionInfo(void) { return -1; }

/* CFront-mangled operator delete(void*). Nothing in the dead paths ever
 * allocated, so freeing nothing is faithful. */
void __dl__FPv(void) {}

/* Cross-TOC pointer-call glue: r12 holds a function descriptor {entry, toc}.
 * Implemented faithfully so even a live caller would work: load the entry,
 * swap the TOC, jump. bctr never returns, the epilogue is unreachable. */
void _ptrgl12(void)
{
    __asm__ volatile(
        "lwz 0,0(12)\n\t"
        "mtctr 0\n\t"
        "lwz 2,4(12)\n\t"
        "bctr");
}
