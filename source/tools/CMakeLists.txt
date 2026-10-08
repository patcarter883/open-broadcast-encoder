cmake_minimum_required(VERSION 3.28)

project(
    obr-cam-browse
    LANGUAGES CXX
)

# ---- The LAN "discovered" view (MC5) ----
#
# A LAN-side helper for the panel's discovered-cameras list. It reuses the SAME
# DNS-SD parser the bridge browser uses: mdns.cpp and discovery.cpp are compiled
# into this target directly (the very same source files), not copied, so there is
# exactly ONE parser in the tree -- "one parser, two service types" (MC5.1).

add_executable(obr-cam-browse ${warning_guard}
    cam_browse.cpp
    ${CMAKE_SOURCE_DIR}/source/bridge/mdns.cpp
    ${CMAKE_SOURCE_DIR}/source/bridge/discovery.cpp
)

target_include_directories(obr-cam-browse PRIVATE
    ${CMAKE_SOURCE_DIR}/source
    ${CMAKE_SOURCE_DIR}/external
)

target_compile_features(obr-cam-browse PRIVATE cxx_std_20)

# Plain HTTP client/server only (localhost + the node's diagnostics API), so this
# target deliberately does NOT link httplib_ssl -- no TLS here, and no reason to
# drag OpenSSL into a helper that only reads the LAN.
target_link_libraries(obr-cam-browse PRIVATE
    nlohmann_json::nlohmann_json
    Threads::Threads
)
