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

// Kinds of file, as fjell shows them (its src/ui/icons.hpp), each in its
// category's hue (file_kind.cpp). Textures, audio and fonts are the kit's.
inline constexpr const char* script = ICON_LC_FILE_CODE;           // C++
inline constexpr const char* shader = ICON_LC_CODE;                // FJSL and GLSL
inline constexpr const char* mesh = ICON_LC_BOX;                   // meshes, models and instance sets
inline constexpr const char* skeleton = ICON_LC_BONE;
inline constexpr const char* material = ICON_LC_CIRCLE_DOT;
inline constexpr const char* colour_grade = ICON_LC_PALETTE;
inline constexpr const char* animation = ICON_LC_FILM;
inline constexpr const char* blend_space = ICON_LC_WORKFLOW;
inline constexpr const char* vfx = ICON_LC_SPARKLES;
inline constexpr const char* ui_layout = ICON_LC_LAYOUT_PANEL_TOP; // layouts, style sheets and widgets
inline constexpr const char* surface = ICON_LC_LAYERS_3;           // surfaces and terrain
inline constexpr const char* day_profile = ICON_LC_SUNRISE;
inline constexpr const char* weather = ICON_LC_CLOUD_RAIN;
inline constexpr const char* climate = ICON_LC_THERMOMETER_SUN;
inline constexpr const char* water = ICON_LC_WAVES_HORIZONTAL;
inline constexpr const char* placement = ICON_LC_TREES;
inline constexpr const char* scene = ICON_LC_CLAPPERBOARD;
inline constexpr const char* preset = ICON_LC_BOXES;
inline constexpr const char* input = ICON_LC_GAMEPAD_2;
inline constexpr const char* saved_game = ICON_LC_SAVE;

} // namespace fjedit::icon
