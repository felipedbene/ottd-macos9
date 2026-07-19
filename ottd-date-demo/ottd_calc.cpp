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

#include "stdafx.h"
#include "core/math_func.hpp"
#include <cstdio>
#include <cstring>
extern "C" void ottd_math_report(char *buf, int buflen)
{
    buf[0] = 0; char line[128];
    int pairs[][2] = {{48,36},{1071,462},{120,45}};
    for (auto &p : pairs) {
        snprintf(line, sizeof line, "  gcd(%d,%d)=%d   lcm=%d\n",
                 p[0], p[1], GreatestCommonDivisor(p[0],p[1]), LeastCommonMultiple(p[0],p[1]));
        strncat(buf, line, buflen - strlen(buf) - 1);
    }
    unsigned vals[] = {2, 144, 1000000, 4294836225u};
    for (unsigned v : vals) {
        snprintf(line, sizeof line, "  IntSqrt(%u)=%u\n", v, (unsigned)IntSqrt(v));
        strncat(buf, line, buflen - strlen(buf) - 1);
    }
}
