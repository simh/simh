include(CMakeFindDependencyMacro)
find_dependency(Freetype)
include("${CMAKE_CURRENT_LIST_DIR}/SDL2_TTFTargets.cmake")
