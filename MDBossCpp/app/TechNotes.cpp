#include "TechNotes.h"

#include "FileScan.h"
#include "InternalNotes.h"
#include "PathUtf8.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace mdboss {
namespace {

namespace fs = std::filesystem;

std::string to_lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::string trimmed(std::string_view text)
{
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin) {
        const char ch = text[end - 1];
        if (ch != ' ' && ch != '\t' && ch != '\r') {
            break;
        }
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

// The front-matter block at the very top, or empty when there is none.
//
// Deliberately shallow, matching mdrender's own front_matter_title(): only a
// `---` at the very start counts, and the block must be closed.  A file whose
// front matter runs past the head we were given is treated as having none,
// because the alternative is guessing at half a block.
std::string_view front_matter(std::string_view text)
{
    if (text.compare(0, 4, "---\n") != 0 && text.compare(0, 5, "---\r\n") != 0) {
        return {};
    }
    const std::size_t first = text.find('\n');
    if (first == std::string_view::npos) {
        return {};
    }
    std::size_t pos = first + 1;
    // Bounded by the text, and by a line count no front matter should reach.
    for (int line = 0; line < 200 && pos < text.size(); ++line) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string content = trimmed(text.substr(pos, end - pos));
        if (content == "---" || content == "...") {
            return text.substr(first + 1, pos - first - 1);
        }
        pos = end + 1;
    }
    return {};
}

// The value of a top-level key, compared case-insensitively.  `GUID` is upper
// case in the template while YAML keys are conventionally lower, and a note
// hand-edited to `guid:` is still the same note.
std::string value_of(std::string_view block, const std::string& key)
{
    const std::string wanted = to_lower(key) + ":";
    std::size_t pos = 0;
    for (int line = 0; line < 200 && pos < block.size(); ++line) {   // bounded
        std::size_t end = block.find('\n', pos);
        if (end == std::string_view::npos) {
            end = block.size();
        }
        const std::string_view raw = block.substr(pos, end - pos);
        // Column zero only: an indented key belongs to something nested, not
        // to the document.
        if (!raw.empty() && raw.front() != ' ' && raw.front() != '\t') {
            const std::size_t colon = raw.find(':');
            if (colon != std::string_view::npos &&
                to_lower(std::string(raw.substr(0, colon + 1))) == wanted) {
                return trimmed(raw.substr(colon + 1));
            }
        }
        pos = end + 1;
    }
    return {};
}

// True when `keywords` names TechNote among its comma-separated entries.
// Matched case-insensitively and per entry, so "Draft, technote" counts and
// "TechNotes" does not.
bool has_technote_keyword(const std::string& keywords)
{
    std::string entry;
    std::istringstream stream(keywords);
    while (std::getline(stream, entry, ',')) {   // bounded by the line length
        if (to_lower(trimmed(entry)) == "technote") {
            return true;
        }
    }
    return false;
}

// Every number that more than one note claims, each named once, in order.
std::vector<std::string> duplicate_indices(const std::vector<TechNote>& notes)
{
    std::vector<std::string> seen;
    std::vector<std::string> twice;
    for (const TechNote& note : notes) {   // bounded by kMaxNotesListed
        if (note.tn_index.empty()) {
            continue;   // unnumbered notes do not collide with each other
        }
        if (std::find(seen.begin(), seen.end(), note.tn_index) == seen.end()) {
            seen.push_back(note.tn_index);
        } else if (std::find(twice.begin(), twice.end(), note.tn_index) ==
                   twice.end()) {
            twice.push_back(note.tn_index);
        }
    }
    return twice;
}

// An absolute file: URL for `path`, safe to use as a Markdown link
// destination.
//
// ABSOLUTE rather than relative: the index lives in MD_Internal under one
// root, while the notes it lists may be under any of them, so a relative link
// would have to climb out of one root and back down another -- and there is no
// relative path at all between two different drives.
std::string file_url(const std::string& path)
{
    std::string url = "file:///";
    for (const char ch : path) {   // bounded by the path length
        if (ch == '\\') {
            url += '/';
        } else if (ch == ' ') {
            // A raw space ends a Markdown link destination, and is not legal
            // in a URL either.
            url += "%20";
        } else if (ch == '<' || ch == '>' || ch == '"' || ch == '`') {
            continue;   // cannot appear in a Windows path; dropped, not trusted
        } else {
            url += ch;
        }
    }
    return url;
}

std::string escape_cell(const std::string& text)
{
    // Same hazard as the logins table: an unescaped pipe ends the cell early
    // and silently shifts every column after it.
    return escape_table_cell(text);
}

}  // namespace

