cmake_minimum_required(VERSION 3.10)
project(ottdb2 CXX C)

set(OT_LIBS
    ${CMAKE_CURRENT_SOURCE_DIR}/netlog.o
    ${CMAKE_CURRENT_SOURCE_DIR}/statsd_ot.o
    ${CMAKE_CURRENT_SOURCE_DIR}/OpenTransportAppPPC.o
    ${CMAKE_CURRENT_SOURCE_DIR}/OpenTptInetPPC.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../Retro68-build/toolchain/powerpc-apple-macos/lib/libOpenTransportLib.a
    ${CMAKE_CURRENT_SOURCE_DIR}/../Retro68-build/toolchain/powerpc-apple-macos/lib/libOpenTptInternetLib.a)

# B2.0 clock probe
add_application(ottdb2probe b2probe.cpp)
target_link_options(ottdb2probe PRIVATE "LINKER:--gc-sections")
target_link_libraries(ottdb2probe ${OT_LIBS})

# B2.1 VideoDriver_MacClassic demo: Mac TUs built here (multiversal headers);
# game TUs prebuilt raw (see docs/B2-VIDEO-DRIVER.md); B1's game objects reused.
add_application(ottdb2 b2main.c macclassic_sys.c macdir.c)
# two-pass bss-scanner wiring: pass VMAs from the pass-1 binary at configure time
if(B2_ANCHOR_VMA)
    target_compile_definitions(ottdb2 PRIVATE
        B2_ANCHOR_VMA=${B2_ANCHOR_VMA}
        B2_BSS_START_VMA=${B2_BSS_START_VMA}
        B2_BSS_END_VMA=${B2_BSS_END_VMA})
endif()
# both direct sources are C, but the prebuilt game objects need libstdc++/libsupc++
set_target_properties(ottdb2 PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(ottdb2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_options(ottdb2 PRIVATE "LINKER:--gc-sections")
target_link_libraries(ottdb2
    ${CMAKE_CURRENT_SOURCE_DIR}/macclassic_v.o
    ${CMAKE_CURRENT_SOURCE_DIR}/b2_scene.o
    ${CMAKE_CURRENT_SOURCE_DIR}/b2_shims.o
    ${CMAKE_CURRENT_SOURCE_DIR}/mac_sockets_ot.o
    ${CMAKE_CURRENT_SOURCE_DIR}/macnet_test.o
    ${CMAKE_CURRENT_SOURCE_DIR}/video_driver.o
    ${CMAKE_CURRENT_SOURCE_DIR}/driver.o
    ${CMAKE_CURRENT_SOURCE_DIR}/random_func.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../ottd-b1/b1_shims.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../ottd-b1/spritecache.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../ottd-b1/gfx.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../ottd-b1/grf.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../ottd-b1/sprite_file.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../ottd-b1/random_access_file.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../ottd-b1/tile_map.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../ottd-b1/map.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../ottd-b1/bitmath_func.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../ottd-b1/8bpp_simple.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../ottd-b1/8bpp_base.o
    ${OT_LIBS})
