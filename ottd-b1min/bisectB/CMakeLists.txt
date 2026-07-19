cmake_minimum_required(VERSION 3.10)
project(bisectB CXX)
add_application(bisectB winmain_B.cpp maclog.cpp)
target_link_options(bisectB PRIVATE "LINKER:--gc-sections")
target_link_libraries(bisectB
    ${CMAKE_CURRENT_SOURCE_DIR}/../stub_fio.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../gfx.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../b1_render.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../b1_shims.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../grf.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../sprite_file.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../random_access_file.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../tile_map.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../map.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../bitmath_func.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../8bpp_simple.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../8bpp_base.o)
