cmake_minimum_required(VERSION 2.8.12)
project(draw-glyph)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_executable(draw-glyph main.c)
target_link_libraries(draw-glyph ${CONAN_LIBS})

if(MSVC)
	target_compile_options(draw-glyph PRIVATE /W4)
else()
	target_compile_options(draw-glyph PRIVATE -Wall -Wextra -std=c99 -pedantic)
endif()
