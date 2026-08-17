// md4c -> HTML, shaped to match markdown-it's output byte for byte.
//
// md4c ships its own md_html.c, but it renders HTML5-style void elements
// (<img>, <hr>) while markdown-it's commonmark preset uses XHTML (<img />,
// <hr />), and it has no notion of the anchor ids, mermaid fences or tight-list
// newline placement mdrender.py produces.  So the callbacks are written here
// rather than vendored and patched.
//
// Two markdown-it behaviours that are easy to miss and are reproduced exactly:
//
//  * In a *tight* list markdown-it hides the paragraph tokens rather than
//    dropping them, and gives a block token that follows a hidden one a
//    leading newline -- which is why "<li>item one\n<ul>" has a newline that
//    "<li>item two</li>" does not.  md4c instead omits MD_BLOCK_P inside a
//    tight item entirely, so the same newline has to be tracked directly;
//    see Renderer::pending_lf.
//  * Heading ids come from the pre-scanned source headings, consumed in
//    document order -- see Outline.cpp for why they cannot be derived here.

#include <cassert>
#include <cstddef>
#include <cstring>

#include <md4c.h>

#include "Internal.h"

namespace mdrender::detail {
namespace {

struct Renderer {
    std::string out;
    const std::vector<ScannedHeading>* headings = nullptr;
    std::size_t heading_index = 0;

    // is_tight per open list, innermost last.
    std::vector<bool> list_tight;
    // Depth of open list items, so a paragraph knows if it is inside one.
    std::size_t item_depth = 0;
    // Set when the next block-level tag should be preceded by a newline.
    //
    // markdown-it decides this at the *previous* token: a block token whose
    // predecessor was hidden gets a leading newline, and an opening block tag
    // whose successor is neither inline nor hidden gets a trailing one.  Both
    // reduce to "a newline sits between inline content and a following block
    // tag inside a list item", which is what this flag tracks.
    //
    // It cannot be driven off paragraph tokens the way the Python side is:
    // md4c omits MD_BLOCK_P inside a tight list item altogether rather than
    // emitting a hidden one, so there is no token to hang the decision on.
    bool pending_lf = false;
    // Inside an image span the content becomes the alt attribute instead of
    // markup, so output is diverted here.
    std::size_t image_depth = 0;
    std::string alt_text;
    // Fence bodies are escaped by mdrender.py's own _render_fence (Python's
    // html.escape, apostrophes included) while inline code spans are escaped
    // by markdown-it (apostrophes left alone), and md4c reports both as
    // MD_TEXT_CODE.  This tells them apart.
    std::size_t code_block_depth = 0;

    bool in_tight_item() const
    {
        return item_depth > 0 && !list_tight.empty() && list_tight.back();
    }

    // Append a block-level closing tag.  Never separated by a newline from
    // what precedes it, so it clears any pending one.
    void raw(std::string_view text)
    {
        if (image_depth > 0) {
            return;   // markup inside alt text is dropped, as markdown-it does
        }
        out += text;
        pending_lf = false;
    }

    // Append an inline tag.  Inside a tight list item this leaves a newline
    // pending, so a block that follows the item's inline content is separated
    // from it; anywhere else the paragraph tags already do that job.
    void inline_raw(std::string_view text)
    {
        if (image_depth > 0) {
            return;
        }
        out += text;
        pending_lf = in_tight_item();
    }

    // Append text content, honouring the image-alt diversion.
    void text_out(std::string_view text)
    {
        if (image_depth > 0) {
            alt_text += text;
            return;
        }
        out += text;
        pending_lf = in_tight_item();
    }

