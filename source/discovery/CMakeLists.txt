cmake_minimum_required(VERSION 3.28)

project(
    discovery-lib
    LANGUAGES CXX
)

# ---- Declare library ----

add_library(discovery_lib STATIC)

target_sources(discovery_lib ${warning_guard}
  PUBLIC
    discovery.h
    mdns_parse.h
    discovery.cpp
    mdns_parse.cpp
)

target_include_directories(discovery_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/source
)

target_compile_features(discovery_lib PUBLIC cxx_std_20)

# Winsock on Windows; POSIX sockets need no extra link on Linux/macOS.
if(WIN32)
  target_link_libraries(discovery_lib PRIVATE ws2_32)
endif()
