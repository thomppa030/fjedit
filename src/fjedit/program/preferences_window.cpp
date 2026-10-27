#include "fjedit/program/preferences_window.hpp"

#include "fjedit/program/icons.hpp"

#include "ftk/ui/kit/feedback.hpp"
#include "ftk/ui/kit/icons.hpp"
#include "ftk/ui/kit/layout.hpp"
#include "ftk/ui/kit/pages.hpp"
#include "ftk/ui/kit/row.hpp"
#include "ftk/ui/kit/save_bar.hpp"
#include "ftk/ui/kit/search.hpp"
#include "ftk/ui/theme.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fjedit {

namespace {

namespace ui = ftk::ui;
namespace theme = ftk::theme;

// The folders opened lately, in a section of their own, as
//     [fjedit-folders][Recent]
//     Folder=/home/user/game
constexpr const char* FOLDERS_SECTION = "fjedit-folders";
constexpr std::string_view FOLDERS_ENTRY = "Recent";
constexpr std::string_view FOLDER_KEY = "Folder=";

// The window's layout, in a section of its own, as
//     [fjedit-layout][Window]
//     Files=0
constexpr const char* LAYOUT_SECTION = "fjedit-layout";
constexpr std::string_view LAYOUT_ENTRY = "Window";
constexpr std::string_view FILES_KEY = "Files=";

// The window as sheet 39 first shows it.
constexpr glm::vec2 SIZE{780.0f, 500.0f};

// The Code editor page's settings, as rows a search can filter, in the
// groups both programs draw. Returns whether keys.vim's Open was pressed.
bool draw_code_editor(Preferences& preferences, const LanguageServerShown& clangd, const LanguageServerShown& fjls,
                      const std::filesystem::path& keys) {
    draw_server_rows(preferences, clangd, fjls);
    draw_text_rows(preferences);
    return draw_key_rows(preferences, keys);
}

} // namespace

ui::SettingsSection recent_folders_section(std::vector<std::string>& folders) {
    return {.name = FOLDERS_SECTION,
            .read = [&folders](std::string_view entry, std::string_view line) {
                if (entry != FOLDERS_ENTRY || !line.starts_with(FOLDER_KEY)) return;
                std::string folder(line.substr(FOLDER_KEY.size()));
                if (!folder.empty() && std::ranges::find(folders, folder) == folders.end()) {
                    folders.push_back(std::move(folder));
                }
            },
            .write = [&folders] {
                ui::SettingsEntry recent{.name = std::string(FOLDERS_ENTRY), .lines = {}};
                for (const std::string& folder : folders) recent.lines.push_back(std::string(FOLDER_KEY) + folder);
                return std::vector<ui::SettingsEntry>{std::move(recent)};
            }};
}

ui::SettingsSection layout_section(WindowLayout& layout) {
    return {.name = LAYOUT_SECTION,
            .read = [&layout](std::string_view entry, std::string_view line) {
                if (entry != LAYOUT_ENTRY || !line.starts_with(FILES_KEY)) return;
                layout.files = line.substr(FILES_KEY.size()) != "0";
            },
            .write = [&layout] {
                return std::vector<ui::SettingsEntry>{
                    {.name = std::string(LAYOUT_ENTRY), .lines = {std::string(FILES_KEY) + (layout.files ? "1" : "0")}}};
            }};
}

PreferencesAsk PreferencesWindow::draw(Preferences& preferences, const LanguageServerShown& clangd,
                                      const LanguageServerShown& fjls, const std::filesystem::path& keys) {
    PreferencesAsk ask;
    if (!open_) return ask;
    auto window = ui::PagedWindow("Preferences###preferences", &open_, SIZE);
    if (!window) return ask;

    const float footer = ui::save_bar_height();
    const bool searching = !search_.empty();
    if (auto list = ui::PageList("##pages", footer)) {
        ui::next_width(ui::FILL);
        ui::search_field("##search", search_);
        ui::reserve({0.0f, theme::GAP_XS});
        if (ui::page_entry(icon::code_editor, "Code editor", !searching, searching && found_ == 0)) search_.clear();
    }

    if (auto body = ui::PageBody("##page", footer)) {
        // A title goes by what its page found the frame before: the rows
        // only count as they are drawn.
        if (!searching || found_ > 0) ui::page_title(icon::code_editor, "Code editor");
        const ui::RowFilter filter(search_);
        ask.open_keys = draw_code_editor(preferences, clangd, fjls, keys);
        found_ = filter.matches();
        if (searching && found_ == 0) ui::empty_state(ui::icon::search, "No settings match", "Try another word");
    }

    ui::note_bar(icon::this_machine, "Kept on this machine, never in a project. Changes apply at once.");
    return ask;
}

} // namespace fjedit
