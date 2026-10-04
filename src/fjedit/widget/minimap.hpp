#pragma once

// The minimap beside a code view (sheet 39, M2): the shapes of its rows, a
// bar for each word in its token's colour and comments fainter, the part
// on screen a lighter band, and ticks at its right edge for what is marked
// (search hits, the caret). A press there or a drag of the band scrolls
// the view.

#include "fjedit/core/document.hpp"
#include "fjedit/core/folds.hpp"

#include "ftk/ui/core/rect.hpp"
#include "ftk/ui/types.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace fjedit {

/// How wide a minimap is.
inline constexpr float MINIMAP_WIDTH = 96.0f;

/// A tick at a minimap's right edge: the row it marks, in its colour.
struct MinimapMark {
    std::size_t row{0};
    ftk::ui::Color colour{};
};

/// What a minimap did in a frame.
struct Minimap {
    /// Where it asks the view to be scrolled to, down from the first row.
    std::optional<float> scroll;
    /// The pointer is over it, for the wheel to scroll the view.
    bool hovered{false};
};

/// Draws the minimap of `document`, its rows as `folds` leave them, into
/// `rect`, for a view `view_height` tall scrolled `scroll` down with rows
/// `row_height` apart. It takes the pointer as `id`.
Minimap minimap(ftk::ui::Id id, const Document& document, const Folds& folds, ftk::ui::core::Rect rect, float scroll,
                float view_height, float row_height, std::span<const MinimapMark> marks);

} // namespace fjedit
