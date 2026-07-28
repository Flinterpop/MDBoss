# Milestone 0 spike -- throwaway.  Delete once M2 has the real app shell, or
# keep it as the smallest reproducer if the Edge backend ever misbehaves.
#
# Built as a console app on purpose: it prints PASS/FAIL lines and exits with a
# status, so it can be run unattended.

find_package(wxWidgets CONFIG REQUIRED)

add_executable(mdboss_spike main.cpp)

target_link_libraries(mdboss_spike PRIVATE wx::core wx::base wx::webview)

# WRL's Callback<> and the WebView2 headers need the SDK's ComPtr; the loader
# is the static variant so the exe keeps needing no redistributable.
target_link_libraries(mdboss_spike PRIVATE
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/WebView2LoaderStatic.lib"
)
