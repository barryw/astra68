include(CMakeFindDependencyMacro)
find_dependency(SDL2 CONFIG)
if(NOT TARGET SDL2_net::SDL2_net)
  add_library(SDL2_net::SDL2_net INTERFACE IMPORTED)
  set_target_properties(SDL2_net::SDL2_net PROPERTIES
    INTERFACE_LINK_LIBRARIES "Astra::SDL2_net;SDL2::SDL2")
endif()
set(SDL2_net_VERSION @PACKAGE_VERSION@)
