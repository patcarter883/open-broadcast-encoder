cmake_minimum_required(VERSION 3.28)

set(CMP0155 NEW)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

project(
    stats_lib
    LANGUAGES CXX
)

add_compile_options(
"$<$<COMPILE_LANGUAGE:CXX>:-pthread>"
)

# ---- Declare library ----

add_library(
    stats_lib STATIC
)

target_sources(stats_lib ${warning_guard}
  PUBLIC
#   ${CMAKE_CURRENT_LIST_DIR}/encode.cpp
    FILE_SET CXX_MODULES FILES
      stats.cppm
)

# target_include_directories(
#     stats_lib ${warning_guard}
#     PUBLIC
#     ${CMAKE_CURRENT_LIST_DIR}
# )

target_compile_features(stats_lib PUBLIC cxx_std_20)

target_link_libraries(stats_lib PRIVATE ${warning_guard}
open-broadcast-encoder_lib
ui_lib
ristnet
)


target_include_directories(stats_lib PRIVATE SYSTEM
${GSTREAMER_INCLUDE_DIRS})