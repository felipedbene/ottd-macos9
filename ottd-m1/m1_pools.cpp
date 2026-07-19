// M1: superseded at build 6. The real town_cmd.cpp is now compiled, so it
// provides _town_pool, INSTANTIATE_POOL_METHODS(Town), Town::~Town and
// Town::PostDestructor for real. This TU is intentionally empty (kept in the
// build to avoid churning CMakeLists/build.sh object lists).
