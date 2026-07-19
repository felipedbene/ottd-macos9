extern "C" void ottd_log_init(const char*); extern "C" void ottd_log(const char*,...); extern "C" void ottd_log_close(void);
struct Palette; extern Palette _cur_palette; void *volatile keep=(void*)&_cur_palette;
int main(){ ottd_log_init("x"); ottd_log("gfx ctors ran, keep=%p", keep); ottd_log_close(); return 0; }
