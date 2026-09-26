#include "AppIdentity.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>

#include "PathUtf8.h"

namespace mdboss {
namespace {

AppIdentity& identity_storage()
{
    static AppIdentity identity;
    return identity;
}

// getenv() is deprecated under /W4 /WX on MSVC, and the _s variant hands back
// an allocation the caller owns.
std::string environment(const char* name)
{
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return {};
    }
    std::string out(value);
    std::free(value);
    return out;
}

std::string user_data_base()
{
    const std::string appdata = environment("APPDATA");
    if (!appdata.empty()) {
        return appdata;
    }
    const std::string profile = environment("USERPROFILE");
    return profile.empty() ? std::string(".") : profile;
}

}  // namespace

const AppIdentity& app_identity()
{
    return identity_storage();
}

void set_app_identity(const AppIdentity& identity)
{
    assert(!identity.display_name.empty() && "an identity needs a name");
    assert(!identity.instance_mutex.empty() &&
           "two apps sharing one mutex would each refuse to start");
    identity_storage() = identity;
}

std::string user_data_dir()
{
    const AppIdentity& identity = app_identity();
    if (!identity.data_dir.empty()) {
        return identity.data_dir;
    }
    const std::string out =
        path_to_utf8(path_from_utf8(user_data_base()) / "MDBoss");
    assert(!out.empty() && "the data folder always resolves to something");
    return out;
}

}  // namespace mdboss
