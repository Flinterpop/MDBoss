// A guard against a mistake that has now been made twice.
//
// A narrow string literal containing a non-ASCII character is handed to
// wxString as raw bytes and decoded in the current ANSI code page, so
// "Filter files…" renders as "Filter filesâ€¦".  The /utf-8 compiler switch  (mojibake-ok: this is the example)
// does not help: it governs how the compiler reads the file, not how wx reads
// the bytes.  Wide literals (L"…") are unambiguous.
//
// It first appeared in the files-pane filter hint, was fixed, and then
// reappeared in the toolbar tooltips.  Scanning the sources is cheaper than
// noticing it in a screenshot a third time.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct Offence {
    std::string file;
    int line = 0;
    std::string text;
};

// Walk one line, tracking whether we are inside a string literal and whether
// that literal was introduced by an L prefix.
void scan_line(const std::string& line, const std::string& file, int number,
               std::vector<Offence>& out)
{
    // Ignore // comments: prose may legitimately contain non-ASCII.
    const std::size_t comment = line.find("//");
    const std::string code = line.substr(0, comment);

    bool inside = false;
    bool wide = false;
    for (std::size_t i = 0; i < code.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(code[i]);
        if (ch == '"' && (i == 0 || code[i - 1] != '\\')) {
            if (inside) {
                inside = false;
            } else {
                inside = true;
                wide = i > 0 && code[i - 1] == 'L';
            }
        } else if (inside && !wide && ch > 127) {
            out.push_back(Offence{file, number, line});
            return;
        }
    }
}

// ---- Encoding integrity --------------------------------------------------

std::string read_bytes(const fs::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

// Offset of the first byte that is not valid UTF-8, or npos.
std::size_t first_invalid_utf8(const std::string& text)
{
    std::size_t i = 0;
    while (i < text.size()) {   // bounded by the file (Rule of 10)
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        if (lead < 0x80) {
            extra = 0;
        } else if ((lead & 0xE0) == 0xC0 && lead >= 0xC2) {
            extra = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
        } else if ((lead & 0xF8) == 0xF0 && lead <= 0xF4) {
            extra = 3;
        } else {
            return i;   // a stray continuation byte, or an illegal lead
        }
        if (i + extra >= text.size()) {
            return i;   // truncated sequence at end of file
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) {
                return i;
            }
        }
        i += extra + 1;
    }
    return std::string::npos;
}

// Lines bearing the fingerprint of text decoded as CP1252 and re-encoded as
// UTF-8.
//
// PowerShell 5.1 does exactly this: Get-Content reads UTF-8 as the ANSI code
// page and Set-Content -Encoding utf8 writes the result back, so every
// typographic character in the file gains a layer.  "…" (E2 80 A6) becomes
// "â€¦", and "·" (C2 B7) becomes "Â·".  (mojibake-ok: this is the example)
//
// A line may carry the sequence on purpose -- one comment in the app quotes
// it to show what the bug looks like -- so an explicit marker exempts it.
// Deliberately a marker rather than a count: corruption is usually wholesale
// but need not be, and "fewer than N is fine" would let a small one through.
std::vector<int> double_encoded_lines(const std::string& text)
{
    // "â€": the double-encoded form of any E2 80 xx character -- the en and  (mojibake-ok: this is the example)
    // em dashes, curly quotes and the ellipsis this codebase uses.
    const std::string punctuation = "\xC3\xA2\xE2\x82\xAC";
    // "Â" followed by another C2-lead: the double-encoded form of C2 xx,
    // which covers the middle dot and the non-breaking space.
    const std::string latin1 = "\xC3\x82\xC2";

    std::vector<int> out;
    std::istringstream stream(text);
    std::string line;
    int number = 0;
    while (std::getline(stream, line)) {
        ++number;
        if (line.find("mojibake-ok") != std::string::npos) {
            continue;
        }
        if (line.find(punctuation) != std::string::npos ||
            line.find(latin1) != std::string::npos) {
            out.push_back(number);
        }
    }
    return out;
}

struct SourceFile {
    fs::path path;
    std::string label;
};

