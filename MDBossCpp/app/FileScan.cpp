#include "FileScan.h"

#include "PathUtf8.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <shellapi.h>

namespace mdboss {
namespace {

namespace fs = std::filesystem;

// Bounded (Rule of 10): a pathological tree must not spin the UI.
constexpr int kMaxWalkedDirs = 100000;
constexpr std::size_t kMaxEntriesPerDir = 20000;

std::string to_lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    });
    return text;
}

}  // namespace

bool is_markdown(const std::string& name)
{
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos) {
        return false;
    }
    // Matches app.py's MARKDOWN_EXTS; keep the two in step.
    const std::string ext = to_lower(name.substr(dot));
    return ext == ".md" || ext == ".markdown" || ext == ".mdown" ||
           ext == ".mkd" || ext == ".mdwn";
}

std::string norm_path(const std::string& path)
{
    std::error_code ec;
    fs::path absolute = fs::absolute(path_from_utf8(path), ec);
    if (ec) {
        absolute = path_from_utf8(path);
    }
    return to_lower(path_to_utf8(absolute.lexically_normal()));
}

bool is_under_any_root(const std::string& path,
                       const std::vector<std::string>& roots)
{
    if (path.empty()) {
        return false;
    }
    const std::string target = norm_path(path);
    for (const std::string& root : roots) {   // bounded by the roots list
        if (root.empty()) {
            continue;
        }
        std::string base = norm_path(root);
        // A root spelled "C:\Docs\" must compare equal to "C:\Docs".  Bounded
        // by the string, and it can empty the base -- "\" normalises to one
        // separator -- which the length test below then rejects.
        while (!base.empty() && (base.back() == '\\' || base.back() == '/')) {
            base.pop_back();
        }
        if (base.empty() || target.size() <= base.size()) {
            continue;
        }
        if (target.compare(0, base.size(), base) != 0) {
            continue;
        }
        // The separator is what makes this containment rather than a prefix
        // match: without it "c:\docs2\a.md" reads as living under "c:\docs".
        const char next = target[base.size()];
        if (next == '\\' || next == '/') {
            return true;
        }
    }
    return false;
}

namespace {

// The matched line, trimmed and capped, ready to show in the tree.
std::string excerpt_at(const std::string& text, std::size_t hit)
{
    constexpr std::size_t kMaxExcerpt = 160;
    std::size_t begin = text.rfind('\n', hit);
    begin = (begin == std::string::npos) ? 0 : begin + 1;
    std::size_t end = text.find('\n', hit);
    if (end == std::string::npos) {
        end = text.size();
    }
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' ||
                           text[begin] == '\r')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r')) {
        --end;
    }
    std::string line = text.substr(begin, end - begin);
    if (line.size() > kMaxExcerpt) {
        line.resize(kMaxExcerpt);
        line += "...";
    }
    return line;
}

// 1-based line number of the byte at `hit`.
int line_number_at(const std::string& text, std::size_t hit)
{
    int line = 1;
    // Not std::min: <windows.h> is included here and defines a min() macro,
    // which turns std::min into a syntax error.
    const std::size_t stop = (hit < text.size()) ? hit : text.size();
    for (std::size_t i = 0; i < stop; ++i) {   // bounded by the file, capped
        if (text[i] == '\n') {
            ++line;
        }
    }
    return line;
}

}  // namespace

