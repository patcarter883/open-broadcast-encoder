cmake_minimum_required(VERSION 3.28)


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
    encode.h
    encode.cpp
)

target_include_directories(
    encode_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/source
)

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