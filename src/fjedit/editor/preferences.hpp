#pragma once

// How this machine likes code shown (sheet 39, S1): kept in the settings
// file of whatever program shows it, never in a folder it opens. Every view
// takes them as it is drawn.
//
//     ui::add_settings_section(preferences_section(preferences));
//     ui::keep_settings_file(path);

#include "fjedit/widget/code_view.hpp"

#include "ftk/ui/kit/choice.hpp"
#include "ftk/ui/kit/settings_file.hpp"

#include <string>

namespace fjedit {

/// The code's text size steps between these, in pixels.
inline constexpr int TEXT_SIZE_MIN = 10;
inline constexpr int TEXT_SIZE_MAX = 32;
/// The rows kept around the caret step between none and this; more than
/// half a view's keep the caret in its middle.
inline constexpr int AROUND_CARET_MAX = 100;

/// The ways of numbering lines, as the settings file and a preferences
/// window name them.
inline constexpr ftk::ui::Choice<LineNumbers> LINE_NUMBERS[] = {
    {LineNumbers::Absolute, "Absolute"},
    {LineNumbers::Relative, "Relative"},
};

struct Preferences {
    /// The code's text size, in pixels; its lines and line numbers scale
    /// with it.
    int text_size{static_cast<int>(CODE_TEXT)};
    /// The views show the minimap.
    bool minimap{true};
    /// Typing an opener or a quote types its closer too
    /// (EditorView::close_pairs).
    bool close_pairs{true};
    /// The views draw brackets in their levels' colours
    /// (EditorView::bracket_colours).
    bool bracket_colours{true};
    /// How the views number their lines.
    LineNumbers line_numbers{LineNumbers::Absolute};
    /// How many rows the views keep in sight above and below the caret as
    /// the keys move it.
    int around_caret{0};
    /// The views take Vim's keys rather than the standard ones.
    bool vim{false};
    /// clangd, which answers for C and C++: a path, or a name looked for
    /// along the PATH.
    std::string clangd{"clangd"};
    /// fjls, fjell's language server, which answers for a game's script
    /// headers beside clangd and for fjell's own files: likewise.
    std::string fjls{"fjls"};
};

/// The settings file's section that keeps `preferences`, read back as the
/// file is read and written as it is saved; a value out of range is held to
/// it. `preferences` outlives the section.
[[nodiscard]] ftk::ui::SettingsSection preferences_section(Preferences& preferences);

} // namespace fjedit
