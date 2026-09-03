// Finding a run of text inside a document, and inside every document.
//
// Deliberately wx-free and free of I/O, for the reason LinkTarget.h records:
// a matcher is exactly the kind of thing that is wrong at the edges, and a
// matcher that cannot be reached from a test is where the bug will be.  The
// two search commands -- Find in this document, and Find in all documents --
// share every rule below, so there is one place to read them and one place
// they can disagree with themselves.
//
// Offsets are BYTE offsets into UTF-8 text, which is what makes them usable
// as they stand: Scintilla positions are byte offsets into the UTF-8 document
// too, so a match found here can be selected in the editor without a
// conversion that could be off by a character.

#ifndef MDBOSS_APP_TEXT_SEARCH_H
#define MDBOSS_APP_TEXT_SEARCH_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mdboss {

struct SearchOptions {
    // Off by default: the tree's Contents box has always matched
    // case-insensitively, and a search someone types is a question about
    // words rather than about capitals.
    bool case_sensitive = false;
};

// One match: where it starts, and how many bytes of the text it covers.
//
// The length is not simply the needle's, because a needle's newline matches
// either a LF or a CRLF -- see match_length_at() -- so a two-line block found
// in a CRLF document is a byte longer than the same block found in an LF one.
// Selecting the match in the editor needs the real span, not the assumed one.
struct TextMatch {
    std::size_t offset = std::string::npos;
    std::size_t length = 0;
    // Set when the search ran off the end (or the start) and continued from
    // the other side.  Reported rather than hidden: "Find next" that silently
    // wraps looks like a list with no end.
    bool wrapped = false;

    bool found() const { return offset != std::string::npos; }
};

// Bytes of `text` matched by `needle` starting at `pos`, or 0 for no match.
//
// Two rules, both there so a BLOCK of text pasted out of one document finds
// itself in another:
//
//   * a '\n' in the needle matches "\n" or "\r\n" in the text, and
//   * a '\r' in the needle is ignored,
//
// so the search does not depend on which line ending either side happens to
// use.  Case folding is ASCII-only and applied byte by byte, matching
// search_file_contents(): a UTF-8 lead or continuation byte is never in
// 'A'..'Z', so folding cannot corrupt a multi-byte character or make two
// different ones compare equal.
std::size_t match_length_at(std::string_view text, std::string_view needle,
                            std::size_t pos, const SearchOptions& options);

// First match at or after `from`.  Never wraps; an empty needle never matches.
TextMatch find_forward(std::string_view text, std::string_view needle,
                       std::size_t from, const SearchOptions& options);

// Last match that starts strictly before `before`.  Never wraps.
TextMatch find_backward(std::string_view text, std::string_view needle,
                        std::size_t before, const SearchOptions& options);

// The two the Find bar actually calls: as above, but continuing from the far
// end when nothing is left in the direction asked for, with `wrapped` set to
// say so.
TextMatch find_next(std::string_view text, std::string_view needle,
                    std::size_t from, const SearchOptions& options);
TextMatch find_previous(std::string_view text, std::string_view needle,
                        std::size_t before, const SearchOptions& options);

// 1-based line number of the byte at `offset`.
int line_number_at(std::string_view text, std::size_t offset);

// The line holding `offset`, trimmed and length-capped, ready to put in a
// list control.  A newline in it would break the row, and an unbounded line
// -- a minified block, a base64 image -- would push every other column off
// the screen.
std::string excerpt_at(std::string_view text, std::size_t offset);

// One match, in the form a results list wants it.
struct LineHit {
    std::size_t offset = 0;
    int line = 1;
    std::string text;
};

// Up to `max_hits` matches in `text`, in order.
//
// Overlapping matches are not reported: the scan resumes after the match it
// just took, so "aa" in "aaaa" is two hits rather than three.  That is the
// count a person reading a results list means.
std::vector<LineHit> hits_in_text(std::string_view text,
                                  std::string_view needle,
                                  const SearchOptions& options,
                                  std::size_t max_hits);

}  // namespace mdboss

#endif  // MDBOSS_APP_TEXT_SEARCH_H
