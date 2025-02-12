cmake_minimum_required(VERSION 3.28)

set(CMP0155 NEW)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

include(cmake/prelude.cmake)
include(ExternalProject)
include(FetchContent)

project(
    open-broadcast-encoder
    VERSION 0.1.0
    DESCRIPTION "Streaming Broadcast Video Encoder"
    HOMEPAGE_URL "https://example.com/"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# include(cmake/project-is-top-level.cmake)
include(cmake/variables.cmake)

# include_directories(${CMAKE_CURRENT_LIST_DIR}/include)

# add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/external/sdp-tools-cpp)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/external/fltk)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/external/rist-cpp)

find_package(NDI REQUIRED)

find_package(PkgConfig REQUIRED)
    pkg_search_module(gstreamer REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.24)
    pkg_search_module(gstreamer-sdp REQUIRED IMPORTED_TARGET gstreamer-sdp-1.0>=1.24)
    pkg_search_module(gstreamer-rtp REQUIRED IMPORTED_TARGET gstreamer-rtp-1.0>=1.24)
    pkg_search_module(gstreamer-app REQUIRED IMPORTED_TARGET gstreamer-app-1.0>=1.24)
    pkg_search_module(gstreamer-video REQUIRED IMPORTED_TARGET gstreamer-video-1.0>=1.24)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/lib)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/ui)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/ndi_input)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/transport)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/encode)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/stats)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/source/url)

# ---- Declare executable ----

add_executable(open-broadcast-encoder_exe ${CMAKE_CURRENT_LIST_DIR}/source/main.cpp)
add_executable(open-broadcast-encoder::exe ALIAS open-broadcast-encoder_exe)

set_property(TARGET open-broadcast-encoder_exe PROPERTY OUTPUT_NAME open-broadcast-encoder)

target_compile_features(open-broadcast-encoder_exe PRIVATE cxx_std_20)

target_link_libraries(open-broadcast-encoder_exe PRIVATE 
  open-broadcast-encoder_lib 
  stats_lib
  ndi_input_lib
  ui_lib
  encode_lib
  transport_lib
  ristnet 
  PkgConfig::gstreamer
  PkgConfig::gstreamer-sdp
  PkgConfig::gstreamer-rtp
  PkgConfig::gstreamer-app
  PkgConfig::gstreamer-video
  ${NDI_LIBRARIES})

target_include_directories(open-broadcast-encoder_exe PUBLIC 
${rist-cpp_SOURCE_DIR} 
${GSTREAMER_INCLUDE_DIRS}
${NDI_INCLUDE_DIRS})



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