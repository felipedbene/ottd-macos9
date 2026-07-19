cmake_minimum_required(VERSION 3.10)
project(bisectC CXX)
add_application(bisectC winmain_C.cpp maclog.cpp)
target_link_options(bisectC PRIVATE "LINKER:--gc-sections")
target_link_libraries(bisectC
    ${CMAKE_CURRENT_SOURCE_DIR}/../stub_fio_C.o
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
