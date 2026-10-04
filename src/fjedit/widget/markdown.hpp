#pragma once

// What a language service writes about code, in Markdown, read and drawn
// in a card: sheet 39's hover card and a completion's documentation. The
// Markdown clangd writes is headings, rules, paragraphs and lists, `code`
// inline, and fenced code. Inline code is drawn in the mono face; fenced
// code in it too, coloured as the editor colours code.
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
/// link is its text; a backslash before a character makes it itself.
[[nodiscard]] std::vector<MarkdownBlock> read_markdown(std::string_view markdown);

/// The faces a card's text is drawn in: its prose, code, and strong text.
struct MarkdownLook {
    ftk::ui::core::TextStyle body{};
    ftk::ui::core::TextStyle mono{};
    ftk::ui::core::TextStyle strong{};
};

/// How wide `blocks` are with nothing wrapped: their widest line.
[[nodiscard]] float markdown_width(ftk::ui::core::Context& ui, std::span<const MarkdownBlock> blocks,
                                   const MarkdownLook& look);

/// Draws `blocks` from `at`, `width` wide, prose wrapped at it and code cut
/// off there. @return how tall they were drawn
float draw_markdown(ftk::ui::core::Context& ui, std::span<const MarkdownBlock> blocks, glm::vec2 at, float width,
                    const MarkdownLook& look);

} // namespace fjedit