std::vector<ContentMatch> search_file_contents(
    const std::string& root, const std::string& needle,
    const std::function<bool()>& stop)
{
    std::vector<ContentMatch> matches;
    if (root.empty() || needle.size() < kMinSearchNeedle) {
        return matches;
    }
    const std::string wanted = to_lower(needle);

    // An explicit worklist rather than recursion, and every loop below is
    // bounded: a junction loop or a pathological tree must not be able to
    // hang the search thread.
    std::vector<std::string> pending{root};
    std::size_t examined = 0;
    while (!pending.empty() && examined < kMaxSearchFiles &&
           matches.size() < kMaxSearchResults) {
        if (stop && stop()) {
            break;
        }
        const std::string dir = pending.back();
        pending.pop_back();

        for (const Entry& entry : list_directory(dir)) {
            if (examined >= kMaxSearchFiles ||
                matches.size() >= kMaxSearchResults) {
                break;
            }
            if (entry.is_dir) {
                pending.push_back(entry.path);
                continue;
            }
            ++examined;

            std::error_code ec;
            const auto size = fs::file_size(path_from_utf8(entry.path), ec);
            if (ec || size == 0 || size > kMaxSearchFileBytes) {
                continue;   // unreadable, empty, or too big to be worth it
            }
            std::ifstream stream(path_from_utf8(entry.path), std::ios::binary);
            if (!stream) {
                continue;
            }
            std::ostringstream buffer;
            buffer << stream.rdbuf();
            const std::string text = strip_utf8_bom(buffer.str());

            // Matched over bytes, lowercased ASCII-only, so a file that is
            // not valid UTF-8 still searches rather than being skipped.
            const std::size_t hit = to_lower(text).find(wanted);
            if (hit == std::string::npos) {
                continue;
            }
            ContentMatch match;
            match.path = entry.path;
            match.line = line_number_at(text, hit);
            match.text = excerpt_at(text, hit);
            matches.push_back(std::move(match));
        }
    }
    return matches;
}

RootScan scan_root(const std::string& root)
{
    RootScan result;
    std::map<std::string, int>& counts = result.counts;
    std::error_code ec;
    if (!fs::is_directory(path_from_utf8(root), ec) || ec) {
        return result;
    }

    // ONE walk.  An earlier version collected the directories and then
    // re-listed each of them, walking the tree twice and normalising every
    // path again on the second pass; over a few thousand files that was slow
    // enough to matter, and it ran on the UI thread.
    const fs::path root_path = path_from_utf8(root).lexically_normal();
    std::vector<fs::path> dirs{root_path};
    counts[norm_path(path_to_utf8(root_path))] = 0;

    // The folder a file sits in, relative to the root, '/'-separated -- the
    // form the tree splits into nodes.  Empty for a file directly in the root.
    // Separators are normalised here rather than at every use: a path built
    // from native components would otherwise split on the wrong character and
    // put "sub\deep" on one row.
    const auto relative_dir = [&root_path](const fs::path& file) {
        const fs::path rel = file.parent_path().lexically_relative(root_path);
        if (rel.empty() || rel == ".") {
            return std::string();
        }
        std::string text = path_to_utf8(rel);
        for (char& c : text) {   // bounded by the path length
            if (c == '\\') {
                c = '/';
            }
        }
        return text;
    };

    // Iterate with the error_code-taking increment.  A range-for uses the
    // throwing operator++, which the error_code constructor does NOT make
    // safe: one unreadable directory mid-walk raises filesystem_error, and on
    // a worker thread that is an uncaught exception and a hard crash.
    fs::recursive_directory_iterator it(
        root_path, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    int walked = 0;
    while (!ec && it != end && walked < kMaxWalkedDirs) {
        ++walked;
        const fs::directory_entry entry = *it;
        std::error_code kind_ec;
        if (entry.is_directory(kind_ec) && !kind_ec) {
            dirs.push_back(entry.path());
            counts.emplace(norm_path(path_to_utf8(entry.path())), 0);
        } else if (is_markdown(path_to_utf8(entry.path().filename()))) {
            const auto found =
                counts.find(norm_path(path_to_utf8(entry.path().parent_path())));
            if (found != counts.end()) {
                ++found->second;
            }
            if (result.entries.size() < kMaxEntriesPerRoot) {
                DocEntry doc;
                doc.path = path_to_utf8(entry.path());
                doc.name = path_to_utf8(entry.path().filename());
                doc.relative_dir = relative_dir(entry.path());
                result.entries.push_back(std::move(doc));
            }
        }
        it.increment(ec);
    }

    // Roll the direct counts up, deepest first, so each parent adds its
    // children's finished totals exactly once.
    std::sort(dirs.begin(), dirs.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.native().size() > b.native().size();
              });
    for (const fs::path& dir : dirs) {
        if (dir == root_path) {
            continue;
        }
        const auto self = counts.find(norm_path(path_to_utf8(dir)));
        const auto parent =
            counts.find(norm_path(path_to_utf8(dir.parent_path())));
        if (self != counts.end() && parent != counts.end()) {
            parent->second += self->second;
        }
    }

    // Display order, decided once here rather than at every rebuild: folders
    // in path order, and within a folder the files case-insensitively by name
    // -- the same ordering the old per-directory listing produced.
    std::sort(result.entries.begin(), result.entries.end(),
              [](const DocEntry& a, const DocEntry& b) {
                  const std::string dir_a = to_lower(a.relative_dir);
                  const std::string dir_b = to_lower(b.relative_dir);
                  if (dir_a != dir_b) {
                      return dir_a < dir_b;
                  }
                  return to_lower(a.name) < to_lower(b.name);
              });
    return result;
}

