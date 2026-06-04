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

# Native builds keep externals in a top-level directory that survives wiping
# the preset build/ directory.  Cross-compile builds are isolated inside their
# own binary directory so they cannot overwrite native ELF artifacts with
# Windows COFF objects or vice-versa.
if(CMAKE_CROSSCOMPILING)
  set(EXTERNAL_BUILD_DIR "${CMAKE_BINARY_DIR}/external")
else()
  set(EXTERNAL_BUILD_DIR "${PROJECT_SOURCE_DIR}/build-external")
endif()

find_package(Threads REQUIRED)

# ===========================================================================
# MinGW cross-compile: case-fix include directory
#
# Linux has a case-sensitive filesystem but Windows SDK headers assume
# case-insensitive paths (e.g. #include <Winsock2.h>).  MinGW-w64 ships
# the headers in lowercase.  build-external/mingw-case-fix/include/ holds
# symlinks with the Windows-canonical casing; they're created once by:
#
#   cd build-external/mingw-case-fix/include
#   for f in /usr/x86_64-w64-mingw32/include/*.h; do
#     cap="$(python3 -c "s='$(basename $f)'; print(s[0].upper()+s[1:])")"
#     [ "$cap" != "$(basename $f)" ] && ln -sf "$f" "$cap"
#   done
# ===========================================================================
set(MINGW_CASE_FIX_DIR "${EXTERNAL_BUILD_DIR}/mingw-case-fix/include")

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
    # When cross-compiling, use the MinGW toolchain directly rather than vcpkg's
    # toolchain: vcpkg wraps add_executable in a way that breaks RPATH on
    # Windows targets built with Ninja on Linux.
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}
    -DOPTION_BUILD_EXAMPLES:BOOL=OFF
    -DOPTION_BUILD_TESTS:BOOL=OFF
    -DFLTK_BUILD_FLUID:BOOL=OFF
    -DFLTK_BUILD_FLTK_OPTIONS:BOOL=OFF
    -DFLTK_BUILD_TEST:BOOL=OFF
    -DBUILD_TESTING:BOOL=OFF
    -DOpenGL_GL_PREFERENCE:STRING=LEGACY
    # Cross-compilation: TestBigEndian can't run target binaries; x86_64 Windows is LE.
    $<$<BOOL:${CMAKE_CROSSCOMPILING}>:-DCMAKE_C_BYTE_ORDER:STRING=LITTLE_ENDIAN>
    $<$<BOOL:${CMAKE_CROSSCOMPILING}>:-DCMAKE_CXX_BYTE_ORDER:STRING=LITTLE_ENDIAN>
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

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_fltk_platform_libs "gdi32;gdiplus;comdlg32;ole32;oleaut32;uuid;comctl32;ws2_32;Threads::Threads")
else()
  set(_fltk_platform_libs "X11;Xcursor;Xfixes;Xinerama;Xft;fontconfig;Xrender;dl;Threads::Threads;m")
endif()
set_target_properties(fltk::fltk PROPERTIES
   IMPORTED_LOCATION "${_fltk_lib_location}"
   INTERFACE_INCLUDE_DIRECTORIES "${FLTK_INSTALL_DIR}/include"
   INTERFACE_LINK_LIBRARIES "${_fltk_platform_libs}"
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
    -DCMAKE_SYSTEM_NAME:STRING=${CMAKE_SYSTEM_NAME}
    -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}
    -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
    # MinGW cross-compile: provide an include dir with properly-cased Windows
    # header names (e.g. Winsock2.h) since Linux's filesystem is case-sensitive.
    $<$<BOOL:${CMAKE_CROSSCOMPILING}>:-DCMAKE_CXX_FLAGS:STRING=-I${MINGW_CASE_FIX_DIR}>
    $<$<BOOL:${CMAKE_CROSSCOMPILING}>:-DCMAKE_C_FLAGS:STRING=-I${MINGW_CASE_FIX_DIR}>
  # Use cmake --install so CMAKE_INSTALL_PREFIX is explicitly passed rather than
  # relying on cmake -P cmake_install.cmake which silently falls back to /usr/local.
  INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --prefix <INSTALL_DIR>
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
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_rist_extra_libs "ws2_32;iphlpapi;wsock32;bcrypt")
else()
  set(_rist_extra_libs "")
endif()
set_target_properties(rist PROPERTIES
  IMPORTED_LOCATION "${RIST_INSTALL_DIR}/lib/librist.a"
  INTERFACE_INCLUDE_DIRECTORIES
    "${RIST_INSTALL_DIR}/include;${RIST_INSTALL_DIR}/include/librist"
  INTERFACE_LINK_LIBRARIES "${_rist_extra_libs}"
)
add_dependencies(rist external_rist_cpp)

add_library(ristnet STATIC IMPORTED GLOBAL)
set_target_properties(ristnet PROPERTIES
  IMPORTED_LOCATION "${RIST_INSTALL_DIR}/lib/libristnet.a"
  INTERFACE_LINK_LIBRARIES "rist;Threads::Threads"
)
# RISTNet.h uses Windows-canonical header casing (Winsock2.h) which is
# case-sensitive on Linux.  Propagate the case-fix include dir to every
# consumer of ristnet so they can find it without modifying the source.
if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set_target_properties(ristnet PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES
      "${RIST_INSTALL_DIR}/include;${RIST_INSTALL_DIR}/include/rist-cpp;${MINGW_CASE_FIX_DIR}"
  )
else()
  set_target_properties(ristnet PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES
      "${RIST_INSTALL_DIR}/include;${RIST_INSTALL_DIR}/include/rist-cpp"
  )
endif()
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
