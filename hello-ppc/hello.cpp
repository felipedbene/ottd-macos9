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

#include <string>
#include <vector>
int main() {
    std::vector<std::string> v{"OpenTTD", "on", "Mac", "OS", "9"};
    std::string s;
    for (auto& w : v) { s += w; s += ' '; }
    return (int)s.size() & 0;   // exercises libstdc++ string/vector on PPC
}
