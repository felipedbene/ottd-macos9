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
#include <string>
#include <vector>
#include <cstdio>

namespace retro { void InitConsole(); }

int main()
{
    retro::InitConsole();

    // Exercise the C++17 stdlib on PowerPC classic Mac OS
    std::vector<std::string> words{"OpenTTD", "on", "Mac", "OS", "9"};
    std::string line;
    for (auto& w : words) { line += w; line += ' '; }

    printf("\033]0;OpenTTD PPC smoke test\007");   // window title
    printf("Hello from a PowerPC PEF built on arm64!\n\n");
    printf("std::vector<std::string> joined -> \"%s\"\n", line.c_str());
    printf("chars: %d   words: %d\n\n", (int)line.size(), (int)words.size());
    printf("If you can read this, the toolchain + libstdc++ work.\n");
    printf("Type 'exit' and press Return to quit.\n");

    std::string in;
    do { in = retro::Console::currentInstance->ReadLine(); } while (in != "exit\n");
    return 0;
}
