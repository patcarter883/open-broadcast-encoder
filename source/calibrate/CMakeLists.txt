cmake_minimum_required(VERSION 3.14)

project(
    calibrate-lib
    LANGUAGES CXX
)

# ---- Declare library ----

# Deliberately dependent on nothing: the probe's payload driver and statistics
# source are injected, so this builds and links without libRIST or GStreamer.
add_library(
    calibrate_lib STATIC
    ${CMAKE_CURRENT_LIST_DIR}/upload_probe.h
    ${CMAKE_CURRENT_LIST_DIR}/upload_probe.cpp
)

target_include_directories(
    calibrate_lib ${warning_guard}
    PUBLIC
    ${CMAKE_SOURCE_DIR}/source
)

target_compile_features(calibrate_lib PUBLIC cxx_std_20)
