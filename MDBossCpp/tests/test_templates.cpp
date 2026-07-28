// Tests for new-document template substitution.

#include <catch2/catch_test_macros.hpp>

#include <ctime>
#include <string>

#include "Templates.h"

namespace {

// A fixed local time, so the expected output is not a moving target.
std::tm fixed_time()
{
    std::tm when{};
    when.tm_year = 126;   // 2026
    when.tm_mon = 6;      // July
    when.tm_mday = 28;
    when.tm_hour = 9;
    when.tm_min = 5;
    when.tm_sec = 0;
    when.tm_isdst = -1;
    return when;
}

}  // namespace

TEST_CASE("placeholders are substituted", "[templates]")
{
    const std::tm when = fixed_time();
    CHECK(mdboss::apply_template("# {{title}}", "Report", when) == "# Report");
    CHECK(mdboss::apply_template("{{date}}", "x", when) == "2026-07-28");
    CHECK(mdboss::apply_template("{{time}}", "x", when) == "09:05");
    CHECK(mdboss::apply_template("{{datetime}}", "x", when) ==
          "2026-07-28 09:05");
}

TEST_CASE("no placeholder is a substring of another", "[templates]")
{
    // {{datetime}} must survive the {{date}} and {{time}} passes intact.  If
    // a future placeholder overlaps an existing one, substitution order
    // starts to matter and this catches it.
    const std::tm when = fixed_time();
    CHECK(mdboss::apply_template("{{datetime}}|{{date}}|{{time}}", "x",
                                 when) ==
          "2026-07-28 09:05|2026-07-28|09:05");
}

TEST_CASE("every occurrence is replaced", "[templates]")
{
    const std::tm when = fixed_time();
    // The "Document" starter uses {{title}} twice, once in front matter and
    // once as the heading.
    CHECK(mdboss::apply_template("{{title}} {{title}} {{title}}", "A", when) ==
          "A A A");
}

TEST_CASE("text with no placeholders is untouched", "[templates]")
{
    const std::tm when = fixed_time();
    const std::string text = "# Plain\n\nNothing to substitute { } {{ }}.\n";
    CHECK(mdboss::apply_template(text, "ignored", when) == text);
}

TEST_CASE("an empty title substitutes as empty, not as the token",
          "[templates]")
{
    const std::tm when = fixed_time();
    CHECK(mdboss::apply_template("[{{title}}]", "", when) == "[]");
}