bool parse_tech_note(std::string_view text, TechNote* out)
{
    assert(out != nullptr && "parse needs somewhere to write");
    const std::string_view block = front_matter(text);
    if (block.empty()) {
        return false;
    }
    const std::string guid = value_of(block, "GUID");
    if (guid.empty()) {
        return false;
    }
    if (!has_technote_keyword(value_of(block, "keywords"))) {
        return false;
    }
    out->title = value_of(block, "title");
    out->guid = guid;
    out->version = value_of(block, "version");
    out->subject = value_of(block, "subject");
    out->tn_index = value_of(block, "TNIndex");
    return true;
}

std::string format_tn_index(int year, int sequence)
{
    assert(year > 0 && "a tech-note number needs a real year");
    assert(sequence >= 1 && sequence <= kMaxTnSequence &&
           "sequence outside the allocatable range");
    std::ostringstream out;
    // Two digits is a floor, not a field width: 2026.100 must not become
    // 2026.00, which would collide with a number already handed out.
    out << year << '.' << (sequence < 10 ? "0" : "") << sequence;
    return out.str();
}

bool parse_tn_index(const std::string& text, int* year, int* sequence)
{
    assert(year != nullptr && sequence != nullptr && "parse needs outputs");
    const std::string value = trimmed(text);
    const std::size_t dot = value.find('.');
    if (dot != 4 || value.size() <= dot + 1 || value.size() > dot + 5) {
        return false;
    }
    for (std::size_t i = 0; i < value.size(); ++i) {   // bounded by the above
        if (i == dot) {
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(value[i])) == 0) {
            return false;
        }
    }
    const int parsed_year = std::stoi(value.substr(0, dot));
    const int parsed_sequence = std::stoi(value.substr(dot + 1));
    if (parsed_year <= 0 || parsed_sequence < 1 ||
        parsed_sequence > kMaxTnSequence) {
        return false;
    }
    *year = parsed_year;
    *sequence = parsed_sequence;
    return true;
}

int next_tn_sequence(const std::vector<TechNote>& existing, int year)
{
    assert(year > 0 && "a tech-note number needs a real year");
    int highest = 0;
    // One past the HIGHEST, not one past the count: a note deleted from the
    // middle of a year must not cause the next one to reuse its number, since
    // the deleted note may well still exist in someone else's copy or in a
    // document that cites it.
    for (const TechNote& note : existing) {   // bounded by kMaxNotesListed
        int found_year = 0;
        int found_sequence = 0;
        if (!parse_tn_index(note.tn_index, &found_year, &found_sequence)) {
            continue;
        }
        if (found_year == year && found_sequence > highest) {
            highest = found_sequence;
        }
    }
    if (highest >= kMaxTnSequence) {
        return kMaxTnSequence;   // clamped rather than wrapped; see the header
    }
    return highest + 1;
}

namespace {

// The span of an empty top-level `TNIndex:` value in the front matter, as
// [begin, end) offsets into `text`.  False when there is no such line.
//
// A template does not have to use the placeholder: a hand-maintained one that
// simply carries `TNIndex:` with nothing after it is asking the same question,
// and filling it is what makes this work on templates that predate the token.
// A line that already has a value is left alone -- that number was chosen.
bool empty_tn_index_span(const std::string& text, std::size_t* begin,
                         std::size_t* end)
{
    assert(begin != nullptr && end != nullptr && "span needs outputs");
    if (text.compare(0, 4, "---\n") != 0 && text.compare(0, 5, "---\r\n") != 0) {
        return false;
    }
    std::size_t pos = text.find('\n');
    if (pos == std::string::npos) {
        return false;
    }
    ++pos;
    for (int line = 0; line < 200 && pos < text.size(); ++line) {   // bounded
        std::size_t stop = text.find('\n', pos);
        if (stop == std::string::npos) {
            stop = text.size();
        }
        const std::string_view raw(text.data() + pos, stop - pos);
        const std::string content = trimmed(raw);
        if (content == "---" || content == "...") {
            return false;   // closed without one
        }
        if (!raw.empty() && raw.front() != ' ' && raw.front() != '\t') {
            const std::size_t colon = raw.find(':');
            if (colon != std::string_view::npos &&
                to_lower(std::string(raw.substr(0, colon))) == "tnindex" &&
                trimmed(raw.substr(colon + 1)).empty()) {
                *begin = pos + colon + 1;
                *end = stop;
                // Not the \r of a CRLF file: rewriting over it would join the
                // line to the next one in every editor that respects CRLF.
                while (*end > *begin && text[*end - 1] == '\r') {
                    --*end;
                }
                return true;
            }
        }
        pos = stop + 1;
    }
    return false;
}

}  // namespace

