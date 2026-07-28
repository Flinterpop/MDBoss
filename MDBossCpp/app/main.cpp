// MD Boss (C++ port) entry point.

#include <wx/app.h>
#include <wx/cmdline.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include <string>

#include "FileAssoc.h"
#include "MainFrame.h"
#include "SingleInstance.h"
#include "mdrender/MdRender.h"

namespace {

constexpr const char* kRegisterFlag = "--register-file-types";
constexpr const char* kUnregisterFlag = "--unregister-file-types";

// Handle the registration switches before any GUI exists: the installer runs
// them silently, and putting up a window would be wrong there.  Returns an
// exit code, or -1 to carry on and start normally.
int handle_registration_flags(int argc, wxChar** argv)
{
    for (int i = 1; i < argc; ++i) {
        const wxString arg(argv[i]);
        if (arg == kRegisterFlag) {
            const mdboss::RegPlan plan = mdboss::current_registration_plan();
            const bool ok = mdboss::apply_registration(plan);
            mdboss::notify_assoc_changed();
            return ok ? 0 : 1;
        }
        if (arg == kUnregisterFlag) {
            mdboss::remove_registration(mdboss::current_registration_plan());
            mdboss::notify_assoc_changed();
            return 0;
        }
    }
    return -1;
}

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

    // Returning false from OnInit() makes wxEntry return -1, which surfaces
    // as exit code 255 -- so a *successful* --register-file-types would look
    // like a failure to the installer running it, and a forwarded document
    // would look like a crash.  Instead OnInit() succeeds and OnRun() returns
    // the code we actually mean without entering the message loop.
    int OnRun() override
    {
        if (exit_immediately_) {
            return exit_code_;
        }
        return wxApp::OnRun();
    }

    bool OnInit() override
    {
        // Before wxApp::OnInit(), whose parser would reject these switches.
        const int registration = handle_registration_flags(argc, argv);
        if (registration >= 0) {
            exit_immediately_ = true;
            exit_code_ = registration;
            return true;
        }
        if (!wxApp::OnInit()) {
            return false;
        }
        locate_assets();

        // Windows hands an associated document to the shell handler as a
        // single argument, so only the first non-switch argument matters.
        std::string document;
        if (argc > 1) {
            const wxString first = argv[1];
            if (!first.StartsWith("-")) {
                document = std::string(first.ToUTF8());
            }
        }

        // One window per session: hand the document to the instance that is
        // already up rather than starting a rival that would overwrite its
        // settings on exit.
        if (mdboss::forward_to_running(document)) {
            exit_immediately_ = true;
            exit_code_ = 0;   // handing the document over is success
            return true;
        }

        auto* frame = new mdboss::MainFrame();
        frame->Show(true);
        mdboss::mark_as_instance(frame->GetHandle());

        if (!document.empty()) {
            frame->open_path(document);
        }
        return true;
    }

private:
    bool exit_immediately_ = false;
    int exit_code_ = 0;
};

}  // namespace

wxIMPLEMENT_APP(MDBossApp);
