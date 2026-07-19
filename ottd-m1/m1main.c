/*
 * This file is part of ottd-macos9 — a port of OpenTTD to Mac OS 9 / PowerPC.
 * Copyright (c) 2026 Felipe De Bene.
 *
 * Derived from and/or built against OpenTTD, Copyright (c) the OpenTTD
 * Development Team. Modified for the Mac OS 9 / PowerPC port in 2026.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2, as published
 * by the Free Software Foundation. This program comes with NO WARRANTY. See
 * the LICENSE and NOTICE files in the repository root for the full terms.
 */

/* M1 Mac-side main: grow the app heap, init Toolbox, then hand off to m1_run().
 * Cloned from ottd-b2/b2main.c, stripped of the .bss scanner + two-pass VMA
 * machinery (demo-specific debugging). The one load-bearing lesson kept:
 * MaxApplZone() must run BOTH as the first line of main AND as a priority-101
 * constructor, so the heap grows before any C++ static ctor allocates. */
#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Events.h>
#include <MacMemory.h>
#include <OSUtils.h>

#define M1_BUILD_TAG "4"

void MaxApplZone(void);

/* Space out UDP log packets: M1 runs straight through with no event pump, so
 * back-to-back ottd_log()s burst the (lossy) OT UDP sink. ~50ms between the
 * milestone lines lets each datagram flush so a successful run can't be
 * mistaken for a crash by dropped packets. Game-side TUs call it via extern. */
void m1_settle(void)
{
	unsigned long t;
	Delay(3, &t); /* 3 ticks ~= 50ms */
}

__attribute__((constructor(101)))
static void m1_early_heap(void)
{
	MaxApplZone(); /* before every normal-priority static ctor allocates */
}

/* Exposed so game-side TUs can sample the heap without Mac headers. */
long m1_maxblock(void) { return (long)MaxBlock(); }

extern void ottd_log_init(const char *path);
extern void ottd_log(const char *fmt, ...);
extern void ottd_log_close(void);
extern int  m1_run(void);

int main(void)
{
	MaxApplZone(); /* FIRST: Retro68 never grows the app heap into the SIZE partition */
	ottd_log_init("ottd-m1.txt");
	ottd_log("=== M1 build %s: headless real game loop: main() reached ===", M1_BUILD_TAG);
	InitGraf(&qd.thePort); InitFonts(); InitWindows(); InitMenus(); InitCursor();
	ottd_log("M1: Toolbox inited; FreeMem=%ld MaxBlock=%ld", (long)FreeMem(), (long)MaxBlock());

	int rc = m1_run();

	ottd_log("=== M1 done rc=%d; ExitToShell (skip static dtors) ===", rc);
	ottd_log_close();
	ExitToShell();
	return 0;
}
