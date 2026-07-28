find_package(Catch2 CONFIG REQUIRED)

# FileScan is deliberately wx-free so it can be compiled straight into the
# test binary; everything else in app/ needs a GUI toolkit.
add_executable(mdrender_tests
    test_mdrender.cpp
    test_filescan.cpp
    ../app/FileScan.cpp
)

target_include_directories(mdrender_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../app)
target_link_libraries(mdrender_tests PRIVATE mdrender Catch2::Catch2WithMain)

# The golden corpus is read at run time rather than embedded, so regenerating
# it with make_golden.py does not require a rebuild.
target_compile_definitions(mdrender_tests PRIVATE
    MDBOSS_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
    MDBOSS_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../assets"
)

include(Catch)
catch_discover_tests(mdrender_tests)
