cmake_minimum_required(VERSION 3.28)


project(
    net-lib
    LANGUAGES CXX
)

# ---- Declare library ----

add_library(
    net_lib STATIC
)

target_sources(net_lib ${warning_guard}
  PUBLIC
    adapters.h
    adapters.cpp
)

target_include_directories(
    net_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/source
)

target_compile_features(net_lib PUBLIC cxx_std_20)

# GetAdaptersAddresses and the winsock calls behind inet_pton.
if(WIN32)
  target_link_libraries(net_lib PRIVATE iphlpapi ws2_32)
endif()
