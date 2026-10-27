include(CMakeFindDependencyMacro)
find_dependency(SDL2 CONFIG)
if(NOT TARGET SDL2_ttf::SDL2_ttf)
  add_library(SDL2_ttf::SDL2_ttf INTERFACE IMPORTED)
  set_target_properties(SDL2_ttf::SDL2_ttf PROPERTIES
    INTERFACE_LINK_LIBRARIES "Astra::SDL2_ttf;SDL2::SDL2")
endif()
set(SDL2_ttf_VERSION @PACKAGE_VERSION@)
