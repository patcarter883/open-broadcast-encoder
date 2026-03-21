cmake_minimum_required(VERSION 3.28)


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
    stats.h
    stats.cpp
)

target_include_directories(
    stats_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/source
)

target_compile_features(stats_lib PUBLIC cxx_std_20)

target_link_libraries(stats_lib PRIVATE ${warning_guard}
open-broadcast-encoder_lib
ui_lib
ristnet
)


target_include_directories(stats_lib PRIVATE SYSTEM
${GSTREAMER_INCLUDE_DIRS})