#pragma once

// How the code view lays its text out: the UI core's text, in one face and
// size, with a tab reaching on to the next stop. What moving a caret up and
// down asks of the text (fjedit::Layout), and what the view draws by.

#include "fjedit/core/layout.hpp"

#include "ftk/ui/core/text.hpp"

#include <cstddef>
#include <string_view>

namespace fjedit {

class TextLayout final : public Layout {
public:
    /// Text in `style` through `text`, which must outlive the layout, a tab
    /// stopping every `tab_width` spaces' width.
    TextLayout(ftk::ui::core::TextSystem& text, ftk::ui::core::TextStyle style, std::size_t tab_width);

    [[nodiscard]] float x_of(const Buffer& buffer, Position at) const override;
    [[nodiscard]] Position position_at(const Buffer& buffer, std::size_t line, float x) const override;
    [[nodiscard]] std::size_t page_lines() const override { return page_lines_; }

    /// How many lines a page moves: as many as the view shows.
    void set_page_lines(std::size_t lines) { page_lines_ = lines; }

    /// How far along `line` byte `to` is, given byte `from` is at `x`: the
    /// characters between, and each tab on to its next stop.
    [[nodiscard]] float advance(std::string_view line, std::size_t from, std::size_t to, float x) const;

    /// The byte of `line` nearest `x` along it (see Layout::position_at()).
    [[nodiscard]] std::size_t byte_at(std::string_view line, float x) const;

    [[nodiscard]] ftk::ui::core::TextStyle style() const { return style_; }
    [[nodiscard]] float line_height() const;
    [[nodiscard]] float space_width() const { return space_; }

private:
    // Where the tab stop after `x` is.
    [[nodiscard]] float next_stop(float x) const;

    ftk::ui::core::TextSystem* text_;
    ftk::ui::core::TextStyle style_;
    std::size_t tab_width_;
    std::size_t page_lines_{20};
    float space_;
};

} // namespace fjedit
