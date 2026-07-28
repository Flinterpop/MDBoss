// The networking half of the updater, kept apart from Updater.cpp so the
// parsing can be tested without linking wxWidgets into the test binary.
//
// This is the only outbound request the application makes and it carries no
// document data.

#include "Updater.h"

#include <wx/app.h>
#include <wx/webrequest.h>

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
        });
    request.Start();
}

}  // namespace mdboss
