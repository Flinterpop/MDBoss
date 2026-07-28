# Milestone 0 spike -- throwaway.  Delete once M2 has the real app shell, or
# keep it as the smallest reproducer if the Edge backend ever misbehaves.
#
# Built as a console app on purpose: it prints PASS/FAIL lines and exits with a
# status, so it can be run unattended.

find_package(wxWidgets CONFIG REQUIRED)

add_executable(mdboss_spike main.cpp)

target_link_libraries(mdboss_spike PRIVATE wx::core wx::base wx::webview)

set(MDBOSS_WEBVIEW2_LOADER
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/WebView2LoaderStatic.lib")

# WRL's Callback<> and the WebView2 headers need the SDK's ComPtr; the loader
# is the static variant so the exe keeps needing no redistributable.
target_link_libraries(mdboss_spike PRIVATE "${MDBOSS_WEBVIEW2_LOADER}")

# The discriminator: the same lock with wxWidgets removed, to tell a wrapper
# fault apart from a WebView2-usage fault.  Links no wx at all on purpose.
add_executable(mdboss_spike_win32 win32_probe.cpp)
target_include_directories(mdboss_spike_win32 PRIVATE
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
target_link_libraries(mdboss_spike_win32 PRIVATE "${MDBOSS_WEBVIEW2_LOADER}")

# The proposed M2 architecture: wxWidgets for every widget, but a WebView2 we
# create ourselves parented to a wxWindow, since wxWebView's Edge wrapper
# swallows the WebResourceRequested event the ITAR lock depends on.
add_executable(mdboss_spike_wxown wx_owned_webview.cpp)
target_link_libraries(mdboss_spike_wxown PRIVATE
    wx::core wx::base "${MDBOSS_WEBVIEW2_LOADER}")
