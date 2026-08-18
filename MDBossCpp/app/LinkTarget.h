// Deciding what a link in the preview points at.
//
// This is the one place that parses an UNTRUSTED URL and decides whether the
// app will act on it, so it is deliberately pure, wx-free and WebView2-free:
// that is what lets the test binary compile it directly.  It lived inside
// PreviewPane.cpp until a rot check pointed out that the single most
// security-relevant function added in a day of work was also the only one no
// test could reach.
//
// UTF-8 throughout, like every other path in this app -- see PathUtf8.h.  The
// caller converts WebView2's wide URI once, on the way in; decoding percent
// escapes into BYTES rather than into wide characters is not a detail, since a
// path outside ASCII is percent-encoded as UTF-8 and decoding it any other way
// produces mojibake and a path that does not exist.

#ifndef MDBOSS_APP_LINK_TARGET_H
#define MDBOSS_APP_LINK_TARGET_H

#include <string>
#include <string_view>

namespace mdboss {

// The Windows path a `file:` URL names, but ONLY when it names a Markdown
// document.  Empty for everything else, and that is the whole safety
// property: the caller opens what this returns, so a link to an .exe, a .bat
// or a .pdf must come back empty and be left to behave as it always did.
//
// Empty is also the answer for a URL that is not `file:`, a path with no
// extension, one carrying an embedded NUL, and a UNC `file://server/share`
// URL -- the last is not rejected because it is dangerous but because it has
// never been supported, and quietly starting to follow one would be a change
// nobody asked for.
//
// A `?query` or `#fragment` is dropped: neither is part of a file name.
std::string markdown_path_from_file_url(std::string_view uri);

}  // namespace mdboss

#endif  // MDBOSS_APP_LINK_TARGET_H
