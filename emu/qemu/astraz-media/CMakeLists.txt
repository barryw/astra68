cmake_minimum_required(VERSION 3.21)
project(AstraZMediaProbe C)

find_package(Astra CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_mixer CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)

get_filename_component(ASTRA_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(ASTRAZ_SOURCE "${ASTRA_PROJECT_ROOT}/sw/userspace/ports/astraz")
add_subdirectory("${ASTRAZ_SOURCE}" "${CMAKE_CURRENT_BINARY_DIR}/astraz"
                 EXCLUDE_FROM_ALL)

add_executable(astraz_media_probe probe.c "${ASTRAZ_SOURCE}/src/audio.c"
               "${ASTRAZ_SOURCE}/src/picture.c")
target_include_directories(astraz_media_probe PRIVATE "${ASTRAZ_SOURCE}/src")
target_compile_options(astraz_media_probe PRIVATE -Wall -Wextra -Werror -Wpedantic)
target_link_libraries(astraz_media_probe PRIVATE astraz_engine Astra::Native
                      Astra::graphics Astra::system SDL2::SDL2 SDL2_mixer::SDL2_mixer
                      SDL2_image::SDL2_image)
astra_posix_program(astraz_media_probe NAME "AstraZ Media Probe"
                    VERSION 0.1.0 AUTHOR "Astra 68 Project"
                    COPYRIGHT "Copyright 2026 Astra 68 Project")
