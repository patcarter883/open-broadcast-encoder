cmake_minimum_required(VERSION 3.28)

project(
    settings-lib
    LANGUAGES CXX
)

# ---- Declare library ----

add_library(settings_lib STATIC)

target_sources(settings_lib ${warning_guard}
  PUBLIC
    settings.h
    settings.cpp
)

target_include_directories(settings_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/source
)

target_compile_features(settings_lib PUBLIC cxx_std_20)

target_link_libraries(settings_lib PRIVATE
    backplane_lib
    open-broadcast-encoder_lib
    nlohmann_json::nlohmann_json
)
