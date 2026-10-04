#pragma once

// A code view's go to line (sheet 39): a plate at the top centre of the
// view holding a field for the line, and under it a line saying where that
// is: "Line 18 of 34, in light() · Enter goes there".

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/document.hpp"

#include "ftk/ui/core/rect.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fjedit {

/// A view's go to line: whether it is open, what is typed in it, and the
/// function the line it names is in.
struct GoToLineState {
    bool open{false};
    std::string text;
    /// The keyboard goes to the field on its next draw.
    bool focus{false};

    /// The function `named_line` is in, for the text at `named_version`;
    /// read again when either changes.
    std::optional<std::string> function;
    std::optional<std::size_t> named_line;
    uint64_t named_version{0};
};

/// Where `text` goes: a line, or a line and a column ("18", "18:5"), both
/// counted from 1, the column in characters and held to the line's end.
/// Nothing when it names no line of `buffer`.
[[nodiscard]] std::optional<Position> go_to_line_target(const Buffer& buffer, std::string_view text);

/// What was done on the plate in a frame.
struct GoToLinePlate {
    /// Enter, on a line there is: the caret goes there.
    std::optional<Position> go;
    /// The field was left, by Escape or a press elsewhere.
    bool closed{false};
};

/// Draws go to line's plate over the top centre of `over`, the view, and
/// takes what is typed in it; with nothing typed, it says where `caret` is.
GoToLinePlate go_to_line_plate(GoToLineState& state, const Document& document, Position caret,
                               ftk::ui::core::Rect over);

} // namespace fjedit