bool send_to_recycle_bin(const std::string& path)
{
    assert(!path.empty() && "deleting an empty path would be a bug");
    if (path.empty()) {
        return false;
    }
    // SHFileOperation wants an absolute path and a DOUBLE-null-terminated
    // list, not a plain string.  A relative path silently resolves against
    // the process's current directory, which is not where the tree is.
    std::error_code ec;
    const fs::path absolute = fs::absolute(path_from_utf8(path), ec);
    if (ec) {
        return false;
    }
    std::wstring wide = absolute.wstring();
    wide.push_back(L'\0');
    wide.push_back(L'\0');

    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = wide.c_str();
    // FOF_ALLOWUNDO is what routes this to the Recycle Bin rather than
    // deleting outright; without it the operation is unrecoverable.  The
    // caller has already confirmed, hence NOCONFIRMATION.
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI |
                FOF_SILENT;
    return SHFileOperationW(&op) == 0 && op.fAnyOperationsAborted == FALSE;
}

std::string choose_dropped_file(const std::vector<std::string>& filenames)
{
    if (filenames.empty()) {
        return {};
    }
    for (const std::string& name : filenames) {
        // Match on the leaf: a directory called "notes.md" further up the
        // path must not make a .txt file look like Markdown.
        const std::size_t slash = name.find_last_of("/\\");
        const std::string leaf =
            (slash == std::string::npos) ? name : name.substr(slash + 1);
        if (is_markdown(leaf)) {
            return name;
        }
    }
    return filenames.front();
}

std::vector<Entry> list_directory(const std::string& path)
{
    std::vector<Entry> out;
    std::error_code ec;
    fs::directory_iterator it(
        path_from_utf8(path), fs::directory_options::skip_permission_denied, ec);
    const fs::directory_iterator end;
    // Same reason as md_counts_for_root: increment(ec) rather than a
    // range-for, so an unreadable entry cannot throw.
    while (!ec && it != end && out.size() < kMaxEntriesPerDir) {
        const fs::directory_entry entry = *it;
        std::error_code kind_ec;
        const bool dir = entry.is_directory(kind_ec) && !kind_ec;
        const std::string name = path_to_utf8(entry.path().filename());
        if (dir || is_markdown(name)) {
            out.push_back(Entry{path_to_utf8(entry.path()), name, dir});
        }
        it.increment(ec);
    }

    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.is_dir != b.is_dir) {
            return a.is_dir;   // directories first
        }
        return to_lower(a.name) < to_lower(b.name);
    });
    return out;
}

