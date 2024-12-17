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

# ---- End-of-file commands ----

add_folders(Test)