bool needs_tn_index(const std::string& text)
{
    if (text.find(kTnIndexPlaceholder) != std::string::npos) {
        return true;
    }
    std::size_t begin = 0;
    std::size_t end = 0;
    return empty_tn_index_span(text, &begin, &end);
}

std::string fill_tn_index(const std::string& text,
                          const std::vector<TechNote>& existing, int year,
                          std::string* assigned)
{
    if (!needs_tn_index(text)) {
        return text;
    }
    const std::string number =
        format_tn_index(year, next_tn_sequence(existing, year));
    if (assigned != nullptr) {
        *assigned = number;
    }

    std::string out = text;
    const std::string placeholder = kTnIndexPlaceholder;
    std::size_t pos = out.find(placeholder);
    // Every occurrence gets the SAME number -- one document, one number, even
    // if a template names it in the front matter and again in the banner line.
    for (int guard = 0; guard < 100 && pos != std::string::npos; ++guard) {
        out.replace(pos, placeholder.size(), number);
        pos = out.find(placeholder, pos + number.size());
    }

    // The placeholder wins where a template has both; this fills the bare
    // `TNIndex:` form, which is all a hand-written template needs to carry.
    std::size_t begin = 0;
    std::size_t end = 0;
    if (empty_tn_index_span(out, &begin, &end)) {
        out.replace(begin, end - begin, " " + number);
    }
    return out;
}

TechNoteGaps tech_note_gaps(const std::string& text)
{
    TechNoteGaps gaps;
    const std::string_view block = front_matter(text);
    if (block.empty()) {
        // No block at all: everything has to be written, including a number.
        gaps.front_matter = true;
        gaps.guid = true;
        gaps.keyword = true;
        gaps.tn_index = true;
        return gaps;
    }
    gaps.guid = value_of(block, "GUID").empty();
    gaps.keyword = !has_technote_keyword(value_of(block, "keywords"));
    gaps.tn_index = value_of(block, "TNIndex").empty();
    return gaps;
}

int tech_note_year(const std::string& text, int fallback_year)
{
    assert(fallback_year > 0 && "a fallback year is required");
    const std::string_view block = front_matter(text);
    if (block.empty()) {
        return fallback_year;
    }
    // First match wins, in this order: a document carrying both `created` and
    // `updated` belongs to the year it was created.
    for (const char* const key : {"date", "created", "updated"}) {
        const std::string value = value_of(block, key);
        if (value.size() < 4) {
            continue;
        }
        bool digits = true;
        for (int i = 0; i < 4; ++i) {   // bounded
            if (std::isdigit(static_cast<unsigned char>(value[i])) == 0) {
                digits = false;
                break;
            }
        }
        if (!digits) {
            continue;   // "17 Aug 2026" and the like: not led by a year
        }
        const int year = std::stoi(value.substr(0, 4));
        // A plausible year only.  A number that happens to lead the value but
        // is not a date -- a version, an identifier -- must not silently
        // create a whole year of tech notes nobody has.
        if (year >= 1970 && year <= fallback_year) {
            return year;
        }
    }
    return fallback_year;
}

