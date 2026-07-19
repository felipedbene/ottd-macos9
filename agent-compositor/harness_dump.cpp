/*
 * harness_dump.cpp -- HOST test driver for the compositor core.
 *
 * Builds a sample 8x8 corner-height heightmap (a hill + a valley), runs the
 * portable iso::BuildScene(), and prints the ordered draw list to stdout as
 * lines "map_x map_y slope base_h sprite screen_x screen_y". The Python
 * renderer (render_scene.py) consumes this and composites real OpenGFX
 * sprites into preview/scene.png.
 *
 * This driver uses NO Mac / OpenTTD deps -- it only links landscape_render.cpp.
 */
#include <cstdio>
#include "landscape_render.h"

int main()
{
	const int N = 9; /* 9x9 corner samples => 8x8 tiles */

	/*
	 * Corner-height grid (z-levels). A rounded hill toward the centre and a
	 * carved valley in one corner, so the scene shows flat, single-corner,
	 * two-corner, three-corner and steep slopes plus draw-order overlap.
	 */
	unsigned char base[N][N] = {
		{0,0,0,0,0,0,0,0,0},
		{0,1,1,1,1,1,0,0,0},
		{0,1,2,2,2,1,0,0,0},
		{0,1,2,3,2,1,0,0,0},
		{0,1,2,2,2,1,0,0,0},
		{0,1,1,1,1,1,0,0,0},
		{0,0,0,0,0,1,1,1,0},
		{0,0,0,0,0,1,2,1,0},
		{0,0,0,0,0,0,0,0,0}
	};

	/* flatten */
	unsigned char h[N * N];
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++)
			h[y * N + x] = base[y][x];

	iso::DrawTile tiles[(N - 1) * (N - 1)];
	int count = iso::BuildScene(h, N, tiles, /*use_atlas=*/false);

	std::printf("# heightmap %dx%d corners, %d tiles, back-to-front\n", N, N, count);
	std::printf("# fields: map_x map_y slope base_h sprite screen_x screen_y\n");
	for (int i = 0; i < count; i++) {
		iso::DrawTile &t = tiles[i];
		std::printf("%d %d %d %d %d %d %d\n",
			t.map_x, t.map_y, t.slope, t.base_h, t.sprite,
			t.screen_x, t.screen_y);
	}
	return 0;
}