std::string strip_utf8_bom(std::string text)
{
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

std::size_t first_invalid_utf8(std::string_view text)
{
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {   // bounded: i strictly advances every iteration
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        if (lead == 0x00) {
            return i;   // legal UTF-8, never legal document text
        }
        if (lead < 0x80) {
            ++i;
            continue;
        }
        // How many continuation bytes, and the allowed range of the first
        // one -- the narrowed ranges reject overlong forms, UTF-16
        // surrogates (via 0xED) and code points past U+10FFFF (via 0xF4).
        std::size_t extra = 0;
        unsigned char first_lo = 0x80;
        unsigned char first_hi = 0xBF;
        if (lead >= 0xC2 && lead <= 0xDF) {
            extra = 1;
        } else if (lead == 0xE0) {
            extra = 2;
            first_lo = 0xA0;
        } else if (lead == 0xED) {
            extra = 2;
            first_hi = 0x9F;
        } else if (lead >= 0xE1 && lead <= 0xEF) {
            extra = 2;
        } else if (lead == 0xF0) {
            extra = 3;
            first_lo = 0x90;
        } else if (lead >= 0xF1 && lead <= 0xF3) {
            extra = 3;
        } else if (lead == 0xF4) {
            extra = 3;
            first_hi = 0x8F;
        } else {
            return i;   // 0x80..0xC1, 0xF5..0xFF are never lead bytes
        }
        if (n - i <= extra) {
            return i;   // sequence truncated by end of text
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            const unsigned char c = static_cast<unsigned char>(text[i + k]);
            const unsigned char lo = (k == 1) ? first_lo : 0x80;
            const unsigned char hi = (k == 1) ? first_hi : 0xBF;
            if (c < lo || c > hi) {
                return i;
            }
        }
        i += extra + 1;
    }
    return std::string_view::npos;
}

TextEncoding detect_text_encoding(const std::string& bytes)
{
    if (bytes.size() >= 2) {
        const unsigned char b0 = static_cast<unsigned char>(bytes[0]);
        const unsigned char b1 = static_cast<unsigned char>(bytes[1]);
        if (b0 == 0xFF && b1 == 0xFE) {
            return TextEncoding::kUtf16LE;
        }
        if (b0 == 0xFE && b1 == 0xFF) {
            return TextEncoding::kUtf16BE;
        }
    }
    if (first_invalid_utf8(bytes) == std::string::npos) {
        return TextEncoding::kUtf8;
    }
    // UTF-16 text that lost its BOM shows one NUL per ASCII character, all
    // on the same side; scattered NULs mean corruption, not an encoding.
    std::size_t odd_nul = 0;
    std::size_t even_nul = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] == '\0') {
            (i % 2 != 0 ? odd_nul : even_nul) += 1;
        }
    }
    const std::size_t half = bytes.size() / 2;
    if (bytes.size() >= 4 && bytes.size() % 2 == 0) {
        if (even_nul == 0 && odd_nul * 4 >= half * 3) {
            return TextEncoding::kUtf16LE;
        }
        if (odd_nul == 0 && even_nul * 4 >= half * 3) {
            return TextEncoding::kUtf16BE;
        }
    }
    if (odd_nul + even_nul == 0) {
        return TextEncoding::kCp1252;
    }
    return TextEncoding::kBinary;
}

std::string text_encoding_name(TextEncoding encoding)
{
    switch (encoding) {
        case TextEncoding::kUtf8:    return "UTF-8";
        case TextEncoding::kUtf16LE: return "UTF-16 (little-endian)";
        case TextEncoding::kUtf16BE: return "UTF-16 (big-endian)";
        case TextEncoding::kCp1252:  return "Windows-1252 (ANSI)";
        case TextEncoding::kBinary:  return "binary";
    }
    assert(false && "unhandled encoding");
    return "unknown";
}

