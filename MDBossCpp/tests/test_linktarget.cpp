// What a link in the preview is allowed to point at.
//
// This is the parser that decides whether a `file:` URL out of an untrusted
// document gets acted on, so the negative cases matter more than the positive
// one.  It only became testable when it was lifted out of PreviewPane.cpp,
// which is the whole reason this file exists.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "LinkTarget.h"

TEST_CASE("a file: URL naming a Markdown document decodes to its path",
          "[linktarget]")
{
    CHECK(mdboss::markdown_path_from_file_url("file:///C:/notes/a.md") ==
          "C:\\notes\\a.md");
    // Every extension the open dialog offers, and case does not matter.
    for (const char* ext : {".md", ".markdown", ".mdown", ".mkd", ".mdwn"}) {
        const std::string uri = std::string("file:///C:/n/a") + ext;
        INFO(uri);
        CHECK_FALSE(mdboss::markdown_path_from_file_url(uri).empty());
    }
    CHECK(mdboss::markdown_path_from_file_url("file:///C:/n/A.MD") ==
          "C:\\n\\A.MD");
}

TEST_CASE("percent escapes decode as BYTES, not as characters", "[linktarget]")
{
    // A space is the everyday case.
    CHECK(mdboss::markdown_path_from_file_url("file:///C:/my%20notes/a%20b.md")
          == "C:\\my notes\\a b.md");

    // The case that matters: a path outside ASCII is percent-encoded as UTF-8,
    // so %C3%A9 is ONE character (e-acute) made of TWO bytes.  Decoding each
    // byte into a wide character instead produces two wrong characters and a
    // path that does not exist -- which is what the version of this code that
    // lived inside PreviewPane.cpp did, unnoticed, because nothing could test
    // it.
    const std::string got =
        mdboss::markdown_path_from_file_url("file:///C:/n/caf%C3%A9.md");
    CHECK(got == std::string("C:\\n\\caf\xC3\xA9.md"));
    REQUIRE(got.size() == 13);   // 5 + "caf" + 2 bytes + ".md"
    // Two bytes for the one accented character, not one and not four.
    CHECK(static_cast<unsigned char>(got[8]) == 0xC3);
    CHECK(static_cast<unsigned char>(got[9]) == 0xA9);
}

TEST_CASE("only Markdown is ever returned", "[linktarget][safety]")
{
    // The safety property.  ShellExecute is never handed a file: URL, and the
    // caller opens whatever comes back, so anything runnable must come back
    // empty and keep behaving exactly as it did before.
    for (const char* uri : {"file:///C:/Windows/System32/cmd.exe",
                            "file:///C:/x/run.bat",
                            "file:///C:/x/run.cmd",
                            "file:///C:/x/script.ps1",
                            "file:///C:/x/setup.msi",
                            "file:///C:/x/doc.pdf",
                            "file:///C:/x/page.html",
                            "file:///C:/x/note.md.exe",
                            "file:///C:/x/noextension"}) {
        INFO(uri);
        CHECK(mdboss::markdown_path_from_file_url(uri).empty());
    }
}

TEST_CASE("a non-file scheme is never a path", "[linktarget][safety]")
{
    for (const char* uri : {"https://example.com/a.md",
                            "http://example.com/a.md",
                            "javascript:alert(1)//a.md",
                            "data:text/plain,a.md",
                            "vbscript:x.md",
                            "mailto:someone@example.com",
                            "about:blank",
                            ""}) {
        INFO(uri);
        CHECK(mdboss::markdown_path_from_file_url(uri).empty());
    }
}

TEST_CASE("a UNC file:// URL is not followed", "[linktarget][safety]")
{
    // Two slashes, not three.  Not refused because it is dangerous, but
    // because it has never been supported -- and quietly starting to open
    // documents off an arbitrary host would be a change nobody asked for.
    CHECK(mdboss::markdown_path_from_file_url("file://server/share/a.md")
              .empty());
    CHECK(mdboss::markdown_path_from_file_url("file:///").empty());
    CHECK(mdboss::markdown_path_from_file_url("file://").empty());
}

TEST_CASE("an embedded NUL is refused outright", "[linktarget][safety]")
{
    // %00 is the classic way to make a checked string and the string that
    // actually gets used disagree: everything after it disappears when the
    // path reaches an API that stops at a NUL, so "a.exe%00.md" would pass an
    // extension check and open something else.
    CHECK(mdboss::markdown_path_from_file_url("file:///C:/x/a.exe%00.md")
              .empty());
    CHECK(mdboss::markdown_path_from_file_url("file:///C:/x/a%00.md").empty());
}

TEST_CASE("a query or fragment is not part of the name", "[linktarget]")
{
    CHECK(mdboss::markdown_path_from_file_url("file:///C:/n/a.md#heading") ==
          "C:\\n\\a.md");
    CHECK(mdboss::markdown_path_from_file_url("file:///C:/n/a.md?v=2") ==
          "C:\\n\\a.md");
    // ...and one that removes the extension leaves nothing to open.
    CHECK(mdboss::markdown_path_from_file_url("file:///C:/n/a#x.md").empty());
}

TEST_CASE("a dot in a folder name is not an extension", "[linktarget]")
{
    // A path ending "a.b/readme" has no extension; reading ".b/readme" as one
    // and then failing to match Markdown is the right answer by luck, not by
    // design -- so it is decided explicitly.
    CHECK(mdboss::markdown_path_from_file_url("file:///C:/a.b/readme").empty());
    CHECK(mdboss::markdown_path_from_file_url("file:///C:/a.md/readme")
              .empty());
    // A dotted FOLDER with a real document inside it still works.
    CHECK(mdboss::markdown_path_from_file_url("file:///C:/a.b/readme.md") ==
          "C:\\a.b\\readme.md");
}

TEST_CASE("an extension with no name in front of it is not a document",
          "[linktarget]")
{
    CHECK(mdboss::markdown_path_from_file_url("file:///C:/x/.md").empty());
}

TEST_CASE("a stray percent is kept rather than dropped", "[linktarget]")
{
    // Windows allows '%' in a file name, and "100%" is a plausible one.  It is
    // only an escape when two hex digits follow.
    CHECK(mdboss::markdown_path_from_file_url("file:///C:/n/100%.md") ==
          "C:\\n\\100%.md");
    CHECK(mdboss::markdown_path_from_file_url("file:///C:/n/a%zz.md") ==
          "C:\\n\\a%zz.md");
}
