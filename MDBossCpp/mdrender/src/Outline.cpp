// Fence-aware scan of Markdown source for headings.
//
// The slug is derived from the *raw* heading source, matching what
// markdown-it hands mdrender.py as the inline token's content.  That detail
// matters: "## A [link](http://x/y) here" slugs to "a-linkhttpxy-here", so a
// slug computed from rendered text ("A link here" -> "a-link-here") would
// disagree with the Python oracle and break every anchor in the outline.

#include <cassert>
#include <cstddef>

#include "Internal.h"

namespace mdrender::detail {
namespace {

std::size_t indent_of(std::string_view line)
{
    std::size_t n = 0;
    while (n < line.size() && line[n] == ' ') {
        ++n;
        assert(n <= line.size() && "indent scan is bounded");
    }
    return n;
}

std::string_view trim(std::string_view text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool is_blank(std::string_view line)
{
    return trim(line).empty();
}

// A run of >= 3 identical fence characters (` or ~) at up to 3 spaces of
// indent.  Returns the run length, or 0 if this is not a fence line.
std::size_t fence_run(std::string_view line, char& fence_char)
{
    const std::size_t indent = indent_of(line);
    if (indent > 3 || indent >= line.size()) {
        return 0;
    }
    const char ch = line[indent];
    if (ch != '`' && ch != '~') {
        return 0;
    }
    std::size_t run = 0;
    while (indent + run < line.size() && line[indent + run] == ch) {
        ++run;
    }
    if (run < 3) {
        return 0;
    }
    fence_char = ch;
    return run;
}

// ATX heading text, or nullopt-ish via `level == 0`.  Strips the optional
// closing run of '#' the way CommonMark does.
int atx_heading(std::string_view line, std::string_view& text_out)
{
    const std::size_t indent = indent_of(line);
    if (indent > 3 || indent >= line.size() || line[indent] != '#') {
        return 0;
    }
    std::size_t hashes = 0;
    while (indent + hashes < line.size() && line[indent + hashes] == '#') {
        ++hashes;
    }
    if (hashes > 6) {
        return 0;
    }
    const std::size_t after = indent + hashes;
    if (after < line.size() && line[after] != ' ' && line[after] != '\t') {
        return 0;   // "#hashtag" is a paragraph, not a heading
    }

    std::string_view rest = trim(line.substr(after));
    // Drop a closing sequence of '#' preceded by a space (or the whole line).
    std::size_t end = rest.size();
    while (end > 0 && rest[end - 1] == '#') {
        --end;
    }
    if (end == 0) {
        rest = std::string_view{};
    } else if (end < rest.size() &&
               (rest[end - 1] == ' ' || rest[end - 1] == '\t')) {
        rest = trim(rest.substr(0, end));
    }
    text_out = rest;
    return static_cast<int>(hashes);
}

// A thematic break: three or more of -, _ or * with only spaces between.
// Checked only where no paragraph is open, because CommonMark gives a setext
// underline precedence over a break when one is.
bool is_thematic_break(std::string_view line)
{
    const std::size_t indent = indent_of(line);
    if (indent > 3 || indent >= line.size()) {
        return false;
    }
    const char ch = line[indent];
    if (ch != '-' && ch != '_' && ch != '*') {
        return false;
    }
    std::size_t count = 0;
    for (std::size_t i = indent; i < line.size(); ++i) {
        if (line[i] == ch) {
            ++count;
        } else if (line[i] != ' ' && line[i] != '\t') {
            return false;
        }
    }
    return count >= 3;
}

// Setext underline level: 1 for '===', 2 for '---', 0 if not an underline.
int setext_level(std::string_view line)
{
    const std::size_t indent = indent_of(line);
    if (indent > 3 || indent >= line.size()) {
        return 0;
    }
    const char ch = line[indent];
    if (ch != '=' && ch != '-') {
        return 0;
    }
    std::size_t run = 0;
    while (indent + run < line.size() && line[indent + run] == ch) {
        ++run;
    }
    if (!trim(line.substr(indent + run)).empty()) {
        return 0;
    }
    return (ch == '=') ? 1 : 2;
}

}  // namespace

std::vector<ScannedHeading> scan_headings(std::string_view md_text)
{
    const std::vector<std::string_view> lines = split_lines(md_text);
    std::vector<ScannedHeading> headings;
    std::unordered_map<std::string, int> seen;

    bool in_fence = false;
    char fence_char = '\0';
    std::size_t fence_len = 0;
    // The open paragraph, if any -- the only context in which a setext
    // underline forms a heading.  A setext heading takes the *whole*
    // paragraph as its text, joined with newlines, which is also what
    // markdown-it puts in the inline token mdrender.py slugs.
    std::vector<std::string_view> paragraph;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        assert(i <= kMaxLines && "line loop is bounded");
        const std::string_view line = lines[i];

        char ch = '\0';
        const std::size_t run = fence_run(line, ch);
        if (in_fence) {
            if (run >= fence_len && ch == fence_char) {
                in_fence = false;
                fence_len = 0;
            }
            paragraph.clear();
            continue;
        }
        if (run > 0) {
            in_fence = true;
            fence_char = ch;
            fence_len = run;
            paragraph.clear();
            continue;
        }

        if (is_blank(line)) {
            paragraph.clear();
            continue;
        }

        // An indented code block can only begin where a paragraph is not
        // already open, which is exactly CommonMark's rule.
        if (paragraph.empty() && indent_of(line) >= 4) {
            continue;
        }

        // A break interrupts nothing here but must not become paragraph text,
        // or the "---" opening an unstripped front-matter block would be
        // swallowed into the setext heading that its closing "---" forms.
        if (paragraph.empty() && is_thematic_break(line)) {
            continue;
        }

        std::string_view atx_text;
        const int level = atx_heading(line, atx_text);
        if (level > 0) {
            ScannedHeading heading;
            heading.level = level;
            heading.text = std::string(atx_text);
            heading.slug = slug(heading.text, seen);
            headings.push_back(std::move(heading));
            paragraph.clear();
            continue;
        }

        const int under = paragraph.empty() ? 0 : setext_level(line);
        if (under > 0) {
            std::string text;
            for (std::size_t j = 0; j < paragraph.size(); ++j) {
                if (j > 0) {
                    text += '\n';
                }
                text += std::string(trim(paragraph[j]));
            }
            ScannedHeading heading;
            heading.level = under;
            heading.text = text;
            heading.slug = slug(heading.text, seen);
            headings.push_back(std::move(heading));
            paragraph.clear();
            continue;
        }

        paragraph.push_back(line);
    }

    return headings;
}

}  // namespace mdrender::detail
