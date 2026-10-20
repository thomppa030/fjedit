#pragma once

// The code view's own icons, by what they mean: the one file of the widget
// that spells glyphs (ftk_check_editor_ui's ICONS). The kit's own, such as
// its error and warning, are ftk::ui::icon's.

#include "ftk/ui/icons_lc.hpp"

namespace fjedit::icon {

// What a completion names (sheet 39, P1).
inline constexpr const char* function = ICON_LC_FUNCTION_SQUARE;
inline constexpr const char* variable = ICON_LC_VARIABLE;
inline constexpr const char* type = ICON_LC_BOX;
inline constexpr const char* type_parameter = ICON_LC_SHAPES;
inline constexpr const char* module = ICON_LC_PACKAGE;
inline constexpr const char* enumeration = ICON_LC_LIST;
inline constexpr const char* enum_member = ICON_LC_LIST_ORDERED;
inline constexpr const char* constant = ICON_LC_PI;
inline constexpr const char* keyword = ICON_LC_KEY_ROUND;
inline constexpr const char* text = ICON_LC_TEXT_QUOTE;
inline constexpr const char* reference = ICON_LC_AT_SIGN;
inline constexpr const char* operation = ICON_LC_PARENTHESES;
inline constexpr const char* file = ICON_LC_FILE;
inline constexpr const char* folder = ICON_LC_FOLDER;

// Kinds of file (sheet 39), each tinted in its category's hue (file_look()).
inline constexpr const char* code_file = ICON_LC_FILE_CODE;   // C++ and shaders
inline constexpr const char* sheet_file = ICON_LC_HASH;       // stylesheets
inline constexpr const char* widget_file = ICON_LC_CODE;      // widgets and their markup
inline constexpr const char* data_file = ICON_LC_BRACES;      // JSON, and the assets written in it
inline constexpr const char* plain_file = ICON_LC_FILE;

} // namespace fjedit::icon