    // Open (or self-close) a block-level tag.
    void block(std::string_view tag)
    {
        if (image_depth > 0) {
            return;
        }
        if (pending_lf) {
            out += '\n';
        }
        out += tag;
        pending_lf = false;
    }
};

std::string attribute_text(const MD_ATTRIBUTE& attr)
{
    std::string out;
    if (attr.text == nullptr) {
        return out;
    }
    out.assign(attr.text, attr.size);
    return out;
}

// The language word of a fence info string: "python title=x" -> "python".
std::string fence_language(const MD_ATTRIBUTE& info)
{
    const std::string text = attribute_text(info);
    std::size_t begin = 0;
    while (begin < text.size() && text[begin] == ' ') {
        ++begin;
    }
    std::size_t end = begin;
    while (end < text.size() && text[end] != ' ' && text[end] != '\t') {
        ++end;
    }
    std::string lang = text.substr(begin, end - begin);
    for (char& ch : lang) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return lang;
}

const char* align_style(MD_ALIGN align)
{
    switch (align) {
    case MD_ALIGN_LEFT: return " style=\"text-align:left\"";
    case MD_ALIGN_CENTER: return " style=\"text-align:center\"";
    case MD_ALIGN_RIGHT: return " style=\"text-align:right\"";
    case MD_ALIGN_DEFAULT:
    default: return "";
    }
}

// ------------------------------------------------------------- callbacks --

int enter_block(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    Renderer& r = *static_cast<Renderer*>(userdata);
    switch (type) {
    case MD_BLOCK_DOC:
        break;
    case MD_BLOCK_QUOTE:
        r.block("<blockquote>\n");
        break;
    case MD_BLOCK_UL: {
        const auto* ul = static_cast<MD_BLOCK_UL_DETAIL*>(detail);
        r.list_tight.push_back(ul != nullptr && ul->is_tight != 0);
        r.block("<ul>\n");
        break;
    }
    case MD_BLOCK_OL: {
        const auto* ol = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
        r.list_tight.push_back(ol != nullptr && ol->is_tight != 0);
        if (ol != nullptr && ol->start != 1) {
            r.block("<ol start=\"" + std::to_string(ol->start) + "\">\n");
        } else {
            r.block("<ol>\n");
        }
        break;
    }
    case MD_BLOCK_LI: {
        ++r.item_depth;
        const auto* li = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
        if (li != nullptr && li->is_task != 0) {
            // A GitHub task list item.  Without this the source "- [ ] thing"
            // renders as a bullet with a literal "[ ]" in it, which is what
            // MD Boss did until the Lists menu started writing checklists.
            // Disabled, as GitHub renders them: the preview is a view of the
            // file, and a box you could tick without the file changing would
            // be lying.
            const bool ticked =
                li->task_mark == 'x' || li->task_mark == 'X';
            r.block("<li class=\"task-list-item\">");
            r.raw(std::string("<input type=\"checkbox\" disabled=\"\"") +
                  (ticked ? " checked=\"\"" : "") + " /> ");
            // No pending newline: the checkbox is inline content, so the item
            // has already opened inline.
        } else {
            r.block("<li>");
            // markdown-it writes "<li>\n" unless the item opens with inline
            // content, so leave the newline pending for the next tag to
            // decide.
            r.pending_lf = true;
        }
        break;
    }
    case MD_BLOCK_HR:
        r.block("<hr />\n");
        break;
    case MD_BLOCK_H: {
        const auto* h = static_cast<MD_BLOCK_H_DETAIL*>(detail);
        const int level = (h != nullptr) ? static_cast<int>(h->level) : 1;
        std::string tag = "<h" + std::to_string(level);
        if (r.headings != nullptr && r.heading_index < r.headings->size()) {
            tag += " id=\"";
            tag += escape_html((*r.headings)[r.heading_index].slug);
            tag += "\"";
        }
        ++r.heading_index;
        tag += ">";
        r.block(tag);
        break;
    }
    case MD_BLOCK_CODE: {
        const auto* code = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
        const std::string lang =
            (code != nullptr) ? fence_language(code->lang) : std::string{};
        ++r.code_block_depth;
        if (lang == "mermaid") {
            r.block("<pre class=\"mermaid\">");
        } else if (lang.empty()) {
            r.block("<pre><code>");
        } else {
            r.block("<pre><code class=\"language-" +
                    escape_html_quoted(lang) + "\">");
        }
        break;
    }
    case MD_BLOCK_HTML:
        // Raw HTML passes through; no wrapper of our own.
        if (r.pending_lf) {
            r.out += '\n';
            r.pending_lf = false;
        }
        break;
    case MD_BLOCK_P:
        if (!r.in_tight_item()) {
            r.block("<p>");
        }
        break;
    case MD_BLOCK_TABLE:
        r.block("<table>\n");
        break;
    case MD_BLOCK_THEAD:
        r.block("<thead>\n");
        break;
    case MD_BLOCK_TBODY:
        r.block("<tbody>\n");
        break;
    case MD_BLOCK_TR:
        r.block("<tr>\n");
        break;
    case MD_BLOCK_TH: {
        const auto* td = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
        r.block(std::string("<th") +
                (td != nullptr ? align_style(td->align) : "") + ">");
        break;
    }
    case MD_BLOCK_TD: {
        const auto* td = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
        r.block(std::string("<td") +
                (td != nullptr ? align_style(td->align) : "") + ">");
        break;
    }
    default:
        break;
    }
    return 0;
}

int leave_block(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    static_cast<void>(detail);
    Renderer& r = *static_cast<Renderer*>(userdata);
    switch (type) {
    case MD_BLOCK_DOC:
        break;
    case MD_BLOCK_QUOTE:
        r.raw("</blockquote>\n");
        break;
    case MD_BLOCK_UL:
        if (!r.list_tight.empty()) {
            r.list_tight.pop_back();
        }
        r.raw("</ul>\n");
        break;
    case MD_BLOCK_OL:
        if (!r.list_tight.empty()) {
            r.list_tight.pop_back();
        }
        r.raw("</ol>\n");
        break;
    case MD_BLOCK_LI:
        assert(r.item_depth > 0 && "list item nesting is balanced");
        if (r.item_depth > 0) {
            --r.item_depth;
        }
        r.raw("</li>\n");
        break;
    case MD_BLOCK_HR:
        break;
    case MD_BLOCK_H: {
        const auto* h = static_cast<MD_BLOCK_H_DETAIL*>(detail);
        const int level = (h != nullptr) ? static_cast<int>(h->level) : 1;
        r.raw("</h" + std::to_string(level) + ">\n");
        break;
    }
    case MD_BLOCK_CODE: {
        const auto* code = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
        const std::string lang =
            (code != nullptr) ? fence_language(code->lang) : std::string{};
        assert(r.code_block_depth > 0 && "code block nesting is balanced");
        if (r.code_block_depth > 0) {
            --r.code_block_depth;
        }
        r.raw(lang == "mermaid" ? "</pre>\n" : "</code></pre>\n");
        break;
    }
    case MD_BLOCK_HTML:
        break;
    case MD_BLOCK_P:
        if (!r.in_tight_item()) {
            r.raw("</p>\n");
        }
        break;
    case MD_BLOCK_TABLE:
        r.raw("</table>\n");
        break;
    case MD_BLOCK_THEAD:
        r.raw("</thead>\n");
        break;
    case MD_BLOCK_TBODY:
        r.raw("</tbody>\n");
        break;
    case MD_BLOCK_TR:
        r.raw("</tr>\n");
        break;
    case MD_BLOCK_TH:
        r.raw("</th>\n");
        break;
    case MD_BLOCK_TD:
        r.raw("</td>\n");
        break;
    default:
        break;
    }
    return 0;
}

int enter_span(MD_SPANTYPE type, void* detail, void* userdata)
{
    Renderer& r = *static_cast<Renderer*>(userdata);
    switch (type) {
    case MD_SPAN_EM:
        r.inline_raw("<em>");
        break;
    case MD_SPAN_STRONG:
        r.inline_raw("<strong>");
        break;
    case MD_SPAN_DEL:
        r.inline_raw("<s>");
        break;
    case MD_SPAN_U:
        r.inline_raw("<u>");
        break;
    case MD_SPAN_CODE:
        r.inline_raw("<code>");
        break;
    case MD_SPAN_A: {
        const auto* a = static_cast<MD_SPAN_A_DETAIL*>(detail);
        std::string tag = "<a href=\"";
        if (a != nullptr) {
            tag += escape_html(attribute_text(a->href));
        }
        tag += "\"";
        if (a != nullptr && a->title.text != nullptr && a->title.size > 0) {
            tag += " title=\"" + escape_html(attribute_text(a->title)) + "\"";
        }
        tag += ">";
        r.inline_raw(tag);
        break;
    }
    case MD_SPAN_IMG:
        // Content between enter and leave is the alt text, not markup.
        ++r.image_depth;
        break;
    case MD_SPAN_LATEXMATH:
        r.inline_raw("<span class=\"math-inline\">");
        break;
    case MD_SPAN_LATEXMATH_DISPLAY:
        r.inline_raw("<div class=\"math-display\">");
        break;
    default:
        break;
    }
    return 0;
}

int leave_span(MD_SPANTYPE type, void* detail, void* userdata)
{
    Renderer& r = *static_cast<Renderer*>(userdata);
    switch (type) {
    case MD_SPAN_EM:
        r.inline_raw("</em>");
        break;
    case MD_SPAN_STRONG:
        r.inline_raw("</strong>");
        break;
    case MD_SPAN_DEL:
        r.inline_raw("</s>");
        break;
    case MD_SPAN_U:
        r.inline_raw("</u>");
        break;
    case MD_SPAN_CODE:
        r.inline_raw("</code>");
        break;
    case MD_SPAN_A:
        r.inline_raw("</a>");
        break;
    case MD_SPAN_IMG: {
        assert(r.image_depth > 0 && "image nesting is balanced");
        const auto* img = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
        const std::string alt = r.alt_text;
        r.alt_text.clear();
        if (r.image_depth > 0) {
            --r.image_depth;
        }
        std::string tag = "<img src=\"";
        if (img != nullptr) {
            tag += escape_html(attribute_text(img->src));
        }
        tag += "\" alt=\"" + escape_html(alt) + "\"";
        if (img != nullptr && img->title.text != nullptr &&
            img->title.size > 0) {
            tag += " title=\"" + escape_html(attribute_text(img->title)) +
                   "\"";
        }
        tag += " />";
        r.inline_raw(tag);
        break;
    }
    case MD_SPAN_LATEXMATH:
        r.inline_raw("</span>");
        break;
    case MD_SPAN_LATEXMATH_DISPLAY:
        r.inline_raw("</div>\n");
        break;
    default:
        break;
    }
    return 0;
}

int on_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size,
            void* userdata)
{
    Renderer& r = *static_cast<Renderer*>(userdata);
    const std::string_view chunk(text, size);
    switch (type) {
    case MD_TEXT_NULLCHAR:
        r.text_out("\xEF\xBF\xBD");   // U+FFFD
        break;
    case MD_TEXT_BR:
        r.inline_raw("<br />\n");
        break;
    case MD_TEXT_SOFTBR:
        r.text_out("\n");
        break;
    case MD_TEXT_HTML:
        // Raw HTML is emitted verbatim; sanitize_html() strips the active
        // parts afterwards, exactly as the Python pipeline does.
        if (r.image_depth > 0) {
            break;
        }
        r.out += chunk;
        r.pending_lf = false;
        break;
    case MD_TEXT_ENTITY:
        // markdown-it re-emits entities unchanged, so a browser resolves them.
        r.text_out(chunk);
        break;
    case MD_TEXT_CODE:
        // Fence bodies go through Python's html.escape in _render_fence;
        // inline code spans go through markdown-it's escapeHtml.
        r.text_out(r.code_block_depth > 0 ? escape_html_quoted(chunk)
                                          : escape_html(chunk));
        break;
    case MD_TEXT_LATEXMATH:
        // _render_math_inline / _render_math_block use html.escape.
        r.text_out(escape_html_quoted(chunk));
        break;
    case MD_TEXT_NORMAL:
    default:
        r.text_out(escape_html(chunk));
        break;
    }
    return 0;
}

}  // namespace