namespace {

// UTF-16 code units in native order, BOM dropped.  Same `ok` contract as
// convert_to_utf8.
std::wstring utf16_units(const std::string& bytes, bool big_endian, bool& ok)
{
    ok = false;
    if (bytes.size() % 2 != 0) {
        return {};
    }
    std::size_t start = 0;
    if (bytes.size() >= 2) {
        const unsigned char b0 = static_cast<unsigned char>(bytes[0]);
        const unsigned char b1 = static_cast<unsigned char>(bytes[1]);
        if ((!big_endian && b0 == 0xFF && b1 == 0xFE) ||
            (big_endian && b0 == 0xFE && b1 == 0xFF)) {
            start = 2;
        }
    }
    std::wstring wide;
    wide.reserve((bytes.size() - start) / 2);
    for (std::size_t i = start; i + 1 < bytes.size(); i += 2) {
        const unsigned a = static_cast<unsigned char>(bytes[i]);
        const unsigned b = static_cast<unsigned char>(bytes[i + 1]);
        wide.push_back(static_cast<wchar_t>(big_endian ? (a << 8) | b
                                                       : (b << 8) | a));
    }
    ok = true;
    return wide;
}

}  // namespace

std::string convert_to_utf8(const std::string& bytes, TextEncoding encoding,
                            bool& ok)
{
    ok = false;
    if (bytes.size() > static_cast<std::size_t>(INT_MAX)) {
        return {};   // Win32 conversion APIs take int lengths
    }
    if (encoding == TextEncoding::kUtf8) {
        ok = true;
        return strip_utf8_bom(bytes);
    }
    if (encoding == TextEncoding::kBinary) {
        return {};
    }

    std::wstring wide;
    if (encoding == TextEncoding::kCp1252) {
        // The five bytes CP1252 leaves undefined fail the conversion
        // instead of being guessed at.  Checked by hand because Windows
        // maps them to C1 controls without raising MB_ERR_INVALID_CHARS.
        for (const char c : bytes) {
            const unsigned char b = static_cast<unsigned char>(c);
            if (b == 0x81 || b == 0x8D || b == 0x8F || b == 0x90 ||
                b == 0x9D) {
                return {};
            }
        }
        const int needed = ::MultiByteToWideChar(
            1252, MB_ERR_INVALID_CHARS, bytes.data(),
            static_cast<int>(bytes.size()), nullptr, 0);
        if (needed <= 0) {
            return {};
        }
        wide.resize(static_cast<std::size_t>(needed));
        const int got = ::MultiByteToWideChar(
            1252, MB_ERR_INVALID_CHARS, bytes.data(),
            static_cast<int>(bytes.size()), wide.data(), needed);
        if (got != needed) {
            return {};
        }
    } else {
        bool units_ok = false;
        wide = utf16_units(bytes, encoding == TextEncoding::kUtf16BE,
                           units_ok);
        if (!units_ok) {
            return {};
        }
    }

    if (wide.empty()) {
        ok = true;   // a BOM-only or empty file converts to an empty string
        return {};
    }
    // WC_ERR_INVALID_CHARS: unpaired surrogates fail rather than becoming
    // U+FFFD -- a wrong guess must be loud, not silently lossy.
    const int len = ::WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
        static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(len), '\0');
    const int written = ::WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
        static_cast<int>(wide.size()), out.data(), len, nullptr, nullptr);
    if (written != len) {
        return {};
    }
    ok = true;
    return out;
}

std::string write_text_file_checked(const std::string& path,
                                    const std::string& text)
{
    assert(!path.empty() && "write_text_file_checked needs a path");
    const std::size_t bad = first_invalid_utf8(text);
    if (bad != std::string::npos) {
        return "The text buffer is corrupt (not valid UTF-8 at byte " +
               std::to_string(bad) + "); the file was not touched.";
    }
    {
        std::ofstream stream(path_from_utf8(path),
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            return "Could not open the file for writing.";
        }
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        stream.close();
        if (!stream.good()) {
            return "The write failed part-way; the file may be incomplete.";
        }
    }
    std::ifstream back(path_from_utf8(path), std::ios::binary);
    if (!back) {
        return "The file could not be re-read to verify the save.";
    }
    std::ostringstream buffer;
    buffer << back.rdbuf();
    if (buffer.str() != text) {
        return "Verification failed: the bytes on disk do not match the "
               "saved text.";
    }
    return {};
}

