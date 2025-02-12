cmake_minimum_required(VERSION 3.28)

set(CMP0155 NEW)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

project(
    open-broadcast-encoder-lib
    LANGUAGES CXX
)

# ---- Declare library ----

add_library(
    open-broadcast-encoder_lib STATIC
)

target_sources(open-broadcast-encoder_lib
  PUBLIC
#   ${CMAKE_CURRENT_LIST_DIR}/encode.cpp
    FILE_SET CXX_MODULES FILES
      lib.cppm
)

# target_include_directories(
#     open-broadcast-encoder_lib ${warning_guard}
#     PUBLIC
#     ${CMAKE_CURRENT_LIST_DIR}
# )

target_compile_features(open-broadcast-encoder_lib PUBLIC cxx_std_20)

target_link_libraries(open-broadcast-encoder_lib PUBLIC

)

target_link_libraries(open-broadcast-encoder_lib PRIVATE 
# sdp-tools-cpp::sdp-tools-cpp
PkgConfig::gstreamer
PkgConfig::gstreamer-sdp
PkgConfig::gstreamer-rtp
PkgConfig::gstreamer-app
PkgConfig::gstreamer-video
)

target_include_directories(open-broadcast-encoder_lib PRIVATE 
${GSTREAMER_INCLUDE_DIRS})