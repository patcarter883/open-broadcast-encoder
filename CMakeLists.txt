cmake_minimum_required(VERSION 3.28)


include(cmake/prelude.cmake)

project(
    open-broadcast-encoder
    VERSION 0.1.0
    DESCRIPTION "Streaming Broadcast Video Encoder"
    HOMEPAGE_URL "https://example.com/"
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_BUILD_PARALLEL_LEVEL 16)

include(cmake/variables.cmake)

include(cmake/ExternalBuilds.cmake)

find_package(NDI REQUIRED)

find_package(nlohmann_json 3.2 REQUIRED)

find_package(PkgConfig REQUIRED)
    pkg_search_module(gstreamer REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.28)
    pkg_search_module(gstreamer-sdp REQUIRED IMPORTED_TARGET gstreamer-sdp-1.0>=1.28)
    pkg_search_module(gstreamer-rtp REQUIRED IMPORTED_TARGET gstreamer-rtp-1.0>=1.28)
    pkg_search_module(gstreamer-app REQUIRED IMPORTED_TARGET gstreamer-app-1.0>=1.28)
    pkg_search_module(gstreamer-video REQUIRED IMPORTED_TARGET gstreamer-video-1.0>=1.28)

# cmake's FindPkgConfig doesn't always propagate -L dirs from the pkg-config
# wrapper into INTERFACE_LINK_DIRECTORIES when cross-compiling.  Add them
# explicitly so the MinGW linker can find the import libraries.
if(CMAKE_CROSSCOMPILING AND DEFINED ENV{GSTREAMER_MINGW_PREFIX})
  link_directories("$ENV{GSTREAMER_MINGW_PREFIX}/lib")
endif()
if(CMAKE_CROSSCOMPILING AND NDI_LIBRARY_DIR)
  link_directories("${NDI_LIBRARY_DIR}")
endif()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/lib)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/ui)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/ndi_input)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/discovery)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/transport)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/encode)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/stats)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/url)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/control)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/backplane)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/settings)

# ---- Declare executable ----

add_executable(open-broadcast-encoder_exe ${CMAKE_CURRENT_LIST_DIR}/source/main.cpp)
add_executable(open-broadcast-encoder::exe ALIAS open-broadcast-encoder_exe)

set_property(TARGET open-broadcast-encoder_exe PROPERTY OUTPUT_NAME open-broadcast-encoder)

# CachyOS compiles its crt files with znver4 ISA notes which propagate into
# any linked binary and cause the glibc loader to reject the binary on older
# CPUs — even when the code itself targets the x86-64 baseline. Strip the
# note so the binary runs on any x86-64 regardless of the build host's CRT.
if(UNIX AND NOT APPLE AND NOT CMAKE_CROSSCOMPILING)
  find_program(OBJCOPY_EXE NAMES objcopy llvm-objcopy)
  if(OBJCOPY_EXE)
    add_custom_command(TARGET open-broadcast-encoder_exe POST_BUILD
      COMMAND ${OBJCOPY_EXE} --remove-section=.note.gnu.property
              $<TARGET_FILE:open-broadcast-encoder_exe>
      COMMENT "Stripping x86 ISA-level note for cross-CPU portability"
    )
  endif()
endif()

target_compile_features(open-broadcast-encoder_exe PRIVATE cxx_std_20)

target_link_libraries(open-broadcast-encoder_exe PRIVATE
  open-broadcast-encoder_lib
  stats_lib
  ndi_input_lib
  ui_lib
  encode_lib
  transport_lib
  discovery_lib
  control_lib
  backplane_lib
  settings_lib
  ristnet
  nlohmann_json::nlohmann_json
  PkgConfig::gstreamer
  PkgConfig::gstreamer-sdp
  PkgConfig::gstreamer-rtp
  PkgConfig::gstreamer-app
  PkgConfig::gstreamer-video
  ${NDI_LIBRARIES})


target_include_directories(open-broadcast-encoder_exe PUBLIC SYSTEM
${RIST_INSTALL_DIR}/include
${RIST_INSTALL_DIR}/include/rist-cpp
${GSTREAMER_INCLUDE_DIRS}
${NDI_INCLUDE_DIRS})

target_include_directories(open-broadcast-encoder_exe PUBLIC
${CMAKE_CURRENT_LIST_DIR}/source/lib
${CMAKE_CURRENT_LIST_DIR}/source/stats
${CMAKE_CURRENT_LIST_DIR}/source/encode
${CMAKE_CURRENT_LIST_DIR}/source/transport
${CMAKE_CURRENT_LIST_DIR}/source/ndi_input
${CMAKE_CURRENT_LIST_DIR}/source/ui
${CMAKE_CURRENT_LIST_DIR}/source/control
${CMAKE_CURRENT_LIST_DIR}/source/backplane
${CMAKE_CURRENT_LIST_DIR}/external)



# ---- Install rules ----

if(NOT CMAKE_SKIP_INSTALL_RULES)
  include(cmake/install-rules.cmake)
endif()

# ---- Developer mode ----

if(NOT open-broadcast-encoder_DEVELOPER_MODE)
  return()
elseif(NOT PROJECT_IS_TOP_LEVEL)
  message(
      AUTHOR_WARNING
      "Developer mode is intended for developers of open-broadcast-encoder"
  )
endif()

include(cmake/dev-mode.cmake)