#pragma once

// The editor library's own icons, by what they mean: the one file of it
// that spells glyphs (ftk_check_editor_ui's ICONS). The kit's own, such as
// its file and search, are ftk::ui::icon's.

#include "ftk/ui/icons_lc.hpp"

namespace fjedit::icon {

inline constexpr const char* split = ICON_LC_COLUMNS_2; // a group's tab row: the file in front beside it

} // namespace fjedit::icon
