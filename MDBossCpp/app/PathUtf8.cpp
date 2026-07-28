#include "PathUtf8.h"

namespace mdboss {

std::string path_to_utf8(const std::filesystem::path& path)
{
    // u8string() is defined to produce UTF-8 and cannot fail the way string()
    // does; the reinterpret is just char8_t -> char, same object
    // representation.
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()),
                       text.size());
}

std::filesystem::path path_from_utf8(std::string_view utf8)
{
    const std::u8string text(reinterpret_cast<const char8_t*>(utf8.data()),
                             utf8.size());
    return std::filesystem::path(text);
}

}  // namespace mdboss
