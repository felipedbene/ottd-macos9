/*
 * This file is part of ottd-macos9 — a port of OpenTTD to Mac OS 9 / PowerPC.
 * Copyright (c) 2026 Felipe De Bene.
 * GPL v2 — see LICENSE/NOTICE in the repository root.
 */

/* zz_marker.cpp (R1-179 Classic forensics) — named to sort LAST in the obj/
 * glob, so its default-priority ctor runs after every GAME ctor and immediately
 * before the libstdc++ locale/ctype static inits (the final entries in
 * ottdr1.xcoff.cdtor.c). premain.txt gains this line => every game ctor
 * survived and the Classic silent-swallow lives in the libstdc++ tail.
 * Remove after the hunt. */
#include <cstdio>

__attribute__((constructor)) static void zz_game_ctors_done(void)
{
    FILE *f = fopen("premain.txt", "a");
    if (f != nullptr) { fputs("game ctors done\n", f); fclose(f); }
}
