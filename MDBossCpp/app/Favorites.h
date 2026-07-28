// The favorites interchange file.
//
// This is a compatibility surface, not a private format: a list exported from
// the Python app must import here and vice versa, so the shape is Python's --
// {"favorites": [ ...paths... ]} with a two-space indent.  Its reader also
// accepts a bare array, and this one does too for the same reason.
//
// Kept free of wx so the parsing and merging can be unit tested; the dialogs
// and message boxes live in MainFrame, as with Updater/UpdaterHttp.

#ifndef MDBOSS_APP_FAVORITES_H
#define MDBOSS_APP_FAVORITES_H

#include <cstddef>
#include <string>
#include <vector>

namespace mdboss {

// The exported document, ready to write.  Byte-identical to what the Python
// app's json.dump(..., indent=2) produces, including the absence of a
// trailing newline -- a test pins it, so a formatting change in either JSON
// library shows up as a failure rather than as a file that merely happens to
// still parse.
std::string favorites_to_json(const std::vector<std::string>& favorites);

struct FavoritesFile {
    // False when the text is not JSON at all, or is JSON of a shape that
    // carries no favorites list.  Distinct from a list that parsed but was
    // empty, because the two get different messages: one is "that isn't a
    // favorites file", the other "no favorites were found in it".
    bool parsed = false;
    // Non-blank strings only; other element types are dropped, matching the
    // Python reader's `isinstance(p, str) and p.strip()` filter.
    std::vector<std::string> paths;
};

FavoritesFile parse_favorites_json(const std::string& text);

// The list after an import.  `merge` appends to `existing`; otherwise the
// imported list replaces it.  Duplicates are dropped case-insensitively
// keeping the first appearance, then the result is capped -- so a merge can
// never grow the list past the limit the config file allows.
std::vector<std::string> merge_favorites(
    const std::vector<std::string>& existing,
    const std::vector<std::string>& imported, bool merge, std::size_t cap);

}  // namespace mdboss

#endif  // MDBOSS_APP_FAVORITES_H
