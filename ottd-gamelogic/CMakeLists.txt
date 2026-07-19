cmake_minimum_required(VERSION 3.10)
project(ottdmap CXX)
add_application(ottdmap winmain.cpp maclog.cpp)
target_link_libraries(ottdmap
    ${CMAKE_CURRENT_SOURCE_DIR}/maplogic.o
    ${CMAKE_CURRENT_SOURCE_DIR}/gl_shims.o
    ${CMAKE_CURRENT_SOURCE_DIR}/map.o
    ${CMAKE_CURRENT_SOURCE_DIR}/tile_map.o
    ${CMAKE_CURRENT_SOURCE_DIR}/bitmath.o)
