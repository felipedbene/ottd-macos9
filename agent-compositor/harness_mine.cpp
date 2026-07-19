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
