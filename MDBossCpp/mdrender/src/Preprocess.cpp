// Source-level rewriting of the two block constructs CommonMark has no
// concept of: GitHub alerts and MkDocs/Material admonitions.
//
// mdrender.py implements these by injecting rules into markdown-it's token
// stream -- a core rule that retags a blockquote's open/close tokens to <div>,
// and a block rule that drives markdown-it's own indent bookkeeping.  md4c is
// a streaming parser with no token list and no rule-injection hook, so the
// same shapes are produced here by rewriting the source before it is parsed.
//
// The trick that makes this work is that CommonMark ends an HTML block at a
// blank line.  Emitting
//
//     <div class="admonition warning">
//     <p class="admonition-title">Title</p>
//     <blank>
//     body markdown
//     <blank>
//     </div>
//
// gives the body to the Markdown parser while the wrappers pass through
// verbatim, which is exactly the token shape the Python rules build.
//
// Known divergence: mdrender.py parses an admonition *title* as Markdown
// inline content, so `!!! note "A **bold** title"` emphasises there.  Here the
// title lands inside the HTML block and stays literal.  No document in the
// corpus or in the repo uses markup in a title; if one ever does, the fix is
// to render the title through a nested render_body() call.

#include <cassert>
#include <cstddef>
#include <unordered_map>

#include "Internal.h"

