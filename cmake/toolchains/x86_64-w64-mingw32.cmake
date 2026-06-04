# Cross-compilation toolchain: Linux → Windows x86_64 via MinGW-w64
#
# Prerequisites (Arch/CachyOS):
#   sudo pacman -S mingw-w64-gcc
#
# Required environment variables before configuring:
#   VCPKG_ROOT              — path to vcpkg checkout (same as native builds)
#   NDI_SDK_DIR             — path to the Windows NDI SDK extracted on this host
#                             (download from ndi.video; extract the .exe with 7z)
#   GSTREAMER_MINGW_PREFIX  — root of the GStreamer MinGW dev package, e.g.
#                             /opt/gstreamer/1.0/mingw_x86_64
#                             (download from gstreamer.freedesktop.org Windows pkg)
#
# Usage:
#   cmake --preset cross-win64
#   cmake --build build/cross-win64

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)
set(CMAKE_AR           x86_64-w64-mingw32-ar)
set(CMAKE_RANLIB       x86_64-w64-mingw32-ranlib)
set(CMAKE_STRIP        x86_64-w64-mingw32-strip)

# Never use host binaries when searching for libraries/headers/packages.
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# BOTH lets vcpkg's install tree be searched alongside the sysroot.
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# Point pkg-config at the GStreamer Windows dev tree, not the host system.
# The pkg-config wrapper from mingw-w64-pkg-config (AUR) handles sysroot
# rewriting automatically; fall back to the host binary + manual path if absent.
find_program(_mingw_pkg_config x86_64-w64-mingw32-pkg-config)
if(_mingw_pkg_config)
  set(PKG_CONFIG_EXECUTABLE "${_mingw_pkg_config}" CACHE FILEPATH "" FORCE)
else()
  message(WARNING
    "x86_64-w64-mingw32-pkg-config not found. "
    "Install mingw-w64-pkg-config (AUR) or set PKG_CONFIG_PATH manually to "
    "\$GSTREAMER_MINGW_PREFIX/lib/pkgconfig before running cmake."
  )
endif()

if(DEFINED ENV{GSTREAMER_MINGW_PREFIX})
  set(_gst_prefix "$ENV{GSTREAMER_MINGW_PREFIX}")
  list(APPEND CMAKE_PREFIX_PATH "${_gst_prefix}")

  # x86_64-w64-mingw32-pkg-config overrides PKG_CONFIG_PATH internally and
  # only prepends PKG_CONFIG_PATH_CUSTOM to its search list.  Set that so our
  # MSYS2 packages are found, and also set PKG_CONFIG_LIBDIR to exclude the
  # host MinGW sysroot packages (which would pull in /usr/x86_64-w64-mingw32
  # paths that cmake then complains about as non-existent).
  set(ENV{PKG_CONFIG_PATH_CUSTOM} "${_gst_prefix}/lib/pkgconfig")
  # PKG_CONFIG_LIBDIR replaces the default search dirs entirely.
  set(ENV{PKG_CONFIG_LIBDIR} "${_gst_prefix}/lib/pkgconfig:${_gst_prefix}/share/pkgconfig")
  # No sysroot rewriting needed: the .pc files' prefix= is already patched to
  # absolute host paths by the cross-win64 setup script.
  set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
endif()
