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

# The OBS raw ingest's port decision, compiled the same way capture_source_test
# is: the .cpp directly, because it carries no GStreamer types and the decision
# must be pinned without a pipeline.
add_executable(raw_local_port_test
    source/raw_local_port_test.cpp
    ${CMAKE_SOURCE_DIR}/source/encode/raw_local_port.cpp
)
target_include_directories(raw_local_port_test PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_features(raw_local_port_test PRIVATE cxx_std_20)
target_link_libraries(raw_local_port_test PRIVATE Catch2::Catch2WithMain)

catch_discover_tests(raw_local_port_test)

# The AV1 mezzanine contract: the profile the receiver's hardware decoder
# accepts is only visible in the encoder fragment, which is a string.
# video_encoder.cpp carries no GStreamer types, so this compiles directly and
# needs no pipeline -- and no qsvav1enc, which is not installed on a dev box.
add_executable(video_encoder_test
    source/video_encoder_test.cpp
    ${CMAKE_SOURCE_DIR}/source/encode/video_encoder.cpp
)
target_include_directories(video_encoder_test PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_features(video_encoder_test PRIVATE cxx_std_20)
target_link_libraries(video_encoder_test PRIVATE Catch2::Catch2WithMain)

catch_discover_tests(video_encoder_test)

# The multicast join interface the encoder puts into a bridge's listen URL.
# join_interface.cpp carries no other dependency, so it is compiled directly: the
# URL it builds must be pinnable without linking the bridge's HTTP stack.
add_executable(join_interface_test
    source/join_interface_test.cpp
    ${CMAKE_SOURCE_DIR}/source/bridge/join_interface.cpp
)
target_include_directories(join_interface_test PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_features(join_interface_test PRIVATE cxx_std_20)
target_link_libraries(join_interface_test PRIVATE Catch2::Catch2WithMain)

catch_discover_tests(join_interface_test)

# The preview/encode appsrc policies: header-only by design, so the numbers and
# the pipeline properties are pinned without a GStreamer pipeline.
add_executable(appsrc_policy_test source/appsrc_policy_test.cpp)
target_include_directories(appsrc_policy_test PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_features(appsrc_policy_test PRIVATE cxx_std_20)
target_link_libraries(appsrc_policy_test PRIVATE Catch2::Catch2WithMain)

catch_discover_tests(appsrc_policy_test)

# The transcode target's derivation: header-only by design, so the numbers a
# hosted start body may carry are pinned without a portal or a WAN.
add_executable(transcode_rate_test source/transcode_rate_test.cpp)
target_include_directories(transcode_rate_test PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_features(transcode_rate_test PRIVATE cxx_std_20)
target_link_libraries(transcode_rate_test PRIVATE Catch2::Catch2WithMain)

catch_discover_tests(transcode_rate_test)

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

# The hosted (portal) block and the bridge block are the settings an operator
# types by hand; losing either on restart signs the encoder out of hosted mode.
add_executable(hosted_settings_test source/hosted_settings_test.cpp)
target_link_libraries(
    hosted_settings_test PRIVATE
    settings_lib
    open-broadcast-encoder_lib
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)
target_compile_features(hosted_settings_test PRIVATE cxx_std_20)

catch_discover_tests(hosted_settings_test)

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

# The AV1-in-MPEG-TS contract: the mux settings in encode/ts_mux.h must produce a
# stream that tsdemux reads back as video/x-av1 (stream_type 0x06 + 'AV1G').
add_executable(ts_mux_test source/ts_mux_test.cpp)
target_link_libraries(
    ts_mux_test PRIVATE
    PkgConfig::gstreamer
    Catch2::Catch2WithMain
)
target_include_directories(ts_mux_test PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_features(ts_mux_test PRIVATE cxx_std_20)

catch_discover_tests(ts_mux_test)

# The preview queue against a real appsrc: a stalled sink must cost frames, not
# the reader's progress. Pins the element properties the reader relies on
# (current-level-bytes, max-bytes, and the fact that a lossy push still returns
# GST_FLOW_OK), which is what makes the reader's own level check the only place
# a drop can be counted.
add_executable(preview_backpressure_test source/preview_backpressure_test.cpp)
target_link_libraries(
    preview_backpressure_test PRIVATE
    PkgConfig::gstreamer
    PkgConfig::gstreamer-app
    Catch2::Catch2WithMain
)
target_include_directories(preview_backpressure_test PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_features(preview_backpressure_test PRIVATE cxx_std_20)

catch_discover_tests(preview_backpressure_test)

# The RIST Address field decides where the stream goes, and it is the only
# value on screen: address.cpp carries no other dependency, so the parse is
# pinned without a pipeline or a socket.
add_executable(address_parse_test
    source/address_parse_test.cpp
    ${CMAKE_SOURCE_DIR}/source/lib/address.cpp
)
target_include_directories(address_parse_test PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_features(address_parse_test PRIVATE cxx_std_20)
target_link_libraries(address_parse_test PRIVATE Catch2::Catch2WithMain)

catch_discover_tests(address_parse_test)

# ---- End-of-file commands ----

add_folders(Test)
# --- Task 4a: the direct leg's PSK, proved on loopback -----------------------
#
# The sender under test is the production transport (setup_rist_sender with the
# session key in output_config::psk) and the listener is the same rist-cpp
# wrapper the receiver ships, on real libRIST. The matrix the test asserts is
# the WORKLIST-12 one: keyed->keyed delivers the whole payload, keyed->unkeyed
# delivers nothing, clear->clear is unchanged. It needs no display and no
# network beyond loopback.
add_executable(rist_psk_test source/rist_psk_test.cpp)
target_link_libraries(
    rist_psk_test PRIVATE
    transport_lib
    open-broadcast-encoder_lib
    Catch2::Catch2WithMain
)
target_compile_features(rist_psk_test PRIVATE cxx_std_20)

catch_discover_tests(rist_psk_test)

# --- Task 4c: the per-peer display state -------------------------------------
#
# Header-only by design, like the other decision tests: the registry, the
# aggregate and the row format are pinned without a window or a sender.
add_executable(peer_stats_test source/peer_stats_test.cpp)
target_include_directories(peer_stats_test PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_features(peer_stats_test PRIVATE cxx_std_20)
target_link_libraries(peer_stats_test PRIVATE Catch2::Catch2WithMain)

catch_discover_tests(peer_stats_test)

# --- Encoder-side bonding: the decisions, pinned without a pipeline ----------
#
# Which URL each uplink produces (miface + weight on the same session port), the
# adapter-name resolution rule under both platform bind styles, and the
# stats-to-weight mapping. The transport library is linked for uplink.cpp; no
# sender, no GStreamer pipeline and no network are started.
add_executable(uplink_test source/uplink_test.cpp)
target_link_libraries(
    uplink_test PRIVATE
    transport_lib
    open-broadcast-encoder_lib
    Catch2::Catch2WithMain
)
target_compile_features(uplink_test PRIVATE cxx_std_20)

catch_discover_tests(uplink_test)

# --- Encoder-side bonding: two peers, two interfaces, real libRIST -----------
#
# This one runs the real sender, because the assertion is on librist's own bind
# line ("Binding socket to <device>"): a failed device bind is logged and
# ignored, so the log is the only proof the leg left by the interface it named.
add_executable(bond_two_interface_test source/bond_two_interface_test.cpp)
target_link_libraries(
    bond_two_interface_test PRIVATE
    transport_lib
    open-broadcast-encoder_lib
    Catch2::Catch2WithMain
)
target_compile_features(bond_two_interface_test PRIVATE cxx_std_20)

catch_discover_tests(bond_two_interface_test)

# --- Encoder-side bonding: the settings file ---------------------------------
#
# The uplink list is also hand-editable, so both directions are pinned: what a
# save writes, and what a load accepts.
add_executable(uplink_settings_test source/uplink_settings_test.cpp)
target_link_libraries(
    uplink_settings_test PRIVATE
    settings_lib
    open-broadcast-encoder_lib
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)
target_compile_features(uplink_settings_test PRIVATE cxx_std_20)

# --- The NDI runtime probe and the options group it gates --------------------
#
# ndi_runtime.cpp is compiled directly and linked against GStreamer: the probe
# is a registry lookup, so the test asserts it against a second, independent
# lookup rather than against a define. ndi_options.h is header-only, so the
# widget state is pinned with no window on screen.
add_executable(ndi_runtime_test
    source/ndi_runtime_test.cpp
    ${CMAKE_SOURCE_DIR}/source/ndi_input/ndi_runtime.cpp
)
target_include_directories(ndi_runtime_test PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_features(ndi_runtime_test PRIVATE cxx_std_20)
target_link_libraries(
    ndi_runtime_test PRIVATE
    PkgConfig::gstreamer
    Catch2::Catch2WithMain
)

catch_discover_tests(ndi_runtime_test)

catch_discover_tests(uplink_settings_test)

# The control channel the encoder opens beside the OBS raw listener: the port
# pairing both sides derive, and the state a client is told. raw_control.cpp
# carries no GStreamer types, so this compiles directly and needs no pipeline.
add_executable(raw_control_test
    source/raw_control_test.cpp
    ${CMAKE_SOURCE_DIR}/source/encode/raw_control.cpp
)
target_include_directories(raw_control_test PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_features(raw_control_test PRIVATE cxx_std_20)
target_link_libraries(raw_control_test PRIVATE Catch2::Catch2WithMain)

catch_discover_tests(raw_control_test)

# The handshake through the real reader: the control channel has to appear with
# the frame listener and tell the plugin to start, then stop it at teardown.
add_executable(raw_local_control_test
    source/raw_local_control_test.cpp
    ${CMAKE_SOURCE_DIR}/source/encode/raw_local.cpp
    ${CMAKE_SOURCE_DIR}/source/encode/raw_control.cpp
    ${CMAKE_SOURCE_DIR}/source/encode/raw_format.cpp
)
target_include_directories(raw_local_control_test PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_compile_features(raw_local_control_test PRIVATE cxx_std_20)
target_link_libraries(
    raw_local_control_test PRIVATE
    PkgConfig::gstreamer
    PkgConfig::gstreamer-app
    Catch2::Catch2WithMain
)

catch_discover_tests(raw_local_control_test)
