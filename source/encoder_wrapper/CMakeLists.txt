add_library(
  encoder_wrapper SHARED
  encoder_wrapper.cpp
  encoder_wrapper.h
)

target_include_directories(encoder_wrapper PRIVATE ${CMAKE_SOURCE_DIR}/source)

target_link_libraries(
  encoder_wrapper
  PRIVATE
    open_broadcast_lib
    encode
    transport
    ndi_input
    statistics_aggregator
    GStreamer::gstreamer
    GStreamer::gstapp
)

# RPATH handling for runtime library discovery
set_target_properties(
  encoder_wrapper
  PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_RPATH "$ORIGIN"
    SOVERSION 1
    VERSION 1.0.0
)

# Install library to build directory for Tauri access
if(BUILD_TAURI)
  add_custom_command(
    TARGET encoder_wrapper
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:encoder_wrapper>
      ${CMAKE_BINARY_DIR}/src-tauri/
    COMMENT "Copying encoder_wrapper library to src-tauri/"
  )
endif()
