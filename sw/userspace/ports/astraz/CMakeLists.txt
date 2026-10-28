cmake_minimum_required(VERSION 3.21)
project(AstraZ C)

find_package(Astra CONFIG REQUIRED)

set(FROTZ_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party/frotz/src)
set_source_files_properties(${FROTZ_SOURCE}/common/main.c PROPERTIES
  COMPILE_DEFINITIONS main=astraz_frotz_main)

add_library(astraz_engine STATIC
  src/engine.c
  ${FROTZ_SOURCE}/blorb/blorblib.c
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
target_include_directories(astraz_engine PRIVATE
  ${FROTZ_SOURCE}/common ${FROTZ_SOURCE}/blorb
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(astraz_engine PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(astraz_engine PROPERTIES
  C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)

option(ASTRAZ_BUILD_ENGINE_SMOKE "Build native CLI engine test" OFF)
if(ASTRAZ_BUILD_ENGINE_SMOKE)
  add_executable(astraz_engine_smoke
    src/smoke_main.c
    ${FROTZ_SOURCE}/dumb/dblorb.c
    ${FROTZ_SOURCE}/dumb/dinit.c
    ${FROTZ_SOURCE}/dumb/dinput.c
    ${FROTZ_SOURCE}/dumb/doutput.c
    ${FROTZ_SOURCE}/dumb/dpic.c)
  target_include_directories(astraz_engine_smoke PRIVATE
    ${FROTZ_SOURCE}/common ${FROTZ_SOURCE}/blorb ${FROTZ_SOURCE}/dumb)
  target_link_libraries(astraz_engine_smoke PRIVATE astraz_engine)
  astra_posix_program(astraz_engine_smoke NAME "AstraZ Engine Smoke"
    VERSION 0.1.0 AUTHOR "Astra 68 Project"
    COPYRIGHT "Copyright 2026 Astra 68 Project")

  add_executable(astraz_session_smoke src/session_smoke.c)
  target_link_libraries(astraz_session_smoke PRIVATE astraz_engine)
  astra_posix_program(astraz_session_smoke NAME "AstraZ Session Smoke"
    VERSION 0.1.0 AUTHOR "Astra 68 Project"
    COPYRIGHT "Copyright 2026 Astra 68 Project")

  add_executable(astraz_engine_contract_test src/engine_contract_test.c)
  target_link_libraries(astraz_engine_contract_test PRIVATE astraz_engine)
  astra_posix_program(astraz_engine_contract_test NAME "AstraZ Contract Test"
    VERSION 0.1.0 AUTHOR "Astra 68 Project"
    COPYRIGHT "Copyright 2026 Astra 68 Project")
endif()
