cmake_minimum_required(VERSION 3.21)
project(AstraNDKCMakeSmoke C CXX)

find_package(Astra CONFIG REQUIRED)
find_package(SDL2 2.26.5 CONFIG REQUIRED)
find_package(SDL2_net CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
find_package(SDL2_mixer CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)
find_package(sdl2-image CONFIG REQUIRED)

add_executable(native native.c)
target_link_libraries(native PRIVATE Astra::Native Astra::streams)

add_executable(posix ../distribution_posix.c)
astra_posix_program(posix NAME ndk-cmake-posix VERSION 1.0.0
  AUTHOR "Astra 68 Project" COPYRIGHT "Copyright 2026 Astra 68 Project")

add_executable(cxx cxx23.cpp)
target_compile_features(cxx PRIVATE cxx_std_23)
astra_posix_program(cxx CXX NAME ndk-cmake-cxx VERSION 1.0.0
  AUTHOR "Astra 68 Project" COPYRIGHT "Copyright 2026 Astra 68 Project")

add_executable(sdl ../distribution_sdl.c)
target_link_libraries(sdl PRIVATE SDL2::SDL2 SDL2_net::SDL2_net
  SDL2_image::SDL2_image SDL2_mixer::SDL2_mixer SDL2_ttf::SDL2_ttf)
astra_posix_program(sdl NAME ndk-cmake-sdl VERSION 1.0.0
  AUTHOR "Astra 68 Project" COPYRIGHT "Copyright 2026 Astra 68 Project")
