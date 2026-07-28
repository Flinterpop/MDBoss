#include "HelpDialog.h"

#include <wx/aboutdlg.h>
#include <wx/button.h>
#include <wx/filename.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>

#include <fstream>
#include <sstream>

#include "PathUtf8.h"
#include "PreviewPane.h"
#include "Version.h"
#include "mdrender/MdRender.h"

namespace mdboss {
namespace {

// Where HELP.md sits relative to the executable: beside it once installed,
// and a few levels up when running straight out of the build tree.
const char* const kHelpCandidates[] = {
    "HELP.md",
    "..\\HELP.md",
    "..\\..\\..\\..\\HELP.md",
    "..\\..\\..\\..\\..\\HELP.md",
};

}  // namespace

std::string find_help_document()
{
    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    for (const char* candidate : kHelpCandidates) {
        wxFileName probe(exe.GetPath() + "\\" + candidate);
        probe.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
        if (probe.FileExists()) {
            return std::string(probe.GetFullPath().ToUTF8());
        }
    }
    return {};
}

HelpDialog::HelpDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, wxString(kAppName) + " Help",
               wxDefaultPosition, wxSize(760, 720),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    auto* preview = new PreviewPane(this);

    auto* footer = new wxBoxSizer(wxHORIZONTAL);
    footer->Add(new wxStaticText(this, wxID_ANY,
                                 wxString(kAppName) + " v" + kAppVersion +
                                     L"  ·  " + kAppStage),
                0, wxALIGN_CENTRE_VERTICAL);
    footer->AddStretchSpacer(1);
    footer->Add(new wxButton(this, wxID_CLOSE, "&Close"), 0);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(preview, 1, wxEXPAND);
    sizer->Add(footer, 0, wxEXPAND | wxALL, 8);
    SetSizer(sizer);

    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); },
         wxID_CLOSE);
    // Escape only dismisses a dialog whose escape id is present as a button,
    // and by default that is wxID_CANCEL -- which this dialog does not have.
    // Without this the window can only be closed by mouse.
    SetEscapeId(wxID_CLOSE);

    const std::string path = find_help_document();
    std::string text;
    std::string base = "file:///";
    if (!path.empty()) {
        std::ifstream stream(path_from_utf8(path), std::ios::binary);
        std::ostringstream buffer;
        buffer << stream.rdbuf();
        text = buffer.str();

        const std::u8string dir =
            path_from_utf8(path).parent_path().generic_u8string();
        base = "file:///" +
               std::string(reinterpret_cast<const char*>(dir.data()),
                           dir.size()) +
               "/";
    }
    if (text.empty()) {
        // Says which document is missing rather than rendering a blank page.
        text = std::string("# ") + kAppName + "\n\nVersion " + kAppVersion +
               ".\n\nHELP.md was not found beside the application.\n";
    }

    preview->show_page(mdrender::render_document(
        text, base, std::string(kAppName) + " Help", false));
}

void show_about_box(wxWindow* parent)
{
    wxAboutDialogInfo info;
    info.SetName(kAppName);
    // Version alone: kAppStage already carries its own parentheses, and
    // wrapping it again read as "1.0.0 (C++ port (in development))".
    info.SetVersion(kAppVersion);
    // Every piece wide, so no fragment can quietly be an ANSI-decoded narrow
    // literal (the em-dash below was exactly that on the first attempt).
    info.SetDescription(
        wxString(kAppStage) +
        L" — not yet at parity with the Python build.\n\n"
        L"A local Markdown manager, editor and offline GitHub-style viewer.\n\n"
        L"Rendering is entirely offline: mermaid, KaTeX and highlight.js are "
        L"bundled,\nand the preview is network-locked, so a document cannot "
        L"reach the network.\n\n"
        L"C++ / wxWidgets port of the Python application in the same "
        L"repository,\nwhich remains the reference implementation.");
    info.SetWebSite("https://github.com/Flinterpop/MDBoss");
    wxAboutBox(info, parent);
}

}  // namespace mdboss
