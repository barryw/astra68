include(CMakeFindDependencyMacro)
find_dependency(SDL2 CONFIG)
if(NOT TARGET SDL2_image::SDL2_image)
  add_library(SDL2_image::SDL2_image INTERFACE IMPORTED)
  set_target_properties(SDL2_image::SDL2_image PROPERTIES
    INTERFACE_LINK_LIBRARIES "Astra::SDL2_image;SDL2::SDL2")
endif()
if(NOT TARGET SDL2::SDL2_image)
  add_library(SDL2::SDL2_image INTERFACE IMPORTED)
  set_target_properties(SDL2::SDL2_image PROPERTIES
    INTERFACE_LINK_LIBRARIES SDL2_image::SDL2_image)
endif()
set(SDL2_image_VERSION @PACKAGE_VERSION@)
