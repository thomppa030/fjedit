#pragma once

// How this machine likes fjedit to show code (sheet 39, S1): kept in the
// settings file beside the window's places, never in a folder it opens, and
// edited in Edit > Preferences, a window shaped like Fjell's project
// settings. There is no Save: a change applies at once.
//
//     ui::add_settings_section(preferences_section(program.preferences()));
//     ui::keep_settings_file(path);

#include "fjedit/widget/code_view.hpp"

#include "ftk/ui/kit/settings_file.hpp"

#include <string>

namespace fjedit {

/// The code's text size steps between these, in pixels.
inline constexpr int TEXT_SIZE_MIN = 10;
inline constexpr int TEXT_SIZE_MAX = 24;

struct Preferences {
    /// The code's text size, in pixels; its lines and line numbers scale
    /// with it.
    int text_size{static_cast<int>(CODE_TEXT)};
    /// The views show the minimap.
    bool minimap{true};
    /// clangd, which answers for C and C++: a path, or a name looked for
    /// along the PATH.
    std::string clangd{"clangd"};
};

/// The settings file's section that keeps `preferences`, read back as the
/// file is read and written as it is saved; a value out of range is held to
/// it. `preferences` outlives the section.
[[nodiscard]] ftk::ui::SettingsSection preferences_section(Preferences& preferences);

class PreferencesWindow {
public:
    void open() { open_ = true; }
    [[nodiscard]] bool is_open() const { return open_; }

    /// Draws it while open: the pages down the left with a search over
    /// their settings, the page on the right, and a foot saying where the
    /// settings are kept. Each edit changes `preferences` and tells the
    /// settings file.
    void draw(Preferences& preferences);

private:
    bool open_{false};
    std::string search_;
    // How many settings the search found, as the last frame drew them.
    int found_{0};
};

} // namespace fjedit
