cmake_minimum_required(VERSION 3.28)

project(
    bridge-client-lib
    LANGUAGES CXX
)

add_library(bridge_lib STATIC)

target_sources(bridge_lib ${warning_guard}
  PUBLIC
    ubus.h
    ubus.cpp
    ubus_httplib.cpp
    mdns.h
    mdns.cpp
    discovery.h
    discovery.cpp
    bridge_client.h
    bridge_client.cpp
    reporting.h
    reporting.cpp
)

target_include_directories(bridge_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/source
    ${CMAKE_SOURCE_DIR}/external
)

target_compile_features(bridge_lib PUBLIC cxx_std_20)

target_link_libraries(bridge_lib PRIVATE
    open-broadcast-encoder_lib
    nlohmann_json::nlohmann_json
    Threads::Threads
)