std::string promote_to_tech_note(const std::string& text,
                                 const std::string& guid,
                                 const std::string& tn_index,
                                 const std::string& title)
{
    const TechNoteGaps gaps = tech_note_gaps(text);
    if (!gaps.front_matter && !gaps.guid && !gaps.keyword && !gaps.tn_index) {
        return text;   // already a tech note, fully furnished
    }

    // No block at all: write one, and leave the document itself untouched
    // below it.  The title is the filename rather than the first heading --
    // guessing at a heading means guessing at which heading.
    if (gaps.front_matter) {
        const std::string eol =
            text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        std::string out = "---" + eol + "title: " + title + eol +
                          "GUID: " + guid + eol;
        if (!tn_index.empty()) {
            out += "TNIndex: " + tn_index + eol;
        }
        out += "keywords: TechNote" + eol + "---" + eol + eol;
        return out + text;
    }

    // A block exists.  Rebuild it line by line rather than splicing at
    // offsets: extending the keywords line moves every offset after it, and
    // two edits computed against the original string would land wrong.
    const std::string_view block = front_matter(text);
    assert(!block.empty() && "gaps said the block was there");
    const std::size_t begin =
        static_cast<std::size_t>(block.data() - text.data());
    const std::size_t end = begin + block.size();
    // Follow the file, not our own habits: a CRLF document must not come back
    // with one LF line in the middle of its front matter.
    const bool crlf = text.find("\r\n") != std::string::npos &&
                      text.find("\r\n") < end;
    const std::string eol = crlf ? "\r\n" : "\n";

    std::string rebuilt;
    bool keyword_done = !gaps.keyword;
    std::size_t pos = 0;
    for (int line = 0; line < 200 && pos < block.size(); ++line) {   // bounded
        std::size_t stop = block.find('\n', pos);
        if (stop == std::string_view::npos) {
            stop = block.size();
        } else {
            ++stop;   // keep the newline with its line
        }
        const std::string_view raw = block.substr(pos, stop - pos);
        pos = stop;

        // Extend an existing keywords list rather than writing a second
        // `keywords:` line -- YAML reads that as one key given twice, and
        // which value wins is parser-dependent.
        const std::size_t colon = raw.find(':');
        if (!keyword_done && !raw.empty() && raw.front() != ' ' &&
            raw.front() != '\t' && colon != std::string_view::npos &&
            to_lower(std::string(raw.substr(0, colon))) == "keywords") {
            // The newline has to come off before trimming: trimmed() strips
            // spaces, tabs and a \r, but not a \n -- every other caller hands
            // it a line that was already split.
            std::string_view tail = raw.substr(colon + 1);
            while (!tail.empty() && tail.back() == '\n') {
                tail.remove_suffix(1);
            }
            const std::string value = trimmed(tail);
            rebuilt += "keywords: ";
            rebuilt += value.empty() ? "TechNote" : value + ", TechNote";
            rebuilt += eol;
            keyword_done = true;
            continue;
        }
        rebuilt.append(raw);
    }

    // Anything still missing goes at the END of the block: the first line of a
    // Typora-style block is the document's own title line, and pushing it down
    // would change what every other reader shows as the title.
    if (gaps.guid) {
        rebuilt += "GUID: " + guid + eol;
    }
    if (gaps.tn_index && !tn_index.empty()) {
        rebuilt += "TNIndex: " + tn_index + eol;
    }
    if (!keyword_done) {
        rebuilt += "keywords: TechNote" + eol;
    }

    std::string out = text;
    out.replace(begin, end - begin, rebuilt);
    return out;
}

std::vector<TechNote> scan_tech_notes(const std::vector<std::string>& paths)
{
    std::vector<TechNote> found;
    std::size_t examined = 0;
    for (const std::string& path : paths) {
        if (examined >= kMaxNotesExamined || found.size() >= kMaxNotesListed) {
            break;
        }
        ++examined;

        std::ifstream stream(path_from_utf8(path), std::ios::binary);
        if (!stream) {
            continue;   // unreadable: not worth interrupting a rebuild for
        }
        // Only the head.  Front matter is at the very top, so reading a 4 MB
        // document to decide it is not a tech note would be the whole cost of
        // the feature for none of the benefit.
        std::string head(kMaxHeadBytes, '\0');
        stream.read(head.data(), static_cast<std::streamsize>(kMaxHeadBytes));
        head.resize(static_cast<std::size_t>(stream.gcount()));

        TechNote note;
        if (!parse_tech_note(strip_utf8_bom(head), &note)) {
            continue;
        }
        note.path = path;
        if (note.title.empty()) {
            // A note whose title was cleared still has to be identifiable, and
            // its filename is the next best name.
            note.title = path_to_utf8(path_from_utf8(path).stem());
        }
        found.push_back(std::move(note));
    }

    std::sort(found.begin(), found.end(),
              [](const TechNote& a, const TechNote& b) {
                  // By number first: it leads the row, and a numbered series
                  // read in any other order looks like it has gaps.  Notes
                  // with no number sort after every numbered one rather than
                  // before -- they are the ones that predate the numbering.
                  int a_year = 0;
                  int a_seq = 0;
                  int b_year = 0;
                  int b_seq = 0;
                  const bool a_num = parse_tn_index(a.tn_index, &a_year, &a_seq);
                  const bool b_num = parse_tn_index(b.tn_index, &b_year, &b_seq);
                  if (a_num != b_num) {
                      return a_num;
                  }
                  if (a_num && (a_year != b_year || a_seq != b_seq)) {
                      return a_year != b_year ? a_year < b_year : a_seq < b_seq;
                  }

                  const std::string left = to_lower(a.title);
                  const std::string right = to_lower(b.title);
                  if (left != right) {
                      return left < right;
                  }
                  // Total and stable: two notes may share a title, and a sort
                  // that called them equal would order them differently run to
                  // run and show up as spurious changes in the index.
                  return a.path < b.path;
              });
    return found;
}

