#pragma once

// How the code view lays its text out: the UI core's text, in one face and
// size, with a tab reaching on to the next stop, room along a line for what
// isn't text (a colour value's swatch), and the lines of the view's closed
// folds hidden. What moving a caret up and down asks of the text
// (fjedit::Layout), and what the view draws by.

#include "fjedit/core/layout.hpp"

#include "ftk/ui/core/text.hpp"

#include <cstddef>
#include <map>
#include <span>
#include <string_view>
#include <vector>

namespace fjedit {

/// Room along a line that isn't text, before the character at `byte`: where
/// the view draws a colour value's swatch. A position at `byte` is before
/// it, the character after it.
struct Room {
    std::size_t byte{0};
    float width{0.0f};

    bool operator==(const Room&) const = default;
};

/// The rooms along each line that has any, by line, each line's in the
/// order of their bytes.
using Rooms = std::map<std::size_t, std::vector<Room>>;

class TextLayout final : public Layout {
public:
    /// Text in `style` through `text`, which must outlive the layout, a tab
    /// stopping every `tab_width` spaces' width.
    TextLayout(ftk::ui::core::TextSystem& text, ftk::ui::core::TextStyle style, std::size_t tab_width);

    [[nodiscard]] float x_of(const Buffer& buffer, Position at) const override;
    [[nodiscard]] Position position_at(const Buffer& buffer, std::size_t line, float x) const override;
    [[nodiscard]] std::size_t page_lines() const override { return page_lines_; }
    [[nodiscard]] std::optional<std::size_t> line_below(const Buffer& buffer, std::size_t line,
                                                        std::size_t count) const override;
    [[nodiscard]] std::optional<std::size_t> line_above(const Buffer& buffer, std::size_t line,
                                                        std::size_t count) const override;

    /// How many lines a page moves: as many as the view shows.
    void set_page_lines(std::size_t lines) { page_lines_ = lines; }

    /// Hides the lines of the folds `folds` has closed, while it lives; null
    /// shows every line.
    void set_folds(const Folds* folds) { folds_ = folds; }

    /// Lays `rooms` along their lines, while they live; null lays none.
    void set_rooms(const Rooms* rooms) { rooms_ = rooms; }

    /// The rooms along line `line`; none where it has none.
    [[nodiscard]] std::span<const Room> rooms_of(std::size_t line) const;

    /// How far along `line` byte `to` is, given byte `from` is at `x`: the
    /// characters between, each tab on to its next stop, and each of
    /// `rooms` (the line's) before a byte from `from` up to `to`.
    [[nodiscard]] float advance(std::string_view line, std::size_t from, std::size_t to, float x,
                                std::span<const Room> rooms = {}) const;

    /// The byte of `line` nearest `x` along it, laid out with `rooms` (see
    /// Layout::position_at()); on a room, the byte after it.
    [[nodiscard]] std::size_t byte_at(std::string_view line, float x, std::span<const Room> rooms = {}) const;

    [[nodiscard]] ftk::ui::core::TextStyle style() const { return style_; }
    [[nodiscard]] float line_height() const;
    [[nodiscard]] float space_width() const { return space_; }

private:
    // Where the tab stop after `x` is.
    [[nodiscard]] float next_stop(float x) const;
    // advance() and byte_at() between rooms: the text's alone, from byte
    // `from` at `x`.
    [[nodiscard]] float advance_text(std::string_view line, std::size_t from, std::size_t to, float x) const;
    [[nodiscard]] std::size_t byte_in(std::string_view line, std::size_t from, std::size_t to, float from_x,
                                      float x) const;

    ftk::ui::core::TextSystem* text_;
    ftk::ui::core::TextStyle style_;
    std::size_t tab_width_;
    std::size_t page_lines_{20};
    float space_;
    const Folds* folds_{nullptr};
    const Rooms* rooms_{nullptr};
};

} // namespace fjedit
