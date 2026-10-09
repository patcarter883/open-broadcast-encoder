# Fix a real bug in the vendored FLTK's fluid, applied as part of the external's
# build rather than committed into the submodule (which stays pristine, so a
# clone never depends on a local commit that cannot be fetched).
#
# Fl_Grid_Type::read_parent_property() chains its property tests with
#
#     } if (!strcmp(property, "location")) { ... } if (!strcmp(property, "minsize")) {
#
# The first of those must be "} else if (". As written, a grid child's
# parent_properties block is mis-parsed: fluid prints
#
#     ui_widgets.fld:235: Unknown parent property "1 1"
#
# for every one of them. The generated C++ happens to come out the same, but the
# property never lands in fluid's model -- so opening and SAVING the design file
# in fluid would silently drop every grid cell location, which is exactly the
# fault that made the Stats labels drift on resize.
#
# Idempotent: it applies the fix if the bug is present, accepts an
# already-fixed file, and fails loudly if the line has moved.

if(NOT DEFINED FLTK_SOURCE_DIR)
  message(FATAL_ERROR "FLTK_SOURCE_DIR must be set (the vendored FLTK source dir).")
endif()

set(SRC "${FLTK_SOURCE_DIR}/fluid/Fl_Grid_Type.cxx")
if(NOT EXISTS "${SRC}")
  message(FATAL_ERROR "not found: ${SRC}")
endif()

file(READ "${SRC}" content)

set(BAD  "  } if (!strcmp(property, \"minsize\")) {")
set(GOOD "  } else if (!strcmp(property, \"minsize\")) {")

string(FIND "${content}" "${GOOD}" already)
if(NOT already EQUAL -1)
  message(STATUS "fluid grid reader already carries the fix")
  return()
endif()

string(FIND "${content}" "${BAD}" at)
if(at EQUAL -1)
  message(FATAL_ERROR
    "Cannot patch fluid's grid reader: neither the buggy line nor the fixed "
    "line is present in ${SRC}. The vendored FLTK has changed; re-check "
    "Fl_Grid_Type::read_parent_property() before removing this patch.")
endif()

string(REPLACE "${BAD}" "${GOOD}" content "${content}")
file(WRITE "${SRC}" "${content}")
message(STATUS "patched fluid's grid reader (upstream FLTK bug)")
