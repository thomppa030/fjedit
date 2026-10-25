#pragma once

// What a language service writes about code, in Markdown, read and drawn
// in a card: sheet 39's hover card and a completion's documentation. The
// Markdown clangd and fjls write is headings, rules, paragraphs and lists,
// `code`, **strong** and *emphasised* text inline, and fenced code. Inline
// code is drawn in the mono face; fenced code in it too, coloured as the
// editor colours code; emphasis in the italic face.
//
//     const auto blocks = read_markdown("### field `speed`\n---\nType: `float`");
//     const float height = draw_markdown(ui, blocks, at, width, look);

#include "ftk/ui/core/context.hpp"

#include <glm/vec2.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit {

/// A stretch of a heading's, a paragraph's or a list item's text.
struct MarkdownRun {
    std::string text{};
    bool code{false};
    bool strong{false};
    bool emphasis{false};

    bool operator==(const MarkdownRun&) const = default;
};

struct MarkdownBlock {
    enum class Kind : uint8_t { Heading, Paragraph, Item, Rule, Code };
    Kind kind{Kind::Paragraph};
    /// A heading's, a paragraph's or an item's text.
    std::vector<MarkdownRun> runs{};
    /// Fenced code's language, as its fence names it ("cpp"), and its lines.
    std::string language{};
    std::vector<std::string> lines{};

    bool operator==(const MarkdownBlock&) const = default;
};

/// The blocks of `markdown`. A paragraph's lines are joined into one; a
/// link is its text; a backslash before a character makes it itself. A
/// single `*` or `_` emphasises as CommonMark has it: opening before a word
/// and closing after one, so `a * b` stays as written, and `_` not inside a
/// word, so `gl_FragCoord` does too.
[[nodiscard]] std::vector<MarkdownBlock> read_markdown(std::string_view markdown);

/// The faces a card's text is drawn in: its prose, code, strong text and
/// emphasis.
struct MarkdownLook {
    ftk::ui::core::TextStyle body{};
    ftk::ui::core::TextStyle mono{};
    ftk::ui::core::TextStyle strong{};
    ftk::ui::core::TextStyle emphasis{};
};

/// The faces of a card's text beside code at `scale` of 13 (sheet 39's
/// hover card and completion): prose, strong and emphasised at 12.5, code
/// at 12, on lines 18 apart.
[[nodiscard]] MarkdownLook card_look(float scale);

/// How wide `blocks` are with nothing wrapped: their widest line.
[[nodiscard]] float markdown_width(ftk::ui::core::Context& ui, std::span<const MarkdownBlock> blocks,
                                   const MarkdownLook& look);

/// Draws `blocks` from `at`, `width` wide: prose wrapped at it a word at a
/// time, code a token at a time. @return how tall they were drawn
float draw_markdown(ftk::ui::core::Context& ui, std::span<const MarkdownBlock> blocks, glm::vec2 at, float width,
                    const MarkdownLook& look);

} // namespace fjedit