bool render_html(std::string_view md_text,
                 const std::vector<ScannedHeading>& headings,
                 std::string& out)
{
    Renderer renderer;
    renderer.headings = &headings;

    MD_PARSER parser;
    std::memset(&parser, 0, sizeof(parser));
    parser.abi_version = 0;
    // TASKLISTS is what turns "- [ ] thing" into a checkbox rather than a
    // bullet with literal brackets.  Added when the Lists menu began writing
    // ToDoList.md as a checklist, but it is not specific to that file -- any
    // document using GitHub's task-list syntax rendered wrong before, and the
    // whole point of this preview is to look like GitHub.  No golden corpus
    // file uses the syntax, so enabling it changes no frozen expectation.
    // PERMISSIVEAUTOLINKS turns a bare http://, www. or user@host into a real
    // link, as GitHub does.  Added because logins.md keeps its Link column as
    // bare URLs -- a table cell is a poor place to type [text](url) by hand --
    // and a URL you cannot click is not a link, it is a string to retype.  Not
    // specific to that file: any document listing a URL was rendering it as
    // plain text.  Only the corpus's sanitizer case contains one, inside a raw
    // <iframe> attribute, which is an HTML block and so out of reach of a span
    // rule -- confirmed by regenerating nothing: the frozen expectation still
    // matches byte for byte.
    //
    // It cannot widen what a click can reach.  The permissive rules match only
    // http/https/ftp and mailto shapes, so no javascript: URL can be minted
    // this way, and PreviewPane's handler allow-lists http, https and mailto
    // before handing anything to ShellExecute.
    parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH |
                   MD_FLAG_LATEXMATHSPANS | MD_FLAG_TASKLISTS |
                   MD_FLAG_PERMISSIVEAUTOLINKS;
    parser.enter_block = &enter_block;
    parser.leave_block = &leave_block;
    parser.enter_span = &enter_span;
    parser.leave_span = &leave_span;
    parser.text = &on_text;

    if (md_text.size() > static_cast<std::size_t>(MD_SIZE(-1))) {
        return false;   // document larger than md4c can address
    }
    const int rc = md_parse(md_text.data(),
                            static_cast<MD_SIZE>(md_text.size()), &parser,
                            &renderer);
    if (rc != 0) {
        return false;
    }
    out = std::move(renderer.out);
    return true;
}

}  // namespace mdrender::detail
