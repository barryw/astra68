include(CMakeFindDependencyMacro)
find_dependency(SDL2 CONFIG)
if(NOT TARGET SDL2_mixer::SDL2_mixer)
  add_library(SDL2_mixer::SDL2_mixer INTERFACE IMPORTED)
  set_target_properties(SDL2_mixer::SDL2_mixer PROPERTIES
    INTERFACE_LINK_LIBRARIES "Astra::SDL2_mixer;SDL2::SDL2")
endif()
set(SDL2_mixer_VERSION @PACKAGE_VERSION@)
