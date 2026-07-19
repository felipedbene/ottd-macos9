// BISECT-A no-gfx: identical link/static-init graph as ottd-b1, but main() only logs+returns.
// Purpose: decide ctor-vs-runtime. If ottd-bA.txt appears on the share, ALL global
// constructors ran fine and the Type-2 crash lives in b1_render's runtime path. If NO
// trace file appears, the crash is a pre-main global constructor (gfx.o/spritecache.o).
#include <MacMemory.h>

extern "C" int  b1_render(unsigned char *fb, int pitch, int w, int h);
extern "C" void ottd_log_init(const char *path);
extern "C" void ottd_log(const char *fmt, ...);
extern "C" void ottd_log_close(void);

// Force-keep the whole render graph so --gc-sections cannot drop spritecache.o/gfx.o
// and change which static initializers run. This is a constant initializer (no ctor).
void *volatile keep_b1 = (void *)&b1_render;

int main()
{
    ottd_log_init("ottd-bA.txt");
    ottd_log("=== BISECT-A no-gfx: main() reached -> ALL global ctors ran OK ===");
    ottd_log("free heap = %ld bytes", (long)FreeMem());
    ottd_log("keep_b1 = %p (graph retained, render NOT called)", keep_b1);
    ottd_log("=== BISECT-A no-gfx done ===");
    ottd_log_close();
    return 0;
}