std::vector<SourceFile> text_sources()
{
    std::vector<SourceFile> out;
    // Widened after a rot check found a UTF-8 BOM sitting in two
    // CMakeLists.txt: the guard only ever walked app/ for .cpp/.h and assets/
    // for web files, so the build files and the whole mdrender/ tree were
    // never inspected at all.  CMake tolerates a BOM, which is exactly why
    // nothing complained -- it would have kept spreading silently.
    const std::pair<const char*, std::vector<std::string>> roots[] = {
        {MDBOSS_APP_DIR, {".cpp", ".h", ".txt"}},
        {MDBOSS_APP_DIR "/..", {".txt"}},
        {MDBOSS_APP_DIR "/../tests", {".cpp", ".txt"}},
        {MDBOSS_APP_DIR "/../mdrender", {".txt"}},
        {MDBOSS_APP_DIR "/../mdrender/src", {".cpp", ".h"}},
        {MDBOSS_APP_DIR "/../mdrender/include/mdrender", {".h"}},
        {MDBOSS_ASSET_DIR, {".html", ".css", ".js"}},
    };
    std::error_code ec;
    for (const auto& [dir, extensions] : roots) {
        for (const fs::directory_entry& entry :
             fs::directory_iterator(fs::path(dir), ec)) {
            const std::string name = entry.path().filename().string();
            for (const std::string& ext : extensions) {
                if (name.size() > ext.size() && name.ends_with(ext)) {
                    out.push_back(SourceFile{entry.path(), name});
                    break;
                }
            }
        }
    }
    return out;
}

}  // namespace

// The narrow-literal guard above only inspects code outside comments, and
// only narrow literals -- so it cannot see a file whose comments and wide
// literals have been mangled wholesale by an editing tool.  That happened:
// a PowerShell rewrite double-encoded every ellipsis and em dash in
// MainFrame.cpp, and nothing in the suite objected.
TEST_CASE("sources are undamaged UTF-8", "[sources]")
{
    const std::vector<SourceFile> files = text_sources();
    // Guard the guard: a bad path define would pass by scanning nothing.
    CHECK(files.size() >= 10);

    std::vector<std::string> problems;
    for (const SourceFile& file : files) {
        const std::string text = read_bytes(file.path);
        if (text.empty()) {
            continue;
        }

        // A BOM is itself evidence of a rewrite: none of these files are
        // authored with one, and the tools that add one are the same ones
        // that mangle the encoding.
        if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            problems.push_back(file.label + ": starts with a UTF-8 BOM");
        }

        const std::size_t bad = first_invalid_utf8(text);
        if (bad != std::string::npos) {
            problems.push_back(file.label + ": invalid UTF-8 at byte " +
                               std::to_string(bad));
        }

        for (const int line : double_encoded_lines(text)) {
            problems.push_back(file.label + ":" + std::to_string(line) +
                               " looks double-encoded (a CP1252 round trip); "
                               "add mojibake-ok if it is deliberate");
        }
    }

    for (const std::string& problem : problems) {
        UNSCOPED_INFO(problem);
    }
    CHECK(problems.empty());
}

TEST_CASE("no narrow string literal holds non-ASCII text", "[sources]")
{
    const fs::path root{MDBOSS_APP_DIR};
    REQUIRE(fs::is_directory(root));

    std::vector<Offence> offences;
    int scanned = 0;
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(root, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.size() < 3) {
            continue;
        }
        const bool source = name.ends_with(".cpp") || name.ends_with(".h");
        if (!source) {
            continue;
        }
        ++scanned;
        std::ifstream stream(entry.path(), std::ios::binary);
        std::string line;
        int number = 0;
        while (std::getline(stream, line)) {
            scan_line(line, name, ++number, offences);
        }
    }

    // Guard the guard: if the directory define ever points somewhere empty
    // this test would pass by scanning nothing.
    CHECK(scanned >= 10);

    for (const Offence& offence : offences) {
        UNSCOPED_INFO(offence.file << ":" << offence.line << "  "
                                   << offence.text);
    }
    CHECK(offences.empty());
}
