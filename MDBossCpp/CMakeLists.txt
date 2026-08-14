# MD Boss -- C++ port.
#
# Parity pair: the Python app at the repo root is the reference implementation.
# Where the two disagree, the port is wrong.
#
# Dependencies come from vcpkg in CLASSIC mode, not a manifest.  A vcpkg.json
# here would switch the toolchain to manifest mode and rebuild wxWidgets from
# source into build/vcpkg_installed -- a ~30 minute detour for no gain, since
# the packages are already installed machine-wide.  Install them once with:
#
#   C:\vcpkg\vcpkg.exe install wxwidgets:x64-windows-static ^
#                              md4c:x64-windows-static catch2:x64-windows-static ^
#                              nlohmann-json:x64-windows-static
#
# Note what is NOT in that line: wxwidgets[webview].  The preview is a
# WebView2 this app creates and owns, because wxWebView's Edge backend never
# delivers WebResourceRequested and so cannot block a remote request -- see
# app/PreviewPane.h.  Installing the feature would offer a wx::webview that
# must not be used, which is worse than not having it.
#
# Then configure, build and test.  There is no CMakePresets.json -- these are
# the commands release.ps1 runs, so they are the ones kept working:
#
#   cmake -S . -B build
#   cmake --build build --config Release
#   ctest --test-dir build -C Release --output-on-failure

cmake_minimum_required(VERSION 3.21)

# Kept in step with app/Version.h by release.ps1; test_version.cpp enforces it.
project(MDBossCpp VERSION 1.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT so the shipped exe needs no VC++ redistributable.  Verify with
# `dumpbin /dependents` -- no VCRUNTIME / MSVCP / api-ms-win-crt.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# First-party code is warning-clean at /W4 /WX.  Vendored sources (none yet)
# would keep their own level.
add_compile_options(/W4 /WX /utf-8 /permissive-)

enable_testing()

find_package(nlohmann_json CONFIG REQUIRED)

add_subdirectory(mdrender)
add_subdirectory(app)
add_subdirectory(tests)

# The M0 spike needs wxWidgets, which the renderer library does not.  Off by
# default so a plain configure stays fast; turn it on with
# -DMDBOSS_BUILD_SPIKE=ON.
option(MDBOSS_BUILD_SPIKE "Build the milestone-0 wxWebViewEdge spike" OFF)
if(MDBOSS_BUILD_SPIKE)
    add_subdirectory(spike)
endif()
