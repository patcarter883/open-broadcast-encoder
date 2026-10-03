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

# ---- End-of-file commands ----

add_folders(Test)
