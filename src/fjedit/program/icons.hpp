#pragma once

// fjedit's own icons, by what they mean: the one file of the program that
// spells glyphs (ftk_check_editor_ui's ICONS). The kit's own, such as its
// folder and search, are ftk::ui::icon's.

#include "ftk/ui/icons_lc.hpp"

namespace fjedit::icon {

// The editor
inline constexpr const char* split = ICON_LC_COLUMNS_2;
inline constexpr const char* desktop_entry = ICON_LC_APP_WINDOW; // File > Create desktop entry

// Preferences
inline constexpr const char* code_editor = ICON_LC_CODE;        // the Code editor page
inline constexpr const char* this_machine = ICON_LC_HARD_DRIVE; // kept here, never in a project

} // namespace fjedit::icon
