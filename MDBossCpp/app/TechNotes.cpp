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
    return true;
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

    out << "| Title | Version | Subject | GUID | File |\n"
        << "|---|---|---|---|---|\n";
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
        out << "| " << escape_cell(note.title)
            << " | " << escape_cell(note.version)
            << " | " << escape_cell(note.subject)
            << " | " << escape_cell(note.guid)
            << " | " << escape_cell(shown)
            << " |\n";
    }

    out << "\n" << notes.size() << " tech note"
        << (notes.size() == 1 ? "" : "s") << ".\n";
    return out.str();
}

}  // namespace mdboss
