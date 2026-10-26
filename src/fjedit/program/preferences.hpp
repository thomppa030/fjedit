#pragma once

// How this machine likes fjedit to show code (sheet 39, S1): kept in the
// settings file beside the window's places, never in a folder it opens, and
// edited in Edit > Preferences, a window shaped like fjell's project
// settings. There is no Save: a change applies at once.
//
//     ui::add_settings_section(preferences_section(preferences));
//     ui::keep_settings_file(path);

#include "fjedit/core/language_service.hpp"
#include "fjedit/widget/code_view.hpp"

#include "ftk/ui/kit/settings_file.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fjedit {

/// The code's text size steps between these, in pixels.
inline constexpr int TEXT_SIZE_MIN = 10;
inline constexpr int TEXT_SIZE_MAX = 32;
/// The rows kept around the caret step between none and this; more than
/// half a view's keep the caret in its middle.
inline constexpr int AROUND_CARET_MAX = 100;

struct Preferences {
    /// The code's text size, in pixels; its lines and line numbers scale
    /// with it.
    int text_size{static_cast<int>(CODE_TEXT)};
    /// The views show the minimap.
    bool minimap{true};
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

/// The settings file's section that keeps `folders`: the folders opened
/// lately, the latest first, as the folder browser offers them. A folder
/// read twice is kept once. `folders` outlives the section.
[[nodiscard]] ftk::ui::SettingsSection recent_folders_section(std::vector<std::string>& folders);

/// How a language server is, for the Preferences window to show: its
/// status, once it has been started, and for clangd the compile commands it
/// reads in the folder open, when it finds some.
struct LanguageServerShown {
    std::optional<ServiceStatus> status{};
    std::string reads{};
};

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
