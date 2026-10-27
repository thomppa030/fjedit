#pragma once

// Edit > Preferences (sheet 39, S1): how this machine likes fjedit to show
// code, edited in a window shaped like fjell's project settings. There is no
// Save: a change applies at once, and the settings file keeps it. Beside it,
// the settings file's section for the folders opened lately.

#include "fjedit/editor/preferences.hpp"
#include "fjedit/editor/preferences_rows.hpp"

#include "ftk/ui/kit/settings_file.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fjedit {

/// The settings file's section that keeps `folders`: the folders opened
/// lately, the latest first, as the folder browser offers them. A folder
/// read twice is kept once. `folders` outlives the section.
[[nodiscard]] ftk::ui::SettingsSection recent_folders_section(std::vector<std::string>& folders);

/// How fjedit's window is laid out on this machine, whatever folder is open:
/// whether its side pane shows the files.
struct WindowLayout {
    bool files{true};
};

/// The settings file's section that keeps `layout`. `layout` outlives the
/// section.
[[nodiscard]] ftk::ui::SettingsSection layout_section(WindowLayout& layout);

/// What the Preferences window asked of the program as it was drawn.
struct PreferencesAsk {
    /// Open keys.vim in a tab, made first when there is none.
    bool open_keys{false};
};

class PreferencesWindow {
public:
    void open() { open_ = true; }
    [[nodiscard]] bool is_open() const { return open_; }

    /// Draws it while open: the pages down the left with a search over
    /// their settings, the page on the right, and a foot saying where the
    /// settings are kept. Each edit changes `preferences` and tells the
    /// settings file. `clangd` and `fjls` are how the language servers are;
    /// `keys` is keys.vim, one's own maps, which Keys shows with Open (none
    /// shows no row).
    [[nodiscard]] PreferencesAsk draw(Preferences& preferences, const LanguageServerShown& clangd,
                                      const LanguageServerShown& fjls, const std::filesystem::path& keys = {});

private:
    bool open_{false};
    std::string search_;
    // How many settings the search found, as the last frame drew them.
    int found_{0};
};

} // namespace fjedit
