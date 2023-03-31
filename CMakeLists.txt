cmake_minimum_required(VERSION 3.14)
project(ingestinator C CXX)
set(CMAKE_CXX_STANDARD 14)

add_subdirectory(lib/FFmpeg)

add_executable(ingestinator MACOSX_BUNDLE WIN32 src/ingestinator.c src/ingestinator.h src/ingestinatorFunctions.c src/ingestinatorFunctions.h)

target_link_libraries(ingestinator FFmpeg)