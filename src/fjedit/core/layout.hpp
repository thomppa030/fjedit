#pragma once

// How the text is laid out along its lines, as far as moving a caret up and
// down needs it: how far along its line a position is, and which position
// of a line is nearest a place along it. The view answers from the text as
// it draws it; MonospaceLayout answers in fixed cells, for tests and for a
// view that has not drawn yet.

#include "fjedit/core/buffer.hpp"

#include <cstddef>

namespace fjedit {

/// How many cells of fixed width `line` takes up to `byte`: one for every
/// character, and a tab reaching on to the next multiple of `tab_width`.
[[nodiscard]] std::size_t column_of(std::string_view line, std::size_t byte, std::size_t tab_width);

class Layout {
public:
    virtual ~Layout() = default;

    /// How far along its line `at` is, from the line's start.
    [[nodiscard]] virtual float x_of(const Buffer& buffer, Position at) const = 0;

    /// The position on `line` nearest `x` along it: before the character
    /// `x` falls in the first half of, after one it falls in the second
    /// half of, and the line's end past its last.
    [[nodiscard]] virtual Position position_at(const Buffer& buffer, std::size_t line, float x) const = 0;

    /// How many lines a page up or down moves.
    [[nodiscard]] virtual std::size_t page_lines() const = 0;
};

/// Every character one cell wide, a tab reaching to the next stop.
class MonospaceLayout final : public Layout {
public:
    explicit MonospaceLayout(std::size_t tab_width = 4, std::size_t page_lines = 20)
        : tab_width_(tab_width), page_lines_(page_lines) {}

    [[nodiscard]] float x_of(const Buffer& buffer, Position at) const override;
    [[nodiscard]] Position position_at(const Buffer& buffer, std::size_t line, float x) const override;
    [[nodiscard]] std::size_t page_lines() const override { return page_lines_; }

private:
    std::size_t tab_width_;
    std::size_t page_lines_;
};

} // namespace fjedit
