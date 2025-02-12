cmake_minimum_required(VERSION 3.14)

project(
    url-lib
    LANGUAGES CXX
)

# ---- Declare library ----

add_library(
    url_lib STATIC
    ${CMAKE_CURRENT_LIST_DIR}/url.cc
)

target_include_directories(
    url_lib ${warning_guard}
    PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_features(url_lib PUBLIC cxx_std_20)