namespace mdrender::detail {
namespace {

std::size_t indent_of(std::string_view line)
{
    std::size_t n = 0;
    while (n < line.size() && (line[n] == ' ' || line[n] == '\t')) {
        ++n;
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

char lower(char ch)
{
    return (ch >= 'A' && ch <= 'Z')
               ? static_cast<char>(ch - 'A' + 'a')
               : ch;
}

std::string to_lower(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        out += lower(ch);
    }
    return out;
}

// "warning" -> "Warning" (Python str.capitalize(): rest lowercased).
std::string capitalize(std::string_view text)
{
    std::string out = to_lower(text);
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') {
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    }
    return out;
}

bool is_fence_line(std::string_view line, char& fence_char, std::size_t& run)
{
    const std::size_t indent = indent_of(line);
    if (indent > 3 || indent >= line.size()) {
        return false;
    }
    const char ch = line[indent];
    if (ch != '`' && ch != '~') {
        return false;
    }
    std::size_t n = 0;
    while (indent + n < line.size() && line[indent + n] == ch) {
        ++n;
    }
    if (n < 3) {
        return false;
    }
    fence_char = ch;
    run = n;
    return true;
}

// ---------------------------------------------------------------- alerts --

// The five GitHub alert kinds and their title labels, matching
// mdrender.py's _ALERT_LABELS.
const std::unordered_map<std::string, std::string>& alert_labels()
{
    static const std::unordered_map<std::string, std::string> labels = {
        {"note", "Note"},
        {"tip", "Tip"},
        {"important", "Important"},
        {"warning", "Warning"},
        {"caution", "Caution"},
    };
    return labels;
}

bool is_blockquote_line(std::string_view line)
{
    const std::size_t indent = indent_of(line);
    return indent <= 3 && indent < line.size() && line[indent] == '>';
}

// Remove one '>' marker and at most one following space.
std::string_view strip_quote(std::string_view line)
{
    const std::size_t indent = indent_of(line);
    if (indent >= line.size() || line[indent] != '>') {
        return line;
    }
    std::size_t at = indent + 1;
    if (at < line.size() && line[at] == ' ') {
        ++at;
    }
    return line.substr(at);
}

// If `line` is exactly an alert marker -- "[!NOTE]" and nothing else -- return
// the lowercased kind, else an empty string.  The "and nothing else" is load
// bearing: "> [!TIP] text on the same line" stays a plain blockquote in the
// Python renderer, and the corpus pins that down.
std::string alert_kind(std::string_view line)
{
    const std::string_view text = trim(line);
    if (text.size() < 4 || text.front() != '[' || text.back() != ']') {
        return {};
    }
    if (text[1] != '!') {
        return {};
    }
    const std::string kind = to_lower(text.substr(2, text.size() - 3));
    if (alert_labels().find(kind) == alert_labels().end()) {
        return {};
    }
    return kind;
}

// ----------------------------------------------------------- admonitions --

// MkDocs / Material type aliases -> canonical (styled) type, matching
// mdrender.py's _ADMON_ALIASES.
const std::unordered_map<std::string, std::string>& admon_aliases()
{
    static const std::unordered_map<std::string, std::string> aliases = {
        {"note", "note"}, {"seealso", "note"},
        {"abstract", "abstract"}, {"summary", "abstract"},
        {"tldr", "abstract"},
        {"info", "info"}, {"todo", "info"},
        {"tip", "tip"}, {"hint", "tip"}, {"important", "tip"},
        {"success", "success"}, {"check", "success"}, {"done", "success"},
        {"question", "question"}, {"help", "question"}, {"faq", "question"},
        {"warning", "warning"}, {"caution", "warning"},
        {"attention", "warning"},
        {"failure", "failure"}, {"fail", "failure"}, {"missing", "failure"},
        {"danger", "danger"}, {"error", "danger"},
        {"bug", "bug"},
        {"example", "example"},
        {"quote", "quote"}, {"cite", "quote"},
    };
    return aliases;
}

// The marker opening an admonition, or an empty view.
std::string_view admon_marker(std::string_view line)
{
    const std::size_t indent = indent_of(line);
    if (indent >= line.size()) {
        return {};
    }
    const std::string_view rest = line.substr(indent);
    constexpr std::string_view markers[] = {"???+", "!!!", "???"};
    for (const std::string_view marker : markers) {
        if (rest.compare(0, marker.size(), marker) == 0) {
            return rest.substr(0, marker.size());
        }
    }
    return {};
}

struct AdmonHead {
    std::string canonical;
    std::string title;
    bool has_title = false;
};

// Port of _admon_type_and_title(): parse the parameters after the marker.
AdmonHead parse_admon_params(std::string_view params)
{
    AdmonHead head;
    const std::string_view text = trim(params);
    if (text.empty()) {
        head.canonical = "note";
        head.title = "Note";
        head.has_title = true;
        return head;
    }
    const std::size_t sep = text.find(' ');
    const std::string_view keyword =
        (sep == std::string_view::npos) ? text : text.substr(0, sep);
    const std::string key = to_lower(keyword);
    const auto found = admon_aliases().find(key);
    head.canonical = (found == admon_aliases().end()) ? key : found->second;

    const std::string_view rest =
        (sep == std::string_view::npos) ? std::string_view{}
                                        : trim(text.substr(sep + 1));
    if (rest.empty()) {
        head.title = capitalize(keyword);
        head.has_title = true;
        return head;
    }
    // A title wrapped in matching quotes; an empty one suppresses the bar.
    if (rest.size() >= 2 && (rest.front() == '"' || rest.front() == '\'') &&
        rest.back() == rest.front()) {
        const std::string_view body = rest.substr(1, rest.size() - 2);
        head.title = std::string(body);
        head.has_title = !body.empty();
        return head;
    }
    head.title = std::string(rest);
    head.has_title = true;
    return head;
}

}  // namespace

std::string preprocess(std::string_view md_text)
{
    const std::vector<std::string_view> lines = split_lines(md_text);
    std::string out;
    out.reserve(md_text.size() + md_text.size() / 4);

    bool in_fence = false;
    char fence_char = '\0';
    std::size_t fence_len = 0;

    std::size_t i = 0;
    std::size_t bound = 0;
    while (i < lines.size()) {
        ++bound;
        assert(bound <= kMaxLines && "preprocess loop is bounded");
        const std::string_view line = lines[i];

        char ch = '\0';
        std::size_t run = 0;
        if (is_fence_line(line, ch, run)) {
            if (in_fence) {
                if (ch == fence_char && run >= fence_len) {
                    in_fence = false;
                }
            } else {
                in_fence = true;
                fence_char = ch;
                fence_len = run;
            }
            out += line;
            out += '\n';
            ++i;
            continue;
        }
        if (in_fence) {
            out += line;
            out += '\n';
            ++i;
            continue;
        }

        // --------------------------------------------------- block math --
        // mdit-py-plugins' dollarmath makes a `$$...$$` that starts a line a
        // *block* token, so it renders as a top-level <div class=
        // "math-display">.  md4c's MD_FLAG_LATEXMATHSPANS only ever produces
        // spans, which would wrap the same content in a <p>.  Emitting the
        // div here as a raw HTML block reproduces the Python shape; nothing
        // inside math is Markdown, so nothing is lost by not parsing it.
        if (trim(line).substr(0, 2) == "$$") {
            const std::size_t open = line.find("$$");
            std::string math;
            std::size_t end = i;
            const std::size_t close_same =
                line.find("$$", open + 2);
            if (close_same != std::string_view::npos) {
                math = std::string(
                    trim(line.substr(open + 2, close_same - open - 2)));
            } else {
                math = std::string(trim(line.substr(open + 2)));
                ++end;
                while (end < lines.size()) {
                    const std::size_t close = lines[end].find("$$");
                    if (close != std::string_view::npos) {
                        const std::string_view tail =
                            trim(lines[end].substr(0, close));
                        if (!tail.empty()) {
                            if (!math.empty()) {
                                math += '\n';
                            }
                            math += std::string(tail);
                        }
                        break;
                    }
                    if (!math.empty()) {
                        math += '\n';
                    }
                    math += std::string(lines[end]);
                    ++end;
                }
            }
            if (end < lines.size()) {
                out += "\n<div class=\"math-display\">";
                out += escape_html_quoted(math);
                out += "</div>\n\n";
                i = end + 1;
                continue;
            }
        }

        // ------------------------------------------------------- alerts --
        if (is_blockquote_line(line)) {
            std::size_t end = i;
            while (end < lines.size() && is_blockquote_line(lines[end])) {
                ++end;
            }
            std::vector<std::string_view> body;
            body.reserve(end - i);
            for (std::size_t j = i; j < end; ++j) {
                body.push_back(strip_quote(lines[j]));
            }
            std::size_t first = 0;
            while (first < body.size() && trim(body[first]).empty()) {
                ++first;
            }
            const std::string kind =
                (first < body.size()) ? alert_kind(body[first])
                                      : std::string{};
            if (kind.empty()) {
                for (std::size_t j = i; j < end; ++j) {
                    out += lines[j];
                    out += '\n';
                }
                i = end;
                continue;
            }
            out += "<div class=\"markdown-alert markdown-alert-";
            out += kind;
            out += "\">\n<p class=\"markdown-alert-title\">";
            out += alert_labels().at(kind);
            out += "</p>\n\n";
            for (std::size_t j = first + 1; j < body.size(); ++j) {
                out += body[j];
                out += '\n';
            }
            out += "\n</div>\n";
            i = end;
            continue;
        }

        // -------------------------------------------------- admonitions --
        const std::string_view marker = admon_marker(line);
        if (!marker.empty()) {
            const std::size_t indent = indent_of(line);
            const std::string_view params =
                line.substr(indent + marker.size());
            if (trim(params).empty() && marker != "!!!") {
                out += line;
                out += '\n';
                ++i;
                continue;
            }
            const AdmonHead head = parse_admon_params(params);
            if (head.canonical.empty()) {
                out += line;
                out += '\n';
                ++i;
                continue;
            }

            // Body: following lines indented at least 4 past the marker, with
            // blank lines allowed; two consecutive blanks end the block, as
            // does the first under-indented non-blank line.
            const std::size_t body_indent = indent + 4;
            std::size_t end = i + 1;
            bool was_blank = false;
            while (end < lines.size()) {
                const bool blank = trim(lines[end]).empty();
                if (blank && was_blank) {
                    break;
                }
                if (!blank && indent_of(lines[end]) < body_indent) {
                    break;
                }
                was_blank = blank;
                ++end;
            }

            const bool collapsible = marker.front() == '?';
            const bool open = marker == "???+";
            out += collapsible ? "<details class=\"admonition "
                               : "<div class=\"admonition ";
            out += escape_html_quoted(head.canonical);
            out += "\"";
            out += (collapsible && open) ? " open" : "";
            out += ">\n";
            if (head.has_title) {
                out += collapsible ? "<summary class=\"admonition-title\">"
                                   : "<p class=\"admonition-title\">";
                out += escape_html(head.title);
                out += collapsible ? "</summary>\n" : "</p>\n";
            }
            out += "\n";
            for (std::size_t j = i + 1; j < end; ++j) {
                const std::string_view body_line = lines[j];
                out += (indent_of(body_line) >= body_indent)
                           ? body_line.substr(body_indent)
                           : trim(body_line);
                out += '\n';
            }
            out += "\n";
            out += collapsible ? "</details>\n" : "</div>\n";
            i = end;
            continue;
        }

        out += line;
        out += '\n';
        ++i;
    }

    return out;
}

}  // namespace mdrender::detail
