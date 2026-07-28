// MD Boss (C++ port) entry point.

#include <wx/app.h>
#include <wx/cmdline.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include <string>

#include "MainFrame.h"
#include "mdrender/MdRender.h"

namespace {

// The bundled render assets live beside the executable once installed, and
// two levels up from the build tree when running from a build directory.
// Trying both keeps `cmake --build` output runnable without an install step.
void locate_assets()
{
    const wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    const wxString candidates[] = {
        exe.GetPath() + "\\assets",
        exe.GetPath() + "\\..\\..\\..\\..\\assets",
        exe.GetPath() + "\\..\\..\\..\\..\\..\\assets",
    };
    for (const wxString& candidate : candidates) {
        wxFileName dir(candidate + "\\");
        dir.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
        if (mdrender::set_asset_dir(std::string(dir.GetPath().ToUTF8()))) {
            return;
        }
    }
    // Left unset: render_document() then returns an empty page rather than a
    // half-built one, which is visible rather than silently wrong.
}

class MDBossApp : public wxApp {
public:
    // wx's default parser rejects any positional argument it was not told
    // about, and wxApp::OnInit() then returns false -- so the app would exit
    // silently for exactly the case that matters most: a .md file handed to
    // it by the shell as the default handler.  Declaring the parameter is
    // what makes "open with MD Boss" work at all.
    void OnInitCmdLine(wxCmdLineParser& parser) override
    {
        wxApp::OnInitCmdLine(parser);
        parser.AddParam("Markdown file to open", wxCMD_LINE_VAL_STRING,
                        wxCMD_LINE_PARAM_OPTIONAL);
    }

    bool OnInit() override
    {
        if (!wxApp::OnInit()) {
            return false;
        }
        locate_assets();

        auto* frame = new mdboss::MainFrame();
        frame->Show(true);

        // Windows hands an associated document to the shell handler as a
        // single argument, so only the first non-switch argument matters.
        if (argc > 1) {
            const wxString first = argv[1];
            if (!first.StartsWith("-")) {
                frame->open_path(std::string(first.ToUTF8()));
            }
        }
        return true;
    }
};

}  // namespace

wxIMPLEMENT_APP(MDBossApp);