std::string tech_notes_index(const std::vector<TechNote>& notes,
                             const std::vector<std::string>& roots,
                             const std::string& generated)
{
    std::ostringstream out;
    out << "# Tech Notes\n"
        << "\n"
        << "Every document under your folders whose front matter carries a "
           "`GUID` and `TechNote` among its `keywords`.\n"
        << "\n"
        << "Generated by MD Boss";
    if (!generated.empty()) {
        out << " on " << generated;
    }
    out << ". **Rebuilt, not edited** -- changes here are lost on the next "
           "rebuild; edit the notes themselves.\n"
        << "\n";

    if (notes.empty()) {
        out << "No tech notes found.\n";
        return out.str();
    }

    out << "| TN Index | Title | Version | Subject | GUID | File |\n"
        << "|---|---|---|---|---|---|\n";
    for (const TechNote& note : notes) {   // bounded by kMaxNotesListed
        // Shown relative to whichever root holds it: an absolute path is
        // mostly drive letters and repeated prefix, and the point of the
        // column is telling two similarly named notes apart.
        std::string shown = note.path;
        for (const std::string& root : roots) {
            const std::string base = norm_path(root);
            const std::string full = norm_path(note.path);
            if (base.empty() || full.size() <= base.size() + 1) {
                continue;
            }
            if (full.compare(0, base.size(), base) == 0 &&
                (full[base.size()] == '\\' || full[base.size()] == '/')) {
                shown = note.path.substr(base.size() + 1);
                break;
            }
        }
        // The title links to the note.  Clicking it in MD Boss opens that
        // document; the parentheses around the destination are why file_url()
        // encodes spaces, since a raw one would end the link early and leave
        // the rest of the path as text.
        std::string title = escape_cell(note.title);
        if (!note.path.empty()) {
            // A bracket in a title would close the link text early, so the
            // two characters that can are escaped here rather than in
            // escape_cell(), which is also used for cells that are not links.
            std::string label;
            for (const char ch : title) {   // bounded by the title length
                if (ch == '[' || ch == ']') {
                    label += '\\';
                }
                label += ch;
            }
            title = "[" + label + "](" + file_url(note.path) + ")";
        }

        out << "| " << escape_cell(note.tn_index)
            << " | " << title
            << " | " << escape_cell(note.version)
            << " | " << escape_cell(note.subject)
            << " | " << escape_cell(note.guid)
            << " | " << escape_cell(shown)
            << " |\n";
    }

    out << "\n" << notes.size() << " tech note"
        << (notes.size() == 1 ? "" : "s") << ".\n";

    // A number claimed twice is the one failure a numbered series actually
    // suffers from, and the rebuild is the only thing that ever sees every
    // note at once -- so it is the only thing that can notice.  Reported
    // rather than repaired: which of the two should move is the author's call.
    const std::vector<std::string> clashes = duplicate_indices(notes);
    if (!clashes.empty()) {
        out << "\n> **Duplicate numbers:** ";
        for (std::size_t i = 0; i < clashes.size(); ++i) {
            out << (i == 0 ? "" : ", ") << '`' << clashes[i] << '`';
        }
        out << ". Two notes cannot share one number -- renumber one of each "
               "pair, then rebuild.\n";
    }
    return out.str();
}

}  // namespace mdboss
