cmake_minimum_required(VERSION 3.28)

set(CMP0155 NEW)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

project(
    encode-lib
    LANGUAGES CXX
)

# ---- Declare library ----

add_library(
    encode_lib STATIC
    
)

target_sources(encode_lib ${warning_guard}
  PUBLIC
    FILE_SET CXX_MODULES FILES
      encode.cppm
)

# target_include_directories(
#     encode_lib ${warning_guard}
#     PUBLIC
#     ${CMAKE_CURRENT_LIST_DIR}
# )

target_compile_features(encode_lib PUBLIC cxx_std_20)


target_link_libraries(encode_lib PRIVATE ${warning_guard}
open-broadcast-encoder_lib
PkgConfig::gstreamer
PkgConfig::gstreamer-sdp
PkgConfig::gstreamer-rtp
PkgConfig::gstreamer-app
PkgConfig::gstreamer-video)

target_include_directories(encode_lib PRIVATE SYSTEM
${GSTREAMER_INCLUDE_DIRS})