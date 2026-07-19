cmake_minimum_required(VERSION 3.10)
project(ottdb1 CXX)
# maclog.cpp (file sink) replaced by netlog.o (OpenTransport UDP sink, tees to file)
add_application(ottdb1 winmain.cpp)
target_link_options(ottdb1 PRIVATE "LINKER:--gc-sections")
target_link_libraries(ottdb1
    ${CMAKE_CURRENT_SOURCE_DIR}/b1_render.o
    ${CMAKE_CURRENT_SOURCE_DIR}/b1_shims.o
    ${CMAKE_CURRENT_SOURCE_DIR}/spritecache.o
    ${CMAKE_CURRENT_SOURCE_DIR}/gfx.o
    ${CMAKE_CURRENT_SOURCE_DIR}/grf.o
    ${CMAKE_CURRENT_SOURCE_DIR}/sprite_file.o
    ${CMAKE_CURRENT_SOURCE_DIR}/random_access_file.o
    ${CMAKE_CURRENT_SOURCE_DIR}/tile_map.o
    ${CMAKE_CURRENT_SOURCE_DIR}/map.o
    ${CMAKE_CURRENT_SOURCE_DIR}/bitmath_func.o
    ${CMAKE_CURRENT_SOURCE_DIR}/8bpp_simple.o
    ${CMAKE_CURRENT_SOURCE_DIR}/8bpp_base.o
    # --- OpenTransport UDP log sink ---
    ${CMAKE_CURRENT_SOURCE_DIR}/netlog.o
    ${CMAKE_CURRENT_SOURCE_DIR}/statsd_ot.o
    ${CMAKE_CURRENT_SOURCE_DIR}/OpenTransportAppPPC.o
    ${CMAKE_CURRENT_SOURCE_DIR}/OpenTptInetPPC.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../Retro68-build/toolchain/powerpc-apple-macos/lib/libOpenTransportLib.a
    ${CMAKE_CURRENT_SOURCE_DIR}/../Retro68-build/toolchain/powerpc-apple-macos/lib/libOpenTptInternetLib.a)
