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

#include "retro/Console.h"
#include <cstdio>
namespace retro { void InitConsole(); }
extern "C" void ottd_math_report(char *buf, int buflen);
int main()
{
    retro::InitConsole();
    printf("\033]0;OpenTTD math engine on Mac OS 9\007");
    printf("OpenTTD 13.4's REAL functions (src/core/math_func.cpp)\n");
    printf("running on PowerPC / Mac OS 9:\n\n");
    char buf[1024]; ottd_math_report(buf, sizeof buf);
    fputs(buf, stdout);
    printf("\nEvery number above was computed by OpenTTD's own code, unmodified.\n");
    printf("Type 'exit' + Return to quit.\n");
    std::string in;
    do { in = retro::Console::currentInstance->ReadLine(); } while (in != "exit\n");
    return 0;
}
