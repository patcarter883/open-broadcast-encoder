# Building with CMake

## Dependencies

For a list of dependencies, please refer to [vcpkg.json](vcpkg.json).

### NDI support (optional, off by default)

The proprietary NDI SDK is **not required** to build this project. The NDI
input source (`source/ndi_input/`) and the NDI encode pipeline
(`source/encode/encode.cpp`) use GStreamer's `ndisrc` / `ndisrcdemux`
elements, which load the official NDI runtime dynamically at run time. No
translation unit includes or calls the SDK, so the default build links nothing
NDI and Configure prints:

```
-- NDI support: OFF
```

To use NDI input at run time you need the GStreamer NDI plugin and the official
NDI runtime installed — that is a runtime concern, independent of this option.

Opt in only when you are adding code that calls the SDK directly:

```sh
# Linux/Windows: point NDI_SDK_DIR at the extracted SDK
cmake -S . -B build -D OBC_ENABLE_NDI=ON -D NDI_SDK_DIR=/path/to/NDI_SDK
# macOS: uses /Library/NDI SDK for Apple automatically
cmake -S . -B build -D OBC_ENABLE_NDI=ON
```

With `OBC_ENABLE_NDI=ON`, `find_package(NDI)` becomes `REQUIRED`, the SDK's
include/library directories are added, and the `OBC_ENABLE_NDI` compile
definition is set so `#ifdef OBC_ENABLE_NDI` can fence direct-SDK code. When
`OFF` (the default) neither the include nor the library is used, and no SDK
needs to be present.

## Build

This project doesn't require any special command-line flags to build to keep
things simple.

Here are the steps for building in release mode with a single-configuration
generator, like the Unix Makefiles one:

```sh
cmake -S . -B build -D CMAKE_BUILD_TYPE=Release
cmake --build build
```

Here are the steps for building in release mode with a multi-configuration
generator, like the Visual Studio ones:

```sh
cmake -S . -B build
cmake --build build --config Release
```

### Building with MSVC

Note that MSVC by default is not standards compliant and you need to pass some
flags to make it behave properly. See the `flags-msvc` preset in the
[CMakePresets.json](CMakePresets.json) file for the flags and with what
variable to provide them to CMake during configuration.

### Building on Apple Silicon

CMake supports building on Apple Silicon properly since 3.20.1. Make sure you
have the [latest version][1] installed.

## Install

This project doesn't require any special command-line flags to install to keep
things simple. As a prerequisite, the project has to be built with the above
commands already.

The below commands require at least CMake 3.15 to run, because that is the
version in which [Install a Project][2] was added.

Here is the command for installing the release mode artifacts with a
single-configuration generator, like the Unix Makefiles one:

```sh
cmake --install build
```

Here is the command for installing the release mode artifacts with a
multi-configuration generator, like the Visual Studio ones:

```sh
cmake --install build --config Release
```

[1]: https://cmake.org/download/
[2]: https://cmake.org/cmake/help/latest/manual/cmake.1.html#install-a-project
