#include "fjedit/widget/markdown.hpp"

#include "fjedit/core/language.hpp"
#include "fjedit/core/syntax.hpp"
#include "fjedit/widget/code_colours.hpp"

#include "ftk/ui/kit/text.hpp"
#include "ftk/ui/theme.hpp"

#include <algorithm>
#include <cctype>
#include <optional>

namespace fjedit {

namespace {

namespace core = ftk::ui::core;
namespace theme = ftk::theme;

// A card's prose, code and line spacing beside code at 13.
constexpr float CARD_TEXT = 12.5f;
constexpr float CARD_CODE = 12.0f;
constexpr float CARD_LINE = 18.0f;

core::TextStyle style_in(ftk::ui::Font font, float size, float line) {
    const ftk::ui::FontScope scope(font, size);
    core::TextStyle style = core::current().text().style();
    style.line_height = line;
    return style;
}

// Between blocks; above and below a rule; a list item's text in from its
// bullet.
constexpr float BLOCK_GAP = 6.0f;
constexpr float RULE_GAP = 8.0f;
constexpr float ITEM_INDENT = 12.0f;
constexpr std::string_view BULLET = "\xE2\x80\xA2"; // •
// How far down the clip reaches: what is drawn is cut at its width alone.
constexpr float TALLEST = 100000.0f;

std::string_view trimmed(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

bool punctuation(char c) {
    return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

// A line of three or more dashes, stars or underscores, spaces between.
bool rule(std::string_view line) {
    if (line.size() < 3 || (line[0] != '-' && line[0] != '*' && line[0] != '_')) return false;
    std::size_t marks = 0;
    for (const char c : line) {
        if (c == line[0]) {
            ++marks;
        } else if (c != ' ') {
            return false;
        }
    }
    return marks >= 3;
}

// What a list item's line holds after its bullet or number; nothing when
// it isn't one.
std::optional<std::string_view> item(std::string_view line) {
    if (line.size() >= 2 && (line[0] == '-' || line[0] == '*' || line[0] == '+') && line[1] == ' ') {
        return trimmed(line.substr(2));
    }
    std::size_t digits = 0;
    while (digits < line.size() && line[digits] >= '0' && line[digits] <= '9') ++digits;
    if (digits > 0 && digits + 1 < line.size() && (line[digits] == '.' || line[digits] == ')') && line[digits + 1] == ' ') {
        return trimmed(line.substr(digits + 2));
    }
    return std::nullopt;
}

bool blank(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool word_byte(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || static_cast<unsigned char>(c) >= 0x80;
}

// Whether the single `mark` at `at` in `text` opens emphasis: a word follows
// it, an `_` isn't inside a word, and a mark that can close it comes later.
bool opens_emphasis(std::string_view text, std::size_t at, char mark) {
    if (at + 1 >= text.size() || blank(text[at + 1])) return false;
    if (mark == '_' && at > 0 && word_byte(text[at - 1])) return false;
    for (std::size_t close = at + 2; close < text.size(); ++close) {
        if (text[close] != mark || blank(text[close - 1])) continue;
        // One of a pair is strong text's, no emphasis's.
        if ((close + 1 < text.size() && text[close + 1] == mark) || text[close - 1] == mark) continue;
        if (mark == '_' && close + 1 < text.size() && word_byte(text[close + 1])) continue;
        return true;
    }
    return false;
}

// Whether the single `mark` at `at` in `text` closes emphasis: it follows a
// word, and an `_` ends one.
bool closes_emphasis(std::string_view text, std::size_t at, char mark) {
    if (at == 0 || blank(text[at - 1])) return false;
    return mark != '_' || at + 1 >= text.size() || !word_byte(text[at + 1]);
}

// The runs of inline text: `code`, **strong**, *emphasis*, [a link](to) as
// its text, and a backslash making the character after it itself.
std::vector<MarkdownRun> runs_of(std::string_view text) {
    std::vector<MarkdownRun> runs;
    MarkdownRun current;
    const auto flush = [&] {
        if (!current.text.empty()) runs.push_back(current);
        current.text.clear();
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\\' && i + 1 < text.size() && punctuation(text[i + 1])) {
            current.text += text[++i];
            continue;
        }
        if (c == '`') {
            std::size_t ticks = 1;
            while (i + ticks < text.size() && text[i + ticks] == '`') ++ticks;
            const std::size_t close = text.find(std::string(ticks, '`'), i + ticks);
            if (close != std::string_view::npos) {
                flush();
                std::string_view code = text.substr(i + ticks, close - i - ticks);
                // One space either side, there to let the code begin or end
                // with a backtick, goes.
                if (code.size() >= 2 && code.front() == ' ' && code.back() == ' ') code = code.substr(1, code.size() - 2);
                runs.push_back(
                    {.text = std::string(code), .code = true, .strong = current.strong, .emphasis = current.emphasis});
                i = close + ticks - 1;
                continue;
            }
        }
        if ((c == '*' || c == '_') && i + 1 < text.size() && text[i + 1] == c) {
            flush();
            current.strong = !current.strong;
            ++i;
            continue;
        }
        if ((c == '*' || c == '_') &&
            (current.emphasis ? closes_emphasis(text, i, c) : opens_emphasis(text, i, c))) {
            flush();
            current.emphasis = !current.emphasis;
            continue;
        }
        if (c == '[') {
            const std::size_t middle = text.find("](", i);
            const std::size_t close = middle == std::string_view::npos ? middle : text.find(')', middle);
            if (close != std::string_view::npos) {
                current.text += text.substr(i + 1, middle - i - 1);
                i = close;
                continue;
            }
        }
        current.text += c;
    }
    flush();
    return runs;
}

const core::TextStyle& style_of(const MarkdownRun& run, const MarkdownLook& look) {
    return run.code ? look.mono : run.strong ? look.strong : run.emphasis ? look.emphasis : look.body;
}

float width_of(core::Context& ui, std::string_view text, const core::TextStyle& style) {
    return ui.text().caret_x(text, style, text.size());
}

// Draws `runs` from `at`, wrapping at `right` back to `at.x`, prose in
// `prose` and code and strong text in the text colour. Returns where the
// line after the last one begins.
float draw_runs(core::Context& ui, std::span<const MarkdownRun> runs, glm::vec2 at, float right,
                const MarkdownLook& look, ftk::ui::Color prose) {
    core::DrawList& dl = ui.draw_list();
    const float line = std::max(
        {look.body.line_height, look.mono.line_height, look.strong.line_height, look.emphasis.line_height});
    float x = at.x;
    float y = at.y;
    for (const MarkdownRun& run : runs) {
        const core::TextStyle& style = style_of(run, look);
        const ftk::ui::Color colour = run.code || run.strong ? theme::text() : prose;
        // A word at a time, each with the spaces after it.
        std::string_view rest = run.text;
        while (!rest.empty()) {
            const std::size_t space = rest.find(' ');
            std::size_t end = space == std::string_view::npos ? rest.size() : space;
            while (end < rest.size() && rest[end] == ' ') ++end;
            const std::string_view piece = rest.substr(0, end);
            rest.remove_prefix(end);
            const float word = width_of(ui, trimmed(piece), style);
            if (x > at.x && x + word > right) {
                x = at.x;
                y += line;
            }
            ui.text().draw(dl, {x, y}, colour, piece, style);
            x += width_of(ui, piece, style);
        }
    }
    return y + line;
}

} // namespace

std::vector<MarkdownBlock> read_markdown(std::string_view markdown) {
    std::vector<MarkdownBlock> blocks;
    std::string paragraph;
    const auto end_paragraph = [&] {
        if (!trimmed(paragraph).empty()) {
            blocks.push_back({.kind = MarkdownBlock::Kind::Paragraph, .runs = runs_of(paragraph)});
        }
        paragraph.clear();
    };
    std::optional<MarkdownBlock> code;
    std::string fence;
    while (!markdown.empty()) {
        const std::size_t end = markdown.find('\n');
        std::string_view line = markdown.substr(0, end);
        markdown = end == std::string_view::npos ? std::string_view{} : markdown.substr(end + 1);
        if (line.ends_with('\r')) line.remove_suffix(1);
        const std::string_view text = trimmed(line);

        if (code) {
            if (text.starts_with(fence)) {
                blocks.push_back(std::move(*code));
                code.reset();
            } else {
                code->lines.emplace_back(line);
            }
            continue;
        }
        if (text.starts_with("```") || text.starts_with("~~~")) {
            end_paragraph();
            fence = std::string(text.substr(0, 3));
            code = MarkdownBlock{.kind = MarkdownBlock::Kind::Code, .language = std::string(trimmed(text.substr(3)))};
            continue;
        }
        if (text.empty()) {
            end_paragraph();
            continue;
        }
        if (rule(text)) {
            end_paragraph();
            blocks.push_back({.kind = MarkdownBlock::Kind::Rule});
            continue;
        }
        if (text.starts_with('#')) {
            const std::size_t level = text.find_first_not_of('#');
            if (level != std::string_view::npos && text[level] == ' ') {
                end_paragraph();
                std::string_view heading = trimmed(text.substr(level));
                while (heading.ends_with('#')) heading.remove_suffix(1);
                blocks.push_back({.kind = MarkdownBlock::Kind::Heading, .runs = runs_of(trimmed(heading))});
                continue;
            }
        }
        if (const std::optional<std::string_view> listed = item(text)) {
            end_paragraph();
            blocks.push_back({.kind = MarkdownBlock::Kind::Item, .runs = runs_of(*listed)});
            continue;
        }
        // A line ending in two spaces or a backslash ends its paragraph
        // there.
        const bool broken = line.ends_with("  ") || text.ends_with('\\');
        if (!paragraph.empty()) paragraph += ' ';
        paragraph += broken && text.ends_with('\\') ? text.substr(0, text.size() - 1) : text;
        if (broken) end_paragraph();
    }
    if (code) blocks.push_back(std::move(*code));
    end_paragraph();
    return blocks;
}

MarkdownLook card_look(float scale) {
    const float line = CARD_LINE * scale;
    return {.body = style_in(ftk::ui::Font::Body, CARD_TEXT * scale, line),
            .mono = style_in(theme::mono_font(), CARD_CODE * scale, line),
            .strong = style_in(theme::bold_font(), CARD_TEXT * scale, line),
            .emphasis = style_in(theme::italic_font(), CARD_TEXT * scale, line)};
}

float markdown_width(core::Context& ui, std::span<const MarkdownBlock> blocks, const MarkdownLook& look) {
    float widest = 0.0f;
    for (const MarkdownBlock& block : blocks) {
        if (block.kind == MarkdownBlock::Kind::Code) {
            for (const std::string& line : block.lines) widest = std::max(widest, width_of(ui, line, look.mono));
            continue;
        }
        float width = block.kind == MarkdownBlock::Kind::Item ? ITEM_INDENT : 0.0f;
        for (const MarkdownRun& run : block.runs) width += width_of(ui, run.text, style_of(run, look));
        widest = std::max(widest, width);
    }
    return widest;
}

float draw_markdown(core::Context& ui, std::span<const MarkdownBlock> blocks, glm::vec2 at, float width,
                    const MarkdownLook& look) {
    core::DrawList& dl = ui.draw_list();
    dl.push_clip(at, at + glm::vec2{width, TALLEST});
    const float right = at.x + width;
    float y = at.y;
    // The room before the next block: none at the top or after a rule.
    float gap = 0.0f;
    for (const MarkdownBlock& block : blocks) {
        if (block.kind == MarkdownBlock::Kind::Rule) {
            y += y > at.y ? RULE_GAP : 0.0f;
            dl.rect_filled({at.x, y}, {right, y + 1.0f}, theme::surface_raised());
            y += 1.0f + RULE_GAP;
            gap = 0.0f;
            continue;
        }
        y += gap;
        gap = BLOCK_GAP;
        switch (block.kind) {
            case MarkdownBlock::Kind::Heading:
            case MarkdownBlock::Kind::Paragraph:
                y = draw_runs(ui, block.runs, {at.x, y}, right, look, theme::text_secondary());
                break;
            case MarkdownBlock::Kind::Item:
                ui.text().draw(dl, {at.x, y}, theme::text_secondary(), BULLET, look.body);
                y = draw_runs(ui, block.runs, {at.x + ITEM_INDENT, y}, right, look, theme::text_secondary());
                break;
            case MarkdownBlock::Kind::Code: {
                // Coloured as the editor colours code in its language.
                const Language& language = language_for("code." + block.language);
                LineState state;
                std::vector<Token> tokens;
                for (const std::string& line : block.lines) {
                    tokens.clear();
                    state = language.lexer.lex(line, state, tokens);
                    float x = at.x;
                    std::size_t done = 0;
                    // A token that doesn't fit what is left of the line goes
                    // on to the next, as long declarations wrap.
                    const auto run = [&](std::size_t from, std::size_t to, ftk::ui::Color colour) {
                        if (to <= from) return;
                        const std::string_view piece = std::string_view(line).substr(from, to - from);
                        const float width_here = width_of(ui, piece, look.mono);
                        if (x > at.x && x + width_here > right) {
                            x = at.x;
                            y += look.mono.line_height;
                        }
                        ui.text().draw(dl, {x, y}, colour, piece, look.mono);
                        x += width_here;
                    };
                    for (const Token& token : tokens) {
                        run(done, token.begin, theme::text());
                        run(token.begin, token.end, code_colour(token.kind));
                        done = token.end;
                    }
                    run(done, line.size(), theme::text());
                    y += look.mono.line_height;
                }
                break;
            }
            case MarkdownBlock::Kind::Rule:
                break;
        }
    }
    dl.pop_clip();
    return y - at.y;
}

} // namespace fjedit
