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

# ---------------------------------------------------------------------------
# The UI is generated from the design file.
#
# source/ui/ui.fld is the source of truth for the widget tree: every panel,
# row, label, output and button, its geometry, and the Fl_Flex/Fl_Grid layout
# properties (margin, gap, fixed child sizes, grid dimensions and cell
# locations). fluid turns it into ui_widgets.h (the widget members) and
# ui_widgets.cpp (the constructor that builds them).
#
# ui.h/ui.cpp stay hand-written and hold everything that is NOT the tree: the
# callbacks, the thread-safe setters, and the logic. They declare
# `class user_interface : public ui_widgets`, so the generated constructor runs
# first and the hand-written constructor body can then do what fluid cannot
# express (window size_range, for one).
#
# Edit the layout in fluid -- not by hand in the generated files, which are
# overwritten on every build.
# ---------------------------------------------------------------------------

# Which fluid generates the UI.
#
# It cannot be found with find_program() here: a native build gets fluid from
# the FLTK external, which is built at BUILD time, so at configure time the
# binary does not exist yet and a "not found" check would fail every time on a
# fresh clone. So the default is simply where that external installs it, and the
# dependency below orders the build correctly.
#
# Cross-compiling cannot use that one -- it would be a Windows binary -- so the
# cross lane passes a host fluid explicitly, e.g. -DFLUID_EXECUTABLE=/path/to/fluid
set(FLUID_EXECUTABLE "" CACHE FILEPATH
  "Host fluid used to generate the UI from ui.fld. Native builds leave this \
empty and use the fluid built by the FLTK external.")

if(FLUID_EXECUTABLE)
  if(NOT EXISTS "${FLUID_EXECUTABLE}")
    message(FATAL_ERROR
      "FLUID_EXECUTABLE was set to ${FLUID_EXECUTABLE}, which does not exist.")
  endif()
  message(STATUS "fluid (provided): ${FLUID_EXECUTABLE}")
else()
  if(NOT FLTK_INSTALL_DIR)
    message(FATAL_ERROR
      "FLTK_INSTALL_DIR is not set, so the fluid that generates the UI cannot "
      "be located. source/ui/CMakeLists.txt relies on the FLTK external having "
      "been declared before this directory is added.")
  endif()
  set(FLUID_EXECUTABLE "${FLTK_INSTALL_DIR}/bin/fluid")
  # The file does not exist yet; it is produced by the FLTK external, and this
  # dependency is what guarantees it exists before the command below runs.
  set(UI_FLUID_IS_EXTERNAL TRUE)
  message(STATUS "fluid (from the FLTK external): ${FLUID_EXECUTABLE}")
endif()

set(UI_FLD  "${CMAKE_CURRENT_SOURCE_DIR}/ui_widgets.fld")
set(UI_GEN_H   "${CMAKE_CURRENT_SOURCE_DIR}/ui_widgets.h")
set(UI_GEN_CPP "${CMAKE_CURRENT_SOURCE_DIR}/ui_widgets.cpp")

add_custom_command(
  OUTPUT ${UI_GEN_H} ${UI_GEN_CPP}
  COMMAND ${FLUID_EXECUTABLE} -c ${UI_FLD}
  # fluid names its output after the .fld it is given: ui_widgets.fld generates
  # ui_widgets.h + ui_widgets.cpp, which is why the design file is named that and
  # not ui.fld -- "ui.fld" would generate ui.h + ui.cxx and overwrite the
  # hand-written ui.h. It must also run in the directory the .fld lives in, which
  # is why the generated files land beside it.
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${UI_FLD}
  COMMENT "Generating the UI (ui_widgets.h, ui_widgets.cpp) from ui_widgets.fld"
  VERBATIM
)

target_sources(ui_lib ${warning_guard}
  PUBLIC
    ui.cpp
    ${UI_GEN_CPP}
)

# fluid itself comes out of the FLTK external, so the external has to be built
# before this target can generate anything. A target-level dependency is ordered
# ahead of this target's own build, which is what makes the custom command above
# safe to run without a configure-time existence check.
if(UI_FLUID_IS_EXTERNAL)
  add_dependencies(ui_lib external_fltk)
endif()

# ui_widgets.h is generated, so it does not exist at configure time; the
# include path is the source dir (already PUBLIC on this target), and this keeps
# CMake from trying to configure in a header that is not there yet.
set_source_files_properties(${UI_GEN_H} ${UI_GEN_CPP} PROPERTIES GENERATED TRUE)

target_compile_features(ui_lib PUBLIC cxx_std_20)

target_link_libraries(ui_lib PUBLIC
fltk::fltk
)

target_link_libraries(ui_lib PRIVATE
open-broadcast-encoder_lib
)

target_include_directories(ui_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/source
)

target_include_directories(ui_lib PRIVATE
    ${GSTREAMER_INCLUDE_DIRS}
)