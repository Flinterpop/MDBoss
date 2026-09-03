#include "TextSearch.h"

#include <cassert>

namespace mdboss {
namespace {

// ASCII-only, byte at a time.  See the header for why that is safe on UTF-8.
char fold(char c, bool case_sensitive)
{
    if (case_sensitive) {
        return c;
    }
    const unsigned char byte = static_cast<unsigned char>(c);
    return static_cast<char>((byte >= 'A' && byte <= 'Z') ? byte - 'A' + 'a'
                                                          : byte);
}

// The first byte of `needle` that has to be found in the text, folded.  A
// leading carriage return is skipped because match_length_at() ignores those,
// and the answer is a line feed when the needle begins with a line break --
// which the scanners below cannot use as a fast filter, hence `usable`.
char first_significant(std::string_view needle, bool case_sensitive,
                       bool& usable)
{
    for (char c : needle) {   // bounded by the needle
        if (c == '\r') {
            continue;
        }
        usable = (c != '\n');
        return fold(c, case_sensitive);
    }
    usable = false;
    return '\0';
}

}  // namespace

std::size_t match_length_at(std::string_view text, std::string_view needle,
                            std::size_t pos, const SearchOptions& options)
{
    if (needle.empty() || pos > text.size()) {
        return 0;
    }
    std::size_t i = pos;
    std::size_t j = 0;
    while (j < needle.size()) {   // bounded by the needle
        const char want = needle[j];
        if (want == '\r') {
            ++j;   // a needle carrying CRLF must still match an LF document
            continue;
        }
        if (want == '\n') {
            if (i < text.size() && text[i] == '\r') {
                ++i;
            }
            if (i >= text.size() || text[i] != '\n') {
                return 0;
            }
            ++i;
            ++j;
            continue;
        }
        if (i >= text.size() ||
            fold(text[i], options.case_sensitive) !=
                fold(want, options.case_sensitive)) {
            return 0;
        }
        ++i;
        ++j;
    }
    // A needle of nothing but carriage returns consumes no text.  That is not
    // a match, and reporting it as one would let the scanners below stand
    // still on a zero-length hit.
    return i - pos;
}

TextMatch find_forward(std::string_view text, std::string_view needle,
                       std::size_t from, const SearchOptions& options)
{
    TextMatch result;
    if (needle.empty()) {
        return result;
    }
    bool usable = false;
    const char first =
        first_significant(needle, options.case_sensitive, usable);
    for (std::size_t pos = (from > text.size()) ? text.size() : from;
         pos < text.size(); ++pos) {   // bounded by the text
        // One comparison a byte, ahead of the real test: that is what keeps a
        // whole-corpus search from being quadratic in practice.
        if (usable && fold(text[pos], options.case_sensitive) != first) {
            continue;
        }
        const std::size_t length = match_length_at(text, needle, pos, options);
        if (length > 0) {
            result.offset = pos;
            result.length = length;
            return result;
        }
    }
    return result;
}

TextMatch find_backward(std::string_view text, std::string_view needle,
                        std::size_t before, const SearchOptions& options)
{
    TextMatch result;
    if (needle.empty() || before == 0) {
        return result;
    }
    const std::size_t start = (before > text.size()) ? text.size() : before;
    for (std::size_t pos = start; pos-- > 0;) {   // bounded by the text
        const std::size_t length = match_length_at(text, needle, pos, options);
        if (length > 0) {
            result.offset = pos;
            result.length = length;
            return result;
        }
    }
    return result;
}

TextMatch find_next(std::string_view text, std::string_view needle,
                    std::size_t from, const SearchOptions& options)
{
    TextMatch result = find_forward(text, needle, from, options);
    if (result.found() || from == 0) {
        return result;
    }
    result = find_forward(text, needle, 0, options);
    result.wrapped = result.found();
    return result;
}

TextMatch find_previous(std::string_view text, std::string_view needle,
                        std::size_t before, const SearchOptions& options)
{
    TextMatch result = find_backward(text, needle, before, options);
    if (result.found()) {
        return result;
    }
    result = find_backward(text, needle, text.size(), options);
    // Only a wrap when the match really did come from behind us: the single
    // match in a document, found again where it already was, has not wrapped.
    result.wrapped = result.found() && result.offset >= before;
    return result;
}

int line_number_at(std::string_view text, std::size_t offset)
{
    int line = 1;
    const std::size_t stop = (offset < text.size()) ? offset : text.size();
    for (std::size_t i = 0; i < stop; ++i) {   // bounded by the text
        if (text[i] == '\n') {
            ++line;
        }
    }
    return line;
}

std::string excerpt_at(std::string_view text, std::size_t offset)
{
    constexpr std::size_t kMaxExcerpt = 160;
    if (text.empty()) {
        return std::string();
    }
    const std::size_t hit = (offset < text.size()) ? offset : text.size() - 1;
    std::size_t begin = text.rfind('\n', hit);
    begin = (begin == std::string_view::npos) ? 0 : begin + 1;
    std::size_t end = text.find('\n', hit);
    if (end == std::string_view::npos) {
        end = text.size();
    }
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' ||
                           text[begin] == '\r')) {   // bounded by the line
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r')) {   // bounded by the line
        --end;
    }
    std::string line(text.substr(begin, end - begin));
    if (line.size() > kMaxExcerpt) {
        line.resize(kMaxExcerpt);
        line += "...";
    }
    // The whole point of the trim and the cap: a row of a list control cannot
    // survive a newline, and a match on a minified line would push every other
    // column off the screen.
    assert(line.find('\n') == std::string::npos &&
           "an excerpt must be one line");
    return line;
}

std::vector<LineHit> hits_in_text(std::string_view text,
                                  std::string_view needle,
                                  const SearchOptions& options,
                                  std::size_t max_hits)
{
    std::vector<LineHit> hits;
    if (needle.empty() || max_hits == 0) {
        return hits;
    }
    std::size_t pos = 0;
    // Line numbers are counted forward from the previous hit rather than from
    // the start of the text each time: the obvious version is quadratic, and
    // over a corpus that is the difference between a search and a pause.
    std::size_t counted_to = 0;
    int line = 1;
    while (pos <= text.size() && hits.size() < max_hits) {   // bounded
        // At the top, because every path below must leave `pos` further on
        // than it found it -- the property that stops this spinning.
        assert(pos >= counted_to && "the line counter must not run backwards");
        const TextMatch match = find_forward(text, needle, pos, options);
        if (!match.found()) {
            break;
        }
        for (std::size_t i = counted_to; i < match.offset; ++i) {   // bounded
            if (text[i] == '\n') {
                ++line;
            }
        }
        counted_to = match.offset;

        LineHit hit;
        hit.offset = match.offset;
        hit.line = line;
        hit.text = excerpt_at(text, match.offset);
        hits.push_back(std::move(hit));

        // The larger of the match and one byte, so a match that somehow
        // covered nothing still moves on.  find_forward() cannot return one,
        // and this does not depend on that staying true.
        pos = match.offset + ((match.length > 0) ? match.length : 1);
    }
    return hits;
}

}  // namespace mdboss
