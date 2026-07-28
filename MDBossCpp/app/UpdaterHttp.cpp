// The networking half of the updater, kept apart from Updater.cpp so the
// parsing can be tested without linking wxWidgets into the test binary.
//
// This is the only outbound request the application makes and it carries no
// document data.

#include "Updater.h"

#include <wx/app.h>
#include <wx/webrequest.h>

#include <cassert>
#include <filesystem>
#include <system_error>

#include "PathUtf8.h"
#include "Version.h"

namespace mdboss {
namespace {

const char* const kApiUrl =
    "https://api.github.com/repos/Flinterpop/MDBoss/releases/latest";

}  // namespace

void check_for_update(
    std::function<void(const ReleaseInfo&, const std::string&)> done)
{
    // wxWebRequest needs an event handler to report to; the app object
    // outlives any window, so it is the safe owner here.
    wxEvtHandler* handler = wxTheApp;
    wxWebRequest request =
        wxWebSession::GetDefault().CreateRequest(handler, kApiUrl);
    if (!request.IsOk()) {
        done(ReleaseInfo{}, "Could not start the update check.");
        return;
    }
    // GitHub rejects requests without a User-Agent.
    request.SetHeader("User-Agent", kAppName);
    request.SetHeader("Accept", "application/vnd.github+json");

    // Bound to THIS request's id, not to the event generally.  Every request
    // reports to the same handler, so an unfiltered binding would also see
    // the installer download's events and try to parse an .exe as a release
    // payload -- and would fire again for every later check, because these
    // bindings are never removed.
    handler->Bind(
        wxEVT_WEBREQUEST_STATE,
        [done](wxWebRequestEvent& event) {
            switch (event.GetState()) {
            case wxWebRequest::State_Completed: {
                const int status = event.GetResponse().GetStatus();
                if (status != 200) {
                    done(ReleaseInfo{},
                         "GitHub returned status " + std::to_string(status) +
                             ".");
                    return;
                }
                done(parse_release(
                         std::string(event.GetResponse().AsString().ToUTF8())),
                     std::string());
                return;
            }
            case wxWebRequest::State_Failed:
                done(ReleaseInfo{},
                     std::string(event.GetErrorDescription().ToUTF8()));
                return;
            case wxWebRequest::State_Cancelled:
                done(ReleaseInfo{}, "The update check was cancelled.");
                return;
            default:
                return;   // Active / Idle / Unauthorized: nothing to do yet
            }
        },
        request.GetId());
    request.Start();
}

void download_update(const std::string& url, const std::string& dest,
                     std::function<void(const std::string&)> done)
{
    assert(!url.empty() && !dest.empty() && "download needs both ends");
    wxEvtHandler* handler = wxTheApp;
    wxWebRequest request = wxWebSession::GetDefault().CreateRequest(
        handler, wxString::FromUTF8(url));
    if (!request.IsOk()) {
        done("Could not start the download.");
        return;
    }
    request.SetHeader("User-Agent", kAppName);
    // Write straight to the file rather than buffering the installer in
    // memory: it is several megabytes and there is no reason to hold it.
    request.SetStorage(wxWebRequest::Storage_File);

    handler->Bind(
        wxEVT_WEBREQUEST_STATE,
        [done, dest](wxWebRequestEvent& event) {
            switch (event.GetState()) {
            case wxWebRequest::State_Completed: {
                const int status = event.GetResponse().GetStatus();
                if (status != 200) {
                    done("The download returned status " +
                         std::to_string(status) + ".");
                    return;
                }
                // wx wrote it to a temporary of its choosing; move it where
                // the batch expects.  A rename can cross volumes here, so
                // copy-then-remove rather than assuming it cannot.
                const wxString from = event.GetDataFile();
                std::error_code ec;
                const std::filesystem::path target = path_from_utf8(dest);
                std::filesystem::copy_file(
                    path_from_utf8(std::string(from.ToUTF8())), target,
                    std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) {
                    done("Could not save the installer: " + ec.message());
                    return;
                }
                done(std::string());
                return;
            }
            case wxWebRequest::State_Failed:
                done(std::string(event.GetErrorDescription().ToUTF8()));
                return;
            case wxWebRequest::State_Cancelled:
                done("The download was cancelled.");
                return;
            default:
                return;
            }
        },
        request.GetId());
    request.Start();
}

}  // namespace mdboss
