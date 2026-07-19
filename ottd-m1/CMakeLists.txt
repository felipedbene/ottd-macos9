cmake_minimum_required(VERSION 3.10)
project(ottdm1 CXX C)

# M1: package the headless REAL OpenTTD game loop into a runnable Mac OS 9 app.
# Unlike ottd-b2 this needs NO two-pass .bss-scanner VMA bake -- m1main.c was
# stripped of that machinery. We just compile m1main.c and link the prebuilt
# game objects (see ottd-m1/build.sh, which links the same set into a bare
# .xcoff) through Retro68's add_application (MakePEF -> Rez -> .bin/.APPL).

set(B1 ${CMAKE_CURRENT_SOURCE_DIR}/../ottd-b1)
set(B2 ${CMAKE_CURRENT_SOURCE_DIR}/../ottd-b2)
set(OBJ ${CMAKE_CURRENT_SOURCE_DIR}/obj)
set(TCLIB ${CMAKE_CURRENT_SOURCE_DIR}/../Retro68-build/toolchain/powerpc-apple-macos/lib)

# OT UDP log sink (netlog/statsd) + Open Transport glue + the two OT stub libs.
set(OT_LIBS
    ${B2}/netlog.o
    ${B2}/statsd_ot.o
    ${B2}/OpenTransportAppPPC.o
    ${B2}/OpenTptInetPPC.o
    ${TCLIB}/libOpenTransportLib.a
    ${TCLIB}/libOpenTptInternetLib.a)

# add_application compiles m1main.c itself, so obj/m1main.o is intentionally
# NOT in the link list below.
add_application(ottdm1 m1main.c)
set_target_properties(ottdm1 PROPERTIES LINKER_LANGUAGE CXX)
target_link_options(ottdm1 PRIVATE "LINKER:--gc-sections")
target_link_libraries(ottdm1
    # M1's own TUs (headless run driver + shims/pools/methods).
    ${OBJ}/m1_run.o
    ${OBJ}/m1_shims.o
    ${OBJ}/m1_methods.o
    ${OBJ}/m1_pools.o
    ${OBJ}/m1_profiling_stub.o
    ${OBJ}/m1_town_stubs.o
    ${OBJ}/m1_cmd_stubs.o
    ${OBJ}/m1_land_stubs.o
    ${OBJ}/m1_road_stubs.o
    ${OBJ}/m1_deadpools.o
    # Real OpenTTD src TUs that actually run: the calendar, the pool allocator,
    # and the town command TU (build 6: real monthly/yearly town sim + Town pool;
    # build 7 also executes its real CmdTownGrowthRate proc via Command<>::Do).
    # Off-path cascades stubbed in m1_town_stubs.cpp / m1_cmd_stubs.cpp /
    # m1_deadpools.c.
    ${OBJ}/date.o
    ${OBJ}/pool_func.o
    ${OBJ}/town_cmd.o
    ${OBJ}/landscape.o
    ${OBJ}/clear_cmd.o
    ${OBJ}/road_cmd.o
    ${OBJ}/road_map.o
    # Prebuilt map/tile/bitmath/random reused from earlier milestones.
    ${B1}/map.o
    ${B1}/tile_map.o
    ${B1}/bitmath_func.o
    ${B2}/random_func.o
    ${OT_LIBS})
