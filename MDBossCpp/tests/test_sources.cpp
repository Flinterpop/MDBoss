// A guard against a mistake that has now been made twice.
//
// A narrow string literal containing a non-ASCII character is handed to
// wxString as raw bytes and decoded in the current ANSI code page, so
// "Filter files…" renders as "Filter filesâ€¦".  The /utf-8 compiler switch
// does not help: it governs how the compiler reads the file, not how wx reads
// the bytes.  Wide literals (L"…") are unambiguous.
//
// It first appeared in the files-pane filter hint, was fixed, and then
// reappeared in the toolbar tooltips.  Scanning the sources is cheaper than
// noticing it in a screenshot a third time.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
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

}  // namespace

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
