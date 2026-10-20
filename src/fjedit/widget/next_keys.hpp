#pragma once

// The plate of keys that can come next (sheet 47, S2): once keys typed with
// Vim on have waited half a second for the rest of a map or a binding, it
// shows at the code's bottom right what each key that can come next runs,
// teaching one's own maps and fjedit's.

#include "ftk/ui/core/rect.hpp"

#include <span>
#include <string>
#include <string_view>

namespace fjedit {

/// A key that can come next, and what it runs: a map's keys or `:` line, a
/// command's title. A `group` begins more: its title says what they lead to.
struct NextKey {
    std::string key;
    std::string title;
    bool group{false};
};

/// Draws the plate over the bottom right of `over`: `typed`, the keys so
/// far, then `keys` in two columns, each a key label and its title (a
/// group's in the text colour, after a +), at the code's `text_size`.
void next_keys_plate(std::string_view typed, std::span<const NextKey> keys, float text_size, ftk::ui::core::Rect over);

} // namespace fjedit
