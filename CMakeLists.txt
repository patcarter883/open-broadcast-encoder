cmake_minimum_required(VERSION 3.14)

include(cmake/prelude.cmake)
include(ExternalProject)
include(FetchContent)

project(
    open-broadcast-encoder
    VERSION 0.1.0
    DESCRIPTION "Streaming Broadcast Video Encoder"
    HOMEPAGE_URL "https://example.com/"
    LANGUAGES CXX
)

include(cmake/project-is-top-level.cmake)
include(cmake/variables.cmake)

# ---- Declare library ----

add_library(
    open-broadcast-encoder_lib OBJECT
    source/lib.cpp
)

target_include_directories(
    open-broadcast-encoder_lib ${warning_guard}
    PUBLIC
    "\$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/source>"
)

target_compile_features(open-broadcast-encoder_lib PUBLIC cxx_std_20)

find_package(fmt REQUIRED)
find_package(FLTK CONFIG REQUIRED)

target_link_libraries(open-broadcast-encoder_lib PRIVATE fmt::fmt fltk fltk_gl fltk_forms fltk_images)



# ---- Declare executable ----

add_executable(open-broadcast-encoder_exe source/main.cpp)
add_executable(open-broadcast-encoder::exe ALIAS open-broadcast-encoder_exe)

set_property(TARGET open-broadcast-encoder_exe PROPERTY OUTPUT_NAME open-broadcast-encoder)

target_compile_features(open-broadcast-encoder_exe PRIVATE cxx_std_20)

target_link_libraries(open-broadcast-encoder_exe PRIVATE open-broadcast-encoder_lib)

#Include libristnet

FetchContent_Declare(
  rist-cpp
  GIT_REPOSITORY https://github.com/patcarter883/rist-cpp.git
  GIT_TAG swxtch
)

FetchContent_MakeAvailable(rist-cpp)

target_link_libraries(open-broadcast-encoder_exe PRIVATE open-broadcast-encoder_lib ristnet)
target_include_directories(open-broadcast-encoder_exe PUBLIC ${rist-cpp_SOURCE_DIR})


# ExternalProject_Add(project_libristnet
#         GIT_REPOSITORY https://github.com/patcarter883/rist-cpp.git
#         GIT_TAG swxtch
#         SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ristwrap
#         BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ristwrap
#         GIT_PROGRESS 1
#         BUILD_COMMAND cmake --build ${CMAKE_CURRENT_SOURCE_DIR}/ristwrap --config ${CMAKE_BUILD_TYPE} --target ristnet
#         STEP_TARGETS build
#         EXCLUDE_FROM_ALL TRUE
#         INSTALL_COMMAND ""
#         )
# add_library(ristnet STATIC IMPORTED)
# set_property(TARGET ristnet PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/ristwrap/libristnet.a)
# add_dependencies(ristnet project_libristnet)

# #Pass information about where to find headers
# include_directories(${CMAKE_CURRENT_SOURCE_DIR}/ristwrap/)
# include_directories(${CMAKE_CURRENT_SOURCE_DIR}/ristwrap/rist/inc/)

# ---- Install rules ----

if(NOT CMAKE_SKIP_INSTALL_RULES)
  include(cmake/install-rules.cmake)
endif()

# ---- Developer mode ----

if(NOT open-broadcast-encoder_DEVELOPER_MODE)
  return()
elseif(NOT PROJECT_IS_TOP_LEVEL)
  message(
      AUTHOR_WARNING
      "Developer mode is intended for developers of open-broadcast-encoder"
  )
endif()

include(cmake/dev-mode.cmake)