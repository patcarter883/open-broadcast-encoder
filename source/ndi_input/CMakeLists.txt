cmake_minimum_required(VERSION 3.28)


project(
    open-broadcast-encoder-ndi-input-lib
    LANGUAGES CXX
)

# ---- Declare library ----

add_library(
    ndi_input_lib STATIC
)

target_sources(ndi_input_lib ${warning_guard}
  PUBLIC
    ndi_input.h
    ndi_input.cpp
)

target_include_directories(
    ndi_input_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/source
)

target_compile_features(ndi_input_lib PUBLIC cxx_std_20)

# Mirrors the executable's OBC_ENABLE_NDI so any future direct-SDK code added
# here can be fenced with #ifdef OBC_ENABLE_NDI. OFF by default (see the root
# CMakeLists.txt).
target_compile_definitions(ndi_input_lib PRIVATE
  $<$<BOOL:${OBC_ENABLE_NDI}>:OBC_ENABLE_NDI>)

target_link_libraries(ndi_input_lib PUBLIC
)

target_link_libraries(ndi_input_lib PRIVATE 
open-broadcast-encoder_lib
PkgConfig::gstreamer
PkgConfig::gstreamer-sdp
PkgConfig::gstreamer-rtp
PkgConfig::gstreamer-app
PkgConfig::gstreamer-video)

target_include_directories(ndi_input_lib PRIVATE ${GSTREAMER_INCLUDE_DIRS})