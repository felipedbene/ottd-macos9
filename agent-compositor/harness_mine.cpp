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

#include <cstdio>
#include "landscape_render.h"
int main(){
    const int N=9; unsigned char h[N*N];
    for(int k=0;k<N*N;k++) h[k]=2;   // ALL FLAT (constant height)
    iso::DrawTile t[(N-1)*(N-1)];
    int c=iso::BuildScene(h,N,t,false);
    printf("# %d tiles\n",c);
    for(int i=0;i<c;i++) printf("%d %d %d %d %d %d %d\n",t[i].map_x,t[i].map_y,t[i].slope,t[i].base_h,t[i].sprite,t[i].screen_x,t[i].screen_y);
    return 0;
}
