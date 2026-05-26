# ExternalBuilds.cmake
#
# Builds external dependencies (fltk, rist-cpp, sdp-tools-cpp) in an isolated
# directory so that cleaning or removing the main build directory never forces
# a rebuild of the externals.
#
# Layout:
#   ${PROJECT_SOURCE_DIR}/build-external/<name>/install/  -- installed artifacts
#
# Each external is built once via ExternalProject_Add and exposed to the main
# project through IMPORTED targets.

include(ExternalProject)

set(EXTERNAL_BUILD_DIR "${PROJECT_SOURCE_DIR}/build-external")

find_package(Threads REQUIRED)

# ===========================================================================
# FLTK
# ===========================================================================

set(FLTK_PREFIX "${EXTERNAL_BUILD_DIR}/fltk")

ExternalProject_Add(
  external_fltk
  SOURCE_DIR "${PROJECT_SOURCE_DIR}/external/fltk"
  PREFIX     "${FLTK_PREFIX}"
  INSTALL_DIR "${FLTK_PREFIX}/install"

  CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
    -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}
    -DOPTION_BUILD_EXAMPLES:BOOL=OFF
    -DOPTION_BUILD_TESTS:BOOL=OFF
    -DOpenGL_GL_PREFERENCE:STRING=LEGACY
  BUILD_BYPRODUCTS
    "${FLTK_PREFIX}/install/lib/libfltk.a"
  BUILD_ALWAYS 0
)

ExternalProject_Get_Property(external_fltk INSTALL_DIR)
set(FLTK_INSTALL_DIR "${INSTALL_DIR}")

file(MAKE_DIRECTORY "${FLTK_INSTALL_DIR}/include")

add_library(fltk::fltk STATIC IMPORTED GLOBAL)

if(CMAKE_CONFIGURATION_TYPES)
  set(_fltk_lib_location "${FLTK_INSTALL_DIR}/lib/$<CONFIG>/libfltk.a")
else()
  set(_fltk_lib_location "${FLTK_INSTALL_DIR}/lib/libfltk.a")
endif()

set_target_properties(fltk::fltk PROPERTIES
   IMPORTED_LOCATION "${_fltk_lib_location}"
   INTERFACE_INCLUDE_DIRECTORIES "${FLTK_INSTALL_DIR}/include"
   INTERFACE_LINK_LIBRARIES "X11;Xcursor;Xfixes;Xinerama;Xft;fontconfig;Xrender;dl;Threads::Threads;m"
 )
 add_dependencies(fltk::fltk external_fltk)

# ===========================================================================
# rist-cpp  (includes librist via meson + ristnet wrapper)
# ===========================================================================

set(RIST_PREFIX "${EXTERNAL_BUILD_DIR}/rist-cpp")

ExternalProject_Add(
  external_rist_cpp
  SOURCE_DIR "${PROJECT_SOURCE_DIR}/external/rist-cpp"
  PREFIX     "${RIST_PREFIX}"
  INSTALL_DIR "${RIST_PREFIX}/install"

  CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
    -DCMAKE_BUILD_TYPE:STRING=Release
    -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}
    -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
  BUILD_BYPRODUCTS
    "${RIST_PREFIX}/install/lib/librist.a"
    "${RIST_PREFIX}/install/lib/libristnet.a"
  BUILD_ALWAYS 0
)

ExternalProject_Get_Property(external_rist_cpp INSTALL_DIR)
set(RIST_INSTALL_DIR "${INSTALL_DIR}")

file(MAKE_DIRECTORY
  "${RIST_INSTALL_DIR}/include"
  "${RIST_INSTALL_DIR}/include/librist"
  "${RIST_INSTALL_DIR}/include/rist-cpp"
)

add_library(rist STATIC IMPORTED GLOBAL)
set_target_properties(rist PROPERTIES
  IMPORTED_LOCATION "${RIST_INSTALL_DIR}/lib/librist.a"
  INTERFACE_INCLUDE_DIRECTORIES
    "${RIST_INSTALL_DIR}/include;${RIST_INSTALL_DIR}/include/librist"
)
add_dependencies(rist external_rist_cpp)

add_library(ristnet STATIC IMPORTED GLOBAL)
set_target_properties(ristnet PROPERTIES
  IMPORTED_LOCATION "${RIST_INSTALL_DIR}/lib/libristnet.a"
  INTERFACE_INCLUDE_DIRECTORIES "${RIST_INSTALL_DIR}/include;${RIST_INSTALL_DIR}/include/rist-cpp"
  INTERFACE_LINK_LIBRARIES "rist;Threads::Threads"
)
add_dependencies(ristnet external_rist_cpp)

# ===========================================================================
# sdp-tools-cpp  (currently unused but kept ready)
# ===========================================================================

set(SDP_TOOLS_PREFIX "${EXTERNAL_BUILD_DIR}/sdp-tools-cpp")

ExternalProject_Add(
  external_sdp_tools_cpp
  SOURCE_DIR "${PROJECT_SOURCE_DIR}/external/sdp-tools-cpp"
  PREFIX     "${SDP_TOOLS_PREFIX}"
  INSTALL_DIR "${SDP_TOOLS_PREFIX}/install"

  CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
    -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}
    -DCMAKE_CXX_STANDARD:STRING=20
    -DBUILD_EXAMPLES:BOOL=OFF
  INSTALL_COMMAND
    ${CMAKE_COMMAND} -E make_directory "<BINARY_DIR>/export"
    COMMAND ${CMAKE_COMMAND} --install "<BINARY_DIR>"
  BUILD_BYPRODUCTS
    "${SDP_TOOLS_PREFIX}/install/lib/libsdp-tools-cpp_sdp-tools-cpp.a"
  BUILD_ALWAYS 0
  EXCLUDE_FROM_ALL TRUE
)

ExternalProject_Get_Property(external_sdp_tools_cpp INSTALL_DIR)
set(SDP_TOOLS_INSTALL_DIR "${INSTALL_DIR}")

file(MAKE_DIRECTORY "${SDP_TOOLS_INSTALL_DIR}/include")

add_library(sdp-tools-cpp::sdp-tools-cpp STATIC IMPORTED GLOBAL)
set_target_properties(sdp-tools-cpp::sdp-tools-cpp PROPERTIES
  IMPORTED_LOCATION "${SDP_TOOLS_INSTALL_DIR}/lib/libsdp-tools-cpp_sdp-tools-cpp.a"
  INTERFACE_INCLUDE_DIRECTORIES "${SDP_TOOLS_INSTALL_DIR}/include"
)
add_dependencies(sdp-tools-cpp::sdp-tools-cpp external_sdp_tools_cpp)
