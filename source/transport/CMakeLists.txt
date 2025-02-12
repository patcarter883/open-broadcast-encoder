cmake_minimum_required(VERSION 3.28)

set(CMP0155 NEW)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

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
    FILE_SET CXX_MODULES FILES
      transport.cppm
)

# target_include_directories(
#     transport_lib ${warning_guard}
#     PUBLIC
#     ${CMAKE_CURRENT_LIST_DIR}
# )

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