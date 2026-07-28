// Lossless conversion between std::filesystem::path and UTF-8 std::string.
//
// Everything in this app treats std::string as UTF-8 -- it is what the config
// file holds, what wxString::FromUTF8 expects, and what the renderer emits.
// std::filesystem does NOT: path::string() converts to the current ANSI code
// page and *throws* if a character has no mapping there, and constructing a
// path from a narrow string interprets it as ANSI rather than UTF-8.
//
// That is not theoretical.  A real document folder containing one filename
// with a non-ANSI character made path::string() throw
// "No mapping for the Unicode character exists in the target multi-byte code
// page" from inside a worker thread, which took the whole application down.
//
// Use these two functions at every boundary; never path::string() on a path
// that came from, or is going to, the rest of the app.

#ifndef MDBOSS_APP_PATH_UTF8_H
#define MDBOSS_APP_PATH_UTF8_H

#include <filesystem>
#include <string>
#include <string_view>

namespace mdboss {

std::string path_to_utf8(const std::filesystem::path& path);
std::filesystem::path path_from_utf8(std::string_view utf8);

}  // namespace mdboss

#endif  // MDBOSS_APP_PATH_UTF8_H
