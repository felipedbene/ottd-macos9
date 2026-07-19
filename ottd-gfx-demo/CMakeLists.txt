cmake_minimum_required(VERSION 3.10)
project(ottdgfx CXX)
# Mac-side TUs: front-end + file-based trace bridge (maclog.cpp).
add_application(ottdgfx gfx.cpp maclog.cpp)
target_link_libraries(ottdgfx
    ${CMAKE_CURRENT_SOURCE_DIR}/ottd_render.o
    ${CMAKE_CURRENT_SOURCE_DIR}/grf_shims.o
    ${CMAKE_CURRENT_SOURCE_DIR}/random_access_file.o
    ${CMAKE_CURRENT_SOURCE_DIR}/sprite_file.o
    ${CMAKE_CURRENT_SOURCE_DIR}/grf.o
    ${CMAKE_CURRENT_SOURCE_DIR}/8bpp_simple.o
    ${CMAKE_CURRENT_SOURCE_DIR}/8bpp_base.o)
