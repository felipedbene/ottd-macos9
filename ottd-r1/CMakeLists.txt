cmake_minimum_required(VERSION 3.10)
project(ottdr1 CXX C)

# R1 render-merge: M1's real engine (obj/*.o, built by build.sh with -DR1_MERGE
# -DNDEBUG) + B2/b1's sprite/render pipeline + b1/b2 shims for the render-side
# stubs. Mac-side TUs (r1main.c etc.) compiled here with the multiversal headers.
set(M1  ${CMAKE_CURRENT_SOURCE_DIR}/../ottd-m1)
set(B1  ${CMAKE_CURRENT_SOURCE_DIR}/../ottd-b1)
set(B2  ${CMAKE_CURRENT_SOURCE_DIR}/../ottd-b2)
set(TCLIB ${CMAKE_CURRENT_SOURCE_DIR}/../Retro68-build/toolchain/powerpc-apple-macos/lib)

set(OT_LIBS
    ${B2}/netlog.o ${B2}/statsd_ot.o ${B2}/OpenTransportAppPPC.o ${B2}/OpenTptInetPPC.o
    ${TCLIB}/libOpenTransportLib.a ${TCLIB}/libOpenTptInternetLib.a)

# Engine + M1-support objects built by build.sh with the merge flags.
file(GLOB R1_ENGINE_OBJS ${CMAKE_CURRENT_SOURCE_DIR}/obj/*.o)

add_application(ottdr1 r1main.c macclassic_sys.c macdir.c)
set_target_properties(ottdr1 PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(ottdr1 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${B2})
target_link_options(ottdr1 PRIVATE "LINKER:--gc-sections")
target_link_libraries(ottdr1
    # Mac-side render/video driver + scene (reused prebuilt from B2).
    # b2_scene.o replaced by obj/r1_scene.o (from the R1_ENGINE_OBJS glob): draws
    # the real engine-built map instead of B2's synthetic H[] heightmap.
    ${B2}/macclassic_v.o ${B2}/video_driver.o ${B2}/driver.o
    ${B2}/mac_sockets_ot.o ${B2}/macnet_test.o ${B2}/random_func.o
    # Sprite/GRF/blitter pipeline + shared map (reused from B1).
    ${B1}/gfx.o ${B1}/spritecache.o ${B1}/grf.o ${B1}/sprite_file.o ${B1}/random_access_file.o
    ${B1}/8bpp_simple.o ${B1}/8bpp_base.o ${B1}/tile_map.o ${B1}/map.o ${B1}/bitmath_func.o
    # Render-side stubs (window/error/layouter/Fio...). Game-state globals are
    # COMMON (B) here and merge with gfx.o/m1_shims — no conflict.
    # b2_shims.o AND b1_shims.o are now built R1-locally (obj/*.o glob) with
    # -DR1_MERGE (b2: live-tick hook; b1: drop Layouter stubs for real text). Both
    # B-prebuilts dropped here.
    # M1 real engine + engine-calibrated support objects.
    ${R1_ENGINE_OBJS}
    ${OT_LIBS})
