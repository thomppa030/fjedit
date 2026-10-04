#pragma once

// fjedit's own icons, by what they mean: the one file of the program that
// spells glyphs (ftk_check_editor_ui's ICONS). The kit's own, such as its
// folder and search, are ftk::ui::icon's.

#include "ftk/ui/icons_lc.hpp"

namespace fjedit::icon {

// Kinds of file (sheet 39), each tinted in its category's hue.
inline constexpr const char* code_file = ICON_LC_FILE_CODE;   // C++ and shaders
inline constexpr const char* sheet_file = ICON_LC_HASH;       // stylesheets
inline constexpr const char* widget_file = ICON_LC_CODE;      // widgets and their markup
inline constexpr const char* data_file = ICON_LC_BRACES;      // JSON, and the assets written in it
inline constexpr const char* plain_file = ICON_LC_FILE;
inline constexpr const char* open_folder = ICON_LC_FOLDER_OPEN;

// The editor
inline constexpr const char* split = ICON_LC_COLUMNS_2;
inline constexpr const char* more = ICON_LC_MORE_HORIZONTAL;

} // namespace fjedit::icon
