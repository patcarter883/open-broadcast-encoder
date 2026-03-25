cmake_minimum_required(VERSION 3.28)


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
    ui.cpp
    output_stream_widget.h
    output_stream_widget.cpp
    add_output_dialog.h
    add_output_dialog.cpp
)

target_compile_features(ui_lib PUBLIC cxx_std_20)

target_link_libraries(ui_lib PUBLIC
fltk::fltk
)

target_link_libraries(ui_lib PRIVATE
open-broadcast-encoder_lib
transport_lib
)

target_include_directories(ui_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/source
)

target_include_directories(ui_lib PRIVATE
    ${GSTREAMER_INCLUDE_DIRS}
)