std::string ensure_markdown_extension(const std::string& name)
{
    assert(!name.empty() && "a new document needs a name");
    if (name.empty()) {
        return name;
    }
    if (path_from_utf8(name).extension().empty()) {
        return name + ".md";
    }
    return name;
}

std::string filename_from_title(const std::string& title)
{
    // Long enough for any real title, short enough that the result plus its
    // folder stays clear of MAX_PATH on a deep tree.
    constexpr std::size_t kMaxStem = 100;

    std::string stem;
    stem.reserve(title.size());
    bool pending_space = false;
    for (std::size_t i = 0; i < title.size(); ++i) {   // bounded by the title
        const unsigned char ch = static_cast<unsigned char>(title[i]);
        // Inline Markdown markers carry no meaning in a filename.  Link text
        // is kept and the brackets dropped, so "[the rule](x.md)" would give
        // "the rule" -- but the URL is not stripped here, because a title
        // that is entirely a link is rare and half-removing one reads worse
        // than leaving it to the user to edit.
        if (ch == '*' || ch == '_' || ch == '`' || ch == '#' || ch == '[' ||
            ch == ']') {
            continue;
        }
        // Reserved on Windows, plus control characters.  A space stands in so
        // "Parse: the rule" does not become "Parsethe rule".
        if (ch < 0x20 || ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
            ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*') {
            pending_space = true;
            continue;
        }
        if (ch == ' ' || ch == '\t') {
            pending_space = true;
            continue;
        }
        if (pending_space && !stem.empty()) {
            stem += ' ';
        }
        pending_space = false;
        if (stem.size() >= kMaxStem) {
            break;
        }
        stem += static_cast<char>(ch);
    }

    // A trailing dot or space is legal to create through the API but the shell
    // strips it, leaving a file whose name is not the one on screen.
    while (!stem.empty() && (stem.back() == '.' || stem.back() == ' ')) {
        stem.pop_back();
    }
    if (stem.empty()) {
        return {};
    }

    // The DOS device names are still reserved, with or without an extension:
    // "CON.md" cannot be created.  Compared against the whole stem, since
    // "CONTENTS" is perfectly fine.
    static const char* const kReserved[] = {
        "con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4", "com5",
        "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5",
        "lpt6", "lpt7", "lpt8", "lpt9"};
    const std::string folded = to_lower(stem);
    for (const char* const name : kReserved) {   // bounded: a fixed list
        if (folded == name) {
            return {};
        }
    }

    return stem + ".md";
}

std::string markdown_image_link(const std::string& image_path,
                                const std::string& document_path)
{
    assert(!image_path.empty() && "an image link needs an image");
    if (image_path.empty()) {
        return {};
    }

    const fs::path image = path_from_utf8(image_path);
    std::string destination = path_to_utf8(image);

    // Relative to the document's own folder when that is expressible.  An
    // unsaved document has no folder to be relative to, and fs::relative
    // returns empty across drives -- both fall through to the absolute path.
    if (!document_path.empty()) {
        const fs::path folder = path_from_utf8(document_path).parent_path();
        if (!folder.empty()) {
            std::error_code ec;
            const fs::path relative = fs::relative(image, folder, ec);
            if (!ec && !relative.empty()) {
                destination = path_to_utf8(relative);
            }
        }
    }

    // A Markdown destination is a URL: backslashes are escape characters
    // there, and a Windows path full of them arrives at the renderer with the
    // separators eaten.
    for (char& ch : destination) {
        if (ch == '\\') {
            ch = '/';
        }
    }

    const bool needs_brackets =
        destination.find_first_of(" ()<>") != std::string::npos;
    const std::string alt = path_to_utf8(image.stem());

    std::string out = "![";
    out += alt;
    out += "](";
    if (needs_brackets) {
        out += '<';
        out += destination;
        out += '>';
    } else {
        out += destination;
    }
    out += ')';
    return out;
}

std::string find_inbox(const std::vector<std::string>& root_paths)
{
    const std::string target = to_lower(kInboxName);
    std::error_code ec;

    for (const std::string& root : root_paths) {
        if (root.empty()) {
            continue;
        }
        const fs::path dir = path_from_utf8(root);
        if (!fs::is_directory(dir, ec) || ec) {
            ec.clear();
            continue;
        }
        // A root may BE the inbox.  filename() on a path with a trailing
        // separator is empty, so the normalised form is what gets named.
        const fs::path named = dir.lexically_normal();
        if (to_lower(path_to_utf8(named.filename())) == target) {
            return path_to_utf8(dir);
        }

        fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            ec.clear();
            continue;
        }
        std::size_t seen = 0;
        const fs::directory_iterator end;
        while (it != end && seen < kMaxEntriesPerDir) {   // bounded (Rule of 10)
            ++seen;
            const fs::path entry = it->path();
            if (fs::is_directory(entry, ec) && !ec &&
                to_lower(path_to_utf8(entry.filename())) == target) {
                return path_to_utf8(entry);
            }
            ec.clear();
            it.increment(ec);
            if (ec) {
                break;   // increment() throws without this overload.
            }
        }
        ec.clear();
    }
    return {};
}

std::string unique_dest(const std::string& dest_dir,
                        const std::string& filename)
{
    assert(!dest_dir.empty() && "unique_dest needs a folder");
    assert(!filename.empty() && "unique_dest needs a filename");
    if (dest_dir.empty() || filename.empty()) {
        return {};
    }

    const fs::path dir = path_from_utf8(dest_dir);
    const fs::path name = path_from_utf8(filename);
    const fs::path stem = name.stem();
    const fs::path ext = name.extension();

    fs::path candidate = dir / name;
    std::error_code ec;
    // Bounded (Rule of 10).  Exhausting the range is not a silent overwrite:
    // the last candidate is returned and the copy fails visibly instead.
    for (int counter = 2; counter < 10000; ++counter) {
        if (!fs::exists(candidate, ec) || ec) {
            break;
        }
        // Back through path_from_utf8 rather than dividing by a std::string:
        // constructing a path from a narrow string re-encodes it as ANSI, so
        // a non-Latin-1 filename would be mangled on the way in.
        candidate = dir / path_from_utf8(path_to_utf8(stem) + " (" +
                                         std::to_string(counter) + ")" +
                                         path_to_utf8(ext));
    }
    return path_to_utf8(candidate);
}

bool operator==(const FileStamp& a, const FileStamp& b)
{
    // Two absent files compare equal whatever the other fields hold, so a
    // file that stays deleted is not reported as changing over and over.
    if (!a.exists || !b.exists) {
        return a.exists == b.exists;
    }
    return a.mtime_ticks == b.mtime_ticks && a.size == b.size;
}

bool operator!=(const FileStamp& a, const FileStamp& b)
{
    return !(a == b);
}

FileStamp stamp_of(const std::string& path)
{
    assert(!path.empty() && "stamp_of needs a path");
    FileStamp out;
    if (path.empty()) {
        return out;
    }

    const fs::path target = path_from_utf8(path);
    std::error_code ec;
    // Every one of these throws on failure without the error_code overload,
    // and a document can vanish between two of them, so each is checked
    // rather than relying on the status of the one before.
    if (!fs::is_regular_file(target, ec) || ec) {
        return out;
    }
    const std::uintmax_t size = fs::file_size(target, ec);
    if (ec) {
        return out;
    }
    const fs::file_time_type written = fs::last_write_time(target, ec);
    if (ec) {
        return out;
    }

    out.exists = true;
    out.size = static_cast<unsigned long long>(size);
    out.mtime_ticks = static_cast<long long>(written.time_since_epoch().count());
    return out;
}

}  // namespace mdboss
