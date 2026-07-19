cmake_minimum_required(VERSION 3.10)
project(ottdland CXX)
add_application(ottdland winmain.cpp maclog.cpp)
target_link_libraries(ottdland
    ${CMAKE_CURRENT_SOURCE_DIR}/landscape_demo.o
    ${CMAKE_CURRENT_SOURCE_DIR}/landscape_shims.o
    ${CMAKE_CURRENT_SOURCE_DIR}/groundsprite.o
    ${CMAKE_CURRENT_SOURCE_DIR}/landscape_render.o
    ${CMAKE_CURRENT_SOURCE_DIR}/map.o
    ${CMAKE_CURRENT_SOURCE_DIR}/tile_map.o
    ${CMAKE_CURRENT_SOURCE_DIR}/bitmath.o
    ${CMAKE_CURRENT_SOURCE_DIR}/rax.o
    ${CMAKE_CURRENT_SOURCE_DIR}/sf.o
    ${CMAKE_CURRENT_SOURCE_DIR}/grf.o
    ${CMAKE_CURRENT_SOURCE_DIR}/8bpp_simple.o
    ${CMAKE_CURRENT_SOURCE_DIR}/8bpp_base.o)
