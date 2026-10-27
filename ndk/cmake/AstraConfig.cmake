get_filename_component(_ASTRA_NDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(_ASTRA_LIB_DIR "${_ASTRA_NDK_ROOT}/lib/m68040")

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Astra")
  message(FATAL_ERROR "Astra NDK requires the installed m68k-astra-toolchain.cmake")
endif()

# Import exact installed SONAMEs. A second ABI major in one SDK is ambiguous.
file(GLOB _ASTRA_LIBRARIES LIST_DIRECTORIES FALSE "${_ASTRA_LIB_DIR}/*.library.*")
foreach(_ASTRA_LIBRARY IN LISTS _ASTRA_LIBRARIES)
  get_filename_component(_ASTRA_FILENAME "${_ASTRA_LIBRARY}" NAME)
  if(_ASTRA_FILENAME MATCHES "^([A-Za-z0-9_]+)\\.library\\.[0-9]+$")
    set(_ASTRA_NAME "${CMAKE_MATCH_1}")
    if(NOT _ASTRA_NAME STREQUAL "loader")
      if(TARGET Astra::${_ASTRA_NAME})
        get_target_property(_ASTRA_EXISTING Astra::${_ASTRA_NAME} IMPORTED_LOCATION)
        if(NOT _ASTRA_EXISTING STREQUAL _ASTRA_LIBRARY)
          message(FATAL_ERROR "Astra NDK has multiple ABI majors for ${_ASTRA_NAME}")
        endif()
      else()
        add_library(Astra::${_ASTRA_NAME} UNKNOWN IMPORTED)
        set_target_properties(Astra::${_ASTRA_NAME} PROPERTIES
          IMPORTED_LOCATION "${_ASTRA_LIBRARY}")
      endif()
    endif()
  endif()
endforeach()

foreach(_ASTRA_REQUIRED runtime compiler libc cxx unwind SDL2 SDL2_net
                        SDL2_image SDL2_mixer SDL2_ttf)
  if(NOT TARGET Astra::${_ASTRA_REQUIRED})
    message(FATAL_ERROR "Astra NDK missing ${_ASTRA_REQUIRED}.library")
  endif()
endforeach()

if(NOT TARGET Astra::Program)
  add_library(Astra::Program INTERFACE IMPORTED)
  set_target_properties(Astra::Program PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_ASTRA_NDK_ROOT}/include"
    INTERFACE_LINK_OPTIONS "-nostdlib;-pie;-Wl,-Bdynamic;-Wl,--no-as-needed;-Wl,-z,now;-Wl,-z,relro;-Wl,-z,max-page-size=0x1000;-Wl,--build-id=none;-Wl,--gc-sections;-T${_ASTRA_LIB_DIR}/astra_user.ld;-L${_ASTRA_LIB_DIR};-Wl,-rpath-link,${_ASTRA_LIB_DIR};${_ASTRA_LIB_DIR}/crt0-dynamic.o"
    INTERFACE_LINK_LIBRARIES "Astra::runtime;Astra::compiler")
endif()

if(NOT TARGET Astra::Native)
  add_library(Astra::Native INTERFACE IMPORTED)
  set_target_properties(Astra::Native PROPERTIES
    INTERFACE_COMPILE_OPTIONS "-ffreestanding;-fno-builtin"
    INTERFACE_LINK_LIBRARIES Astra::Program)
endif()

if(NOT TARGET Astra::POSIX)
  add_library(Astra::POSIX INTERFACE IMPORTED)
  set_target_properties(Astra::POSIX PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_ASTRA_NDK_ROOT}/include/posix"
    INTERFACE_LINK_OPTIONS "-Wl,--undefined=astra_posix_entry_contract"
    INTERFACE_LINK_LIBRARIES "Astra::Program;Astra::libc")
endif()

if(NOT TARGET Astra::POSIX_CXX)
  add_library(Astra::POSIX_CXX INTERFACE IMPORTED)
  set_target_properties(Astra::POSIX_CXX PROPERTIES
    INTERFACE_LINK_LIBRARIES "Astra::POSIX;Astra::cxx;Astra::unwind")
endif()

# Upstream source remains untouched; the application wrapper names its target.
function(astra_posix_program target)
  cmake_parse_arguments(ARG "CXX" "NAME;VERSION;AUTHOR;COPYRIGHT" "" ${ARGN})
  if(ARG_UNPARSED_ARGUMENTS OR NOT TARGET ${target} OR
     NOT ARG_NAME OR NOT ARG_VERSION OR NOT ARG_AUTHOR OR NOT ARG_COPYRIGHT OR
     NOT ARG_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "astra_posix_program requires target, NAME, VERSION, AUTHOR, COPYRIGHT")
  endif()
  string(REPLACE "." ";" _ASTRA_VERSION "${ARG_VERSION}")
  list(GET _ASTRA_VERSION 0 _ASTRA_MAJOR)
  list(GET _ASTRA_VERSION 1 _ASTRA_MINOR)
  list(GET _ASTRA_VERSION 2 _ASTRA_PATCH)
  target_sources(${target} PRIVATE "${_ASTRA_NDK_ROOT}/src/astra-posix-program.c")
  target_compile_definitions(${target} PRIVATE
    "ASTRA_POSIX_PROGRAM_NAME=\"${ARG_NAME}\""
    "ASTRA_POSIX_PROGRAM_MAJOR=${_ASTRA_MAJOR}"
    "ASTRA_POSIX_PROGRAM_MINOR=${_ASTRA_MINOR}"
    "ASTRA_POSIX_PROGRAM_PATCH=${_ASTRA_PATCH}"
    "ASTRA_POSIX_PROGRAM_AUTHOR=\"${ARG_AUTHOR}\""
    "ASTRA_POSIX_PROGRAM_COPYRIGHT=\"${ARG_COPYRIGHT}\"")
  if(ARG_CXX)
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=crtbeginS.o
      OUTPUT_VARIABLE _ASTRA_CRTBEGIN OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=crtendS.o
      OUTPUT_VARIABLE _ASTRA_CRTEND OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT EXISTS "${_ASTRA_CRTBEGIN}" OR NOT EXISTS "${_ASTRA_CRTEND}")
      message(FATAL_ERROR "Astra C++ compiler missing crtbeginS.o or crtendS.o")
    endif()
    target_link_options(${target} PRIVATE "${_ASTRA_CRTBEGIN}")
    target_link_libraries(${target} PRIVATE Astra::POSIX_CXX "${_ASTRA_CRTEND}")
  else()
    target_link_libraries(${target} PRIVATE Astra::POSIX)
  endif()
endfunction()
