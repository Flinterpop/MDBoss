#include "HelpDialog.h"

#include <wx/button.h>
#include <wx/statline.h>
#include <wx/statbmp.h>
#include <wx/hyperlink.h>
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

namespace {

// An icon resource scaled to `size`.  wxIcon loads at its natural size, so
// the round trip through wxImage is how a 16px studio mark is obtained from
// the same resource that supplies the larger app icon.
wxBitmap icon_at(const char* resource, int size)
{
    wxIcon icon;
    if (!icon.LoadFile(resource, wxBITMAP_TYPE_ICO_RESOURCE)) {
        return wxNullBitmap;
    }
    wxBitmap bitmap;
    bitmap.CopyFromIcon(icon);
    if (!bitmap.IsOk() || bitmap.GetWidth() == size) {
        return bitmap;
    }
    wxImage image = bitmap.ConvertToImage();
    image.Rescale(size, size, wxIMAGE_QUALITY_HIGH);
    return wxBitmap(image);
}

}  // namespace

// A dialog of its own rather than wxAboutBox, which supports exactly one
// icon.  Two are wanted here and they mean different things: the product
// icon identifies the application, the studio mark credits its author.
void show_about_box(wxWindow* parent)
{
    wxDialog dialog(parent, wxID_ANY, wxString("About ") + kAppName,
                    wxDefaultPosition, wxDefaultSize,
                    wxDEFAULT_DIALOG_STYLE);

    // Header: the PRODUCT icon, at the size a title normally carries.
    auto* header = new wxBoxSizer(wxHORIZONTAL);
    const wxBitmap product = icon_at("#1", 48);
    if (product.IsOk()) {
        header->Add(new wxStaticBitmap(&dialog, wxID_ANY, product), 0,
                    wxRIGHT | wxALIGN_TOP, 12);
    }
    auto* titles = new wxBoxSizer(wxVERTICAL);
    auto* name = new wxStaticText(&dialog, wxID_ANY,
                                  wxString(kAppName) + " " + kAppVersion);
    wxFont title_font = name->GetFont();
    title_font.MakeBold().MakeLarger();
    name->SetFont(title_font);
    titles->Add(name, 0);
    titles->Add(new wxStaticText(&dialog, wxID_ANY, kAppStage), 0, wxTOP, 2);
    header->Add(titles, 1, wxALIGN_TOP);

    auto* body = new wxStaticText(
        &dialog, wxID_ANY,
        L"A local Markdown manager, editor and offline GitHub-style viewer.\n\n"
        L"Rendering is entirely offline: mermaid, KaTeX and highlight.js are "
        L"bundled,\nand the preview is network-locked, so a document cannot "
        L"reach the network.\n\n"
        L"C++ / wxWidgets port of the Python application in the same "
        L"repository,\nwhich remains the reference implementation.");

    auto* link = new wxHyperlinkCtrl(&dialog, wxID_ANY,
                                     "github.com/Flinterpop/MDBoss",
                                     "https://github.com/Flinterpop/MDBoss");

    // Footer: the STUDIO mark, beside the attribution and smaller than the
    // product icon above it -- it credits the author, it does not identify
    // the application.
    auto* footer = new wxBoxSizer(wxHORIZONTAL);
    const wxBitmap studio = icon_at("#2", 32);
    if (studio.IsOk()) {
        footer->Add(new wxStaticBitmap(&dialog, wxID_ANY, studio), 0,
                    wxRIGHT | wxALIGN_CENTRE_VERTICAL, 6);
    }
    footer->Add(new wxStaticText(&dialog, wxID_ANY, kAttribution), 0,
                wxALIGN_CENTRE_VERTICAL);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(header, 0, wxEXPAND | wxALL, 16);
    sizer->Add(body, 0, wxLEFT | wxRIGHT, 16);
    sizer->Add(link, 0, wxLEFT | wxRIGHT | wxTOP, 16);
    sizer->Add(new wxStaticLine(&dialog), 0, wxEXPAND | wxALL, 12);
    sizer->Add(footer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16);
    sizer->Add(dialog.CreateStdDialogButtonSizer(wxOK), 0,
               wxALIGN_RIGHT | wxALL, 12);
    dialog.SetSizerAndFit(sizer);
    dialog.CentreOnParent();
    dialog.ShowModal();
}

}  // namespace mdboss
