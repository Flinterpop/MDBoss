// Removal of a leading YAML/TOML front-matter block.
//
// Port of mdrender.py's _FRONT_MATTER_RE, which is anchored at the start of
// the document so a horizontal rule further down is never mistaken for one.

#include <cassert>
#include <cstddef>

#include "Internal.h"

namespace mdrender {
namespace {

constexpr std::string_view kBom = "\xEF\xBB\xBF";

// Length of a run of [ \t] starting at `pos`.
std::size_t spaces_at(std::string_view text, std::size_t pos)
{
    std::size_t n = 0;
    while (pos + n < text.size() &&
           (text[pos + n] == ' ' || text[pos + n] == '\t')) {
        ++n;
        assert(n <= text.size() && "space run is bounded");
    }
    return n;
}

// If `text` at `pos` is `marker` followed only by blanks and then a line end
// (or the very end of the document), return the offset just past that line
// end.  Otherwise return npos.
std::size_t match_fence_line(std::string_view text, std::size_t pos,
                             std::string_view marker, bool allow_eof)
{
    if (text.compare(pos, marker.size(), marker) != 0) {
        return std::string_view::npos;
    }
    std::size_t after = pos + marker.size() + spaces_at(text, pos +
                                                        marker.size());
    if (after >= text.size()) {
        return allow_eof ? text.size() : std::string_view::npos;
    }
    if (text[after] == '\r' && after + 1 < text.size() &&
        text[after + 1] == '\n') {
        return after + 2;
    }
    if (text[after] == '\n') {
        return after + 1;
    }
    return std::string_view::npos;
}

// Offset just past an opening `---` / `+++` fence line, or npos.
std::size_t match_open_fence(std::string_view text, std::size_t pos)
{
    const std::size_t dashes = match_fence_line(text, pos, "---", false);
    if (dashes != std::string_view::npos) {
        return dashes;
    }
    return match_fence_line(text, pos, "+++", false);
}

// Offset just past a closing `---` / `+++` / `...` fence line, or npos.
std::size_t match_close_fence(std::string_view text, std::size_t pos)
{
    constexpr std::string_view markers[] = {"---", "+++", "..."};
    for (const std::string_view marker : markers) {
        const std::size_t end = match_fence_line(text, pos, marker, true);
        if (end != std::string_view::npos) {
            return end;
        }
    }
    return std::string_view::npos;
}

}  // namespace

std::string strip_front_matter(std::string_view md_text)
{
    std::size_t start = 0;
    if (md_text.compare(0, kBom.size(), kBom) == 0) {
        start = kBom.size();
    }

    const std::size_t body_start = match_open_fence(md_text, start);
    if (body_start == std::string_view::npos) {
        return std::string(md_text);
    }

    // The Python body group is non-greedy, so the earliest closing fence at a
    // line start wins.  Walk line starts from just after the opening fence.
    std::size_t pos = body_start;
    std::size_t bound = 0;
    while (pos <= md_text.size()) {
        ++bound;
        if (bound > detail::kMaxLines) {
            break;
        }
        const std::size_t end = match_close_fence(md_text, pos);
        if (end != std::string_view::npos) {
            return std::string(md_text.substr(end));
        }
        const std::size_t nl = md_text.find('\n', pos);
        if (nl == std::string_view::npos) {
            break;
        }
        pos = nl + 1;
    }
    return std::string(md_text);
}

}  // namespace mdrender
