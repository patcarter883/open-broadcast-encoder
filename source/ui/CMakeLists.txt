cmake_minimum_required(VERSION 3.28)

set(CMP0155 NEW)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

add_compile_options(
"$<$<COMPILE_LANGUAGE:CXX>:-pthread>"
)

project(
    open-broadcast-encoder-ui-lib  LANGUAGES CXX
)

add_library(
    ui_lib STATIC
)

target_sources(ui_lib ${warning_guard}
  PUBLIC
    FILE_SET CXX_MODULES FILES
    ui.cppm
)

target_compile_features(ui_lib PUBLIC cxx_std_20)

target_link_libraries(ui_lib PRIVATE
open-broadcast-encoder_lib
fltk::fltk
)

target_include_directories(ui_lib ${warning_guard} PRIVATE ${GSTREAMER_INCLUDE_DIRS})