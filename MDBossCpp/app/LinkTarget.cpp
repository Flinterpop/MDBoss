#include "LinkTarget.h"

#include <algorithm>
#include <cctype>

namespace mdboss {
namespace {

// The same list the open dialog offers, kept in step with app.py's
// MARKDOWN_EXTS.
bool is_markdown_extension(const std::string& lowered_ext)
{
    return lowered_ext == ".md" || lowered_ext == ".markdown" ||
           lowered_ext == ".mdown" || lowered_ext == ".mkd" ||
           lowered_ext == ".mdwn";
}

int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

}  // namespace

std::string markdown_path_from_file_url(std::string_view uri)
{
    // Three slashes: `file:///C:/x.md` is a local path, while
    // `file://server/share/x.md` is UNC and has never been followed.
    const std::string_view prefix = "file:///";
    if (uri.size() <= prefix.size() ||
        uri.compare(0, prefix.size(), prefix) != 0) {
        return {};
    }

    std::string path;
    path.reserve(uri.size());
    // Bounded by the URL.  Every iteration consumes at least one character.
    for (std::size_t i = prefix.size(); i < uri.size(); ++i) {
        const char ch = uri[i];
        if (ch == '#' || ch == '?') {
            break;   // a fragment or a query is not part of the file name
        }
        if (ch == '%' && i + 2 < uri.size()) {
            const int hi = hex_value(uri[i + 1]);
            const int lo = hex_value(uri[i + 2]);
            if (hi >= 0 && lo >= 0) {
                const int value = hi * 16 + lo;
                if (value == 0) {
                    return {};   // an embedded NUL truncates a path silently
                }
                // Decoded as a BYTE, not as a character: a path outside ASCII
                // arrives percent-encoded as UTF-8, and one byte per wide
                // character would turn an accented letter into two wrong ones
                // and name a file that does not exist.
                path += static_cast<char>(static_cast<unsigned char>(value));
                i += 2;
                continue;
            }
            // A stray '%' that is not an escape is kept verbatim; Windows
            // allows it in a file name.
        }
        if (ch == '\0') {
            return {};
        }
        path += (ch == '/') ? '\\' : ch;
    }

    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return {};
    }
    // A dot in a FOLDER name is not an extension: a path ending "a.b/readme"
    // has none at all, and reading ".b/readme" as one would be nonsense.
    const std::size_t slash = path.find_last_of('\\');
    if (slash != std::string::npos && slash > dot) {
        return {};
    }
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (!is_markdown_extension(ext)) {
        return {};
    }
    // An extension with no name in front of it is not a document.
    if (dot == 0 || path[dot - 1] == '\\') {
        return {};
    }
    return path;
}

}  // namespace mdboss
