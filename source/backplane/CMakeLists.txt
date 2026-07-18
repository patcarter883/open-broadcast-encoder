cmake_minimum_required(VERSION 3.28)

project(
    backplane-client-lib
    LANGUAGES CXX
)

add_library(backplane_lib STATIC)

target_sources(backplane_lib ${warning_guard}
  PUBLIC
    backplane.h
    backplane.cpp
    backplane_httplib.cpp
)

target_include_directories(backplane_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/source
    ${CMAKE_SOURCE_DIR}/external
)

target_compile_features(backplane_lib PUBLIC cxx_std_20)

target_link_libraries(backplane_lib PRIVATE
    open-broadcast-encoder_lib
    nlohmann_json::nlohmann_json
    Threads::Threads
)
