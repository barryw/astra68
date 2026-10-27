cmake_minimum_required(VERSION 3.21)
project(AstraZ C)

find_package(Astra CONFIG REQUIRED)

set(FROTZ_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party/frotz/src)

add_library(astraz_frotz_core STATIC
  ${FROTZ_SOURCE}/common/buffer.c
  ${FROTZ_SOURCE}/common/err.c
  ${FROTZ_SOURCE}/common/fastmem.c
  ${FROTZ_SOURCE}/common/files.c
  ${FROTZ_SOURCE}/common/getopt.c
  ${FROTZ_SOURCE}/common/hotkey.c
  ${FROTZ_SOURCE}/common/input.c
  ${FROTZ_SOURCE}/common/main.c
  ${FROTZ_SOURCE}/common/math.c
  ${FROTZ_SOURCE}/common/missing.c
  ${FROTZ_SOURCE}/common/object.c
  ${FROTZ_SOURCE}/common/process.c
  ${FROTZ_SOURCE}/common/quetzal.c
  ${FROTZ_SOURCE}/common/random.c
  ${FROTZ_SOURCE}/common/redirect.c
  ${FROTZ_SOURCE}/common/screen.c
  ${FROTZ_SOURCE}/common/sound.c
  ${FROTZ_SOURCE}/common/stream.c
  ${FROTZ_SOURCE}/common/table.c
  ${FROTZ_SOURCE}/common/text.c
  ${FROTZ_SOURCE}/common/variable.c)
target_include_directories(astraz_frotz_core PUBLIC
  ${FROTZ_SOURCE}/common ${FROTZ_SOURCE}/blorb)
set_target_properties(astraz_frotz_core PROPERTIES
  C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)

add_library(astraz_frotz_blorb STATIC ${FROTZ_SOURCE}/blorb/blorblib.c)
target_include_directories(astraz_frotz_blorb PUBLIC
  ${FROTZ_SOURCE}/common ${FROTZ_SOURCE}/blorb)
set_target_properties(astraz_frotz_blorb PROPERTIES
  C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)

option(ASTRAZ_BUILD_ENGINE_SMOKE "Build native CLI engine test" OFF)
if(ASTRAZ_BUILD_ENGINE_SMOKE)
  add_executable(astraz_engine_smoke
    ${FROTZ_SOURCE}/dumb/dblorb.c
    ${FROTZ_SOURCE}/dumb/dinit.c
    ${FROTZ_SOURCE}/dumb/dinput.c
    ${FROTZ_SOURCE}/dumb/doutput.c
    ${FROTZ_SOURCE}/dumb/dpic.c)
  target_include_directories(astraz_engine_smoke PRIVATE ${FROTZ_SOURCE}/dumb)
  target_link_libraries(astraz_engine_smoke PRIVATE
    astraz_frotz_core astraz_frotz_blorb astraz_frotz_core)
  astra_posix_program(astraz_engine_smoke NAME "AstraZ Engine Smoke"
    VERSION 0.1.0 AUTHOR "Astra 68 Project"
    COPYRIGHT "Copyright 2026 Astra 68 Project")
endif()
