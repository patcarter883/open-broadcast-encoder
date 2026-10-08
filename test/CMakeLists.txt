# Parent project does not export its library target, so this CML implicitly
# depends on being added from it, i.e. the testing is done only from the build
# tree and is not feasible from an install location

project(open-broadcast-encoderTests LANGUAGES CXX)

# ---- Dependencies ----

find_package(Catch2 REQUIRED)
include(Catch)


# ---- Tests ----

add_executable(open-broadcast-encoder_test source/open-broadcast-encoder_test.cpp)
target_link_libraries(
    open-broadcast-encoder_test PRIVATE
    open-broadcast-encoder_lib
    Catch2::Catch2WithMain
)
target_compile_features(open-broadcast-encoder_test PRIVATE cxx_std_20)

catch_discover_tests(open-broadcast-encoder_test)

add_executable(backplane_test source/backplane_test.cpp)
target_link_libraries(
    backplane_test PRIVATE
    backplane_lib
    open-broadcast-encoder_lib
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)
target_compile_features(backplane_test PRIVATE cxx_std_20)

catch_discover_tests(backplane_test)

add_executable(device_auth_test source/device_auth_test.cpp)
target_link_libraries(
    device_auth_test PRIVATE
    backplane_lib
    open-broadcast-encoder_lib
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)
target_compile_features(device_auth_test PRIVATE cxx_std_20)

catch_discover_tests(device_auth_test)

add_executable(ubus_test source/ubus_test.cpp)
target_link_libraries(
    ubus_test PRIVATE
    bridge_lib
    open-broadcast-encoder_lib
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)
target_compile_features(ubus_test PRIVATE cxx_std_20)

catch_discover_tests(ubus_test)

add_executable(mdns_test source/mdns_test.cpp)
target_link_libraries(
    mdns_test PRIVATE
    bridge_lib
    open-broadcast-encoder_lib
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)
target_compile_features(mdns_test PRIVATE cxx_std_20)

catch_discover_tests(mdns_test)

add_executable(discovery_test source/discovery_test.cpp)
target_link_libraries(
    discovery_test PRIVATE
    bridge_lib
    open-broadcast-encoder_lib
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)
target_compile_features(discovery_test PRIVATE cxx_std_20)

catch_discover_tests(discovery_test)

add_executable(bridge_client_test source/bridge_client_test.cpp)
target_link_libraries(
    bridge_client_test PRIVATE
    bridge_lib
    open-broadcast-encoder_lib
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)
target_compile_features(bridge_client_test PRIVATE cxx_std_20)

catch_discover_tests(bridge_client_test)

add_executable(reporting_test source/reporting_test.cpp)
target_link_libraries(
    reporting_test PRIVATE
    bridge_lib
    open-broadcast-encoder_lib
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)
target_compile_features(reporting_test PRIVATE cxx_std_20)

catch_discover_tests(reporting_test)

# raw_format.cpp carries no GStreamer or libobs types, so this test compiles the
# translation unit directly rather than linking encode_lib: the mapping is a
# contract with an external format (libobs' enum values, arriving on the raw
# wire) and this keeps its lane free of the pipeline.
add_executable(raw_format_test
    source/raw_format_test.cpp
    ${CMAKE_SOURCE_DIR}/source/encode/raw_format.cpp
)
target_include_directories(raw_format_test PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_features(raw_format_test PRIVATE cxx_std_20)
target_link_libraries(raw_format_test PRIVATE Catch2::Catch2WithMain)

catch_discover_tests(raw_format_test)

# capture_source.cpp carries no GStreamer types either: it is the unicast
# addressing half of the JPEG XS capture ingest (MC4), and compiling it directly
# keeps its lane free of the pipeline.
add_executable(capture_source_test
    source/capture_source_test.cpp
    ${CMAKE_SOURCE_DIR}/source/encode/capture_source.cpp
)
target_include_directories(capture_source_test PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_features(capture_source_test PRIVATE cxx_std_20)
target_link_libraries(capture_source_test PRIVATE Catch2::Catch2WithMain)

catch_discover_tests(capture_source_test)

# The settings contract for the capture input mode: the mode name and the chosen
# camera must survive a save/load round-trip (MC4).
add_executable(capture_settings_test source/capture_settings_test.cpp)
target_link_libraries(
    capture_settings_test PRIVATE
    settings_lib
    open-broadcast-encoder_lib
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)
target_compile_features(capture_settings_test PRIVATE cxx_std_20)

catch_discover_tests(capture_settings_test)

# The one Allocate action: allocate -> apply the bridge on the LAN -> report. Links
# both libs because the whole point is the seam between them.
add_executable(actuate_test source/actuate_test.cpp)
target_link_libraries(
    actuate_test PRIVATE
    bridge_lib
    backplane_lib
    open-broadcast-encoder_lib
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)
target_compile_features(actuate_test PRIVATE cxx_std_20)

catch_discover_tests(actuate_test)

# The body the encoder sends to a hosted receiver's control plane. control_client
# talks httplib directly, so prepare_hosted_body() is split out as a pure
# string-in/string-out step precisely so this contract can be pinned from a
# fixture instead of an HTTP server.
add_executable(control_client_test source/control_client_test.cpp)
target_link_libraries(
    control_client_test PRIVATE
    control_lib
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)
target_compile_features(control_client_test PRIVATE cxx_std_20)

catch_discover_tests(control_client_test)

# ---- End-of-file commands ----

add_folders(Test)
