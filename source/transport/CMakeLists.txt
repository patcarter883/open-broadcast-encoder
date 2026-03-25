cmake_minimum_required(VERSION 3.28)


project(
    transport-lib
    LANGUAGES CXX
)

# ---- Declare library ----

add_library(
    transport_lib STATIC
)

target_sources(transport_lib ${warning_guard}
  PUBLIC
    transport.h
    transport.cpp
    transport_base.h
    transport_base.cpp
    buffer_distributor.h
    buffer_distributor.cpp
    transport_manager.h
    transport_manager.cpp
    rist_transport.h
    rist_transport.cpp
    srt_transport.h
    srt_transport.cpp
    rtmp_transport.h
    rtmp_transport.cpp
)

target_include_directories(
    transport_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/source
)

target_compile_features(transport_lib PUBLIC cxx_std_20)

target_link_libraries(transport_lib PUBLIC
ristnet
)

target_link_libraries(transport_lib PRIVATE
url_lib
open-broadcast-encoder_lib
)

target_link_libraries(transport_lib PRIVATE
PkgConfig::gstreamer
PkgConfig::gstreamer-sdp
PkgConfig::gstreamer-rtp
PkgConfig::gstreamer-app
PkgConfig::gstreamer-video)

target_include_directories(transport_lib PRIVATE SYSTEM ${GSTREAMER_INCLUDE_DIRS})