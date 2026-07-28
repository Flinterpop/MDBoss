find_package(wxWidgets CONFIG REQUIRED)

# WIN32 => no console window for the shipped app.
add_executable(MDBoss WIN32
    main.cpp
    MainFrame.cpp
    PreviewPane.cpp
    FileTreePanel.cpp
    FileScan.cpp
    PathUtf8.cpp
    OutlinePanel.cpp
    PathListPanel.cpp
    FoldersDialog.cpp
    HelpDialog.cpp
    FileAssoc.cpp
    SingleInstance.cpp
    Templates.cpp
    DropTarget.cpp
    DocumentWatcher.cpp
    Updater.cpp
    UpdaterHttp.cpp
    Config.cpp
    MDBoss.rc        # version resource + application icon
    MDBoss.manifest   # per-monitor DPI awareness; see the file for why
)

target_link_libraries(MDBoss PRIVATE
    mdrender
    wx::core
    wx::base
    wx::net        # wxWebRequest, for the update check
    wx::stc          # Scintilla: the editor gutter the Python app hand-paints
    nlohmann_json::nlohmann_json
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/WebView2LoaderStatic.lib"
)

# Deliberately NOT wx::webview -- see PreviewPane.h.  Its Edge backend never
# delivers WebResourceRequested, which silently defeats the ITAR network lock.

set_target_properties(MDBoss PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.."
)
