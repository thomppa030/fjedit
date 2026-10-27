#pragma once

// The Code editor page's rows (sheet 39, S1), as fjedit's Preferences and
// fjell's Editor preferences both draw them: the language servers, the text
// and the keys, each group under its subheading, as rows a search can
// filter. Each edit changes the preferences and tells the settings file. A
// program draws the groups in the order its page has them.
//
//     draw_server_rows(preferences, clangd_shown(files), fjls_shown(files));
//     draw_text_rows(preferences);
//     if (draw_key_rows(preferences, keys)) open_keys();

#include "fjedit/core/language_service.hpp"
#include "fjedit/editor/code_files.hpp"
#include "fjedit/editor/preferences.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace fjedit {

/// How a language server is, for Preferences to show: its status, once it
/// has been started, and for clangd the compile commands it reads in the
/// folder open, when it finds some.
struct LanguageServerShown {
    std::optional<ServiceStatus> status{};
    std::string reads{};
    /// A server the program brings rather than one the preferences name:
    /// what its row calls it ("Fjell's own") and the path it runs it from,
    /// shown with nothing to edit. Empty for one whose path is set.
    std::string own{};
    std::string own_path{};
};

/// Where clangd finds a folder's compile commands, as it looks from a file
/// at the folder's top.
inline constexpr std::array<std::string_view, 3> CLANGD_READS{"compile_commands.json", "build/compile_commands.json",
                                                              "compile_flags.txt"};

/// How clangd is for `files`, the first of `places` in their folder that is
/// there named as what it reads.
[[nodiscard]] LanguageServerShown clangd_shown(const CodeFiles& files,
                                               std::span<const std::string_view> places = CLANGD_READS);

/// How fjls is for `files`; the program's own, called `own`, when `files`
/// were given one.
[[nodiscard]] LanguageServerShown fjls_shown(const CodeFiles& files, std::string own = "The program's own");

/// The Language servers group: clangd's and fjls's paths, or a program's own
/// fjls as it is, each with how it is under it.
void draw_server_rows(Preferences& preferences, const LanguageServerShown& clangd, const LanguageServerShown& fjls);

/// The Text group: its size, the minimap, line numbers and the lines kept in
/// sight around the caret.
void draw_text_rows(Preferences& preferences);

/// The Keys group: Vim keys, and keys.vim at `keys` with Open (none shows no
/// row). Returns whether Open was pressed.
[[nodiscard]] bool draw_key_rows(Preferences& preferences, const std::filesystem::path& keys);

/// `path` as the user reads it: their home folder as ~.
[[nodiscard]] std::string shown_path(const std::filesystem::path& path);

} // namespace fjedit
