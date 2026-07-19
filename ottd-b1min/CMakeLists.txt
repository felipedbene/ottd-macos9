cmake_minimum_required(VERSION 3.10)
project(ottdb1min CXX)
add_application(ottdb1min winmain_min.cpp maclog.cpp)
target_link_options(ottdb1min PRIVATE "LINKER:--gc-sections")
target_link_libraries(ottdb1min
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
    ${CMAKE_CURRENT_SOURCE_DIR}/8bpp_base.o)
