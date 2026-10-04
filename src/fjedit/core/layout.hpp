#pragma once

// How the text is laid out, as far as moving a caret up and down needs it:
// how far along its line a position is, which position of a line is nearest
// a place along it, and which lines are shown (a folded block's are not).
// The view answers from the text as it draws it; MonospaceLayout answers in
// fixed cells, for tests and for a view that has not drawn yet. Both hide the
// lines of the folds a view has closed (fjedit::Folds) when given them.

#include "fjedit/core/buffer.hpp"

#include <cstddef>
#include <optional>

namespace fjedit {

class Folds;

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

    /// The line `count` shown lines below `line`, or above it; nothing when
    /// there are not that many. Every line is shown unless the layout hides
    /// some; a hidden line counts from the line shown in its place.
    [[nodiscard]] virtual std::optional<std::size_t> line_below(const Buffer& buffer, std::size_t line,
                                                                std::size_t count) const {
        if (line + count >= buffer.line_count()) return std::nullopt;
        return line + count;
    }
    [[nodiscard]] virtual std::optional<std::size_t> line_above(const Buffer& buffer, std::size_t line,
                                                                std::size_t count) const {
        (void)buffer;
        if (count > line) return std::nullopt;
        return line - count;
    }
};

/// Every character one cell wide, a tab reaching to the next stop.
class MonospaceLayout final : public Layout {
public:
    explicit MonospaceLayout(std::size_t tab_width = 4, std::size_t page_lines = 20)
        : tab_width_(tab_width), page_lines_(page_lines) {}

    [[nodiscard]] float x_of(const Buffer& buffer, Position at) const override;
    [[nodiscard]] Position position_at(const Buffer& buffer, std::size_t line, float x) const override;
    [[nodiscard]] std::size_t page_lines() const override { return page_lines_; }
    [[nodiscard]] std::optional<std::size_t> line_below(const Buffer& buffer, std::size_t line,
                                                        std::size_t count) const override;
    [[nodiscard]] std::optional<std::size_t> line_above(const Buffer& buffer, std::size_t line,
                                                        std::size_t count) const override;

    /// Hides the lines of the folds `folds` has closed, while it lives; null
    /// shows every line.
    void set_folds(const Folds* folds) { folds_ = folds; }

private:
    std::size_t tab_width_;
    std::size_t page_lines_;
    const Folds* folds_{nullptr};
};

} // namespace fjedit
