#include "fjedit/program/preferences_window.hpp"

#include "fjedit/program/icons.hpp"

#include "ftk/ui/kit/button.hpp"
#include "ftk/ui/kit/choice.hpp"
#include "ftk/ui/kit/feedback.hpp"
#include "ftk/ui/kit/field.hpp"
#include "ftk/ui/kit/text_field.hpp"
#include "ftk/ui/kit/text.hpp"
#include "ftk/ui/kit/icons.hpp"
#include "ftk/ui/kit/layout.hpp"
#include "ftk/ui/kit/pages.hpp"
#include "ftk/ui/kit/row.hpp"
#include "ftk/ui/kit/save_bar.hpp"
#include "ftk/ui/kit/search.hpp"
#include "ftk/ui/kit/section.hpp"
#include "ftk/ui/theme.hpp"

#include <algorithm>
#include <cstdlib>
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

// A server's path, and how it is under it: `ready` when it runs, `waiting`
// before it has started.
void server_row(const char* name, std::string& path, const LanguageServerShown& shown, const char* waiting,
                const std::string& ready, ui::Severity ready_severity) {
    ui::row(name, [&] {
        const ui::FontScope mono(theme::mono_font(), theme::SMALL_TEXT);
        if (ui::text_field((std::string("##") + name).c_str(), path).committed) ui::settings_changed();
    });
    if (!shown.status) {
        ui::hint(waiting);
    } else if (shown.status->state == ServiceState::Ready) {
        ui::status(ready_severity, ready.c_str());
    } else if (shown.status->state == ServiceState::Starting) {
        ui::status(ui::Severity::Warning, "Starting");
    } else {
        ui::status(ui::Severity::Error, shown.status->note.c_str());
    }
}

// `path` as the user reads it: their home folder as ~.
std::string shown_path(const std::filesystem::path& path) {
    const std::string whole = path.generic_string();
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') return whole;
    const std::string start = std::filesystem::path(home).generic_string();
    if (!whole.starts_with(start + "/")) return whole;
    return "~" + whole.substr(start.size());
}

// The Code editor page's settings, as rows a search can filter. Returns
// whether keys.vim's Open was pressed.
bool draw_code_editor(Preferences& preferences, const LanguageServerShown& clangd, const LanguageServerShown& fjls,
                      const std::filesystem::path& keys) {
    bool open_keys = false;
    ui::subheading("Language servers");
    if (auto table = ui::PropertyTable("##server")) {
        server_row("clangd", preferences.clangd, clangd, "Starts with the first C or C++ file opened",
                   clangd.reads.empty() ? std::string("Running, with no compile commands found")
                                        : "Running, reads " + clangd.reads,
                   clangd.reads.empty() ? ui::Severity::Warning : ui::Severity::Success);
        server_row("fjls", preferences.fjls, fjls, "Starts with the first script header, fjell file or GLSL file opened",
                   "Running", ui::Severity::Success);
    }
    ui::subheading("Text");
    if (auto table = ui::PropertyTable("##text")) {
        ui::row("Text size", [&] {
            if (ui::stepper("##text_size", preferences.text_size, TEXT_SIZE_MIN, TEXT_SIZE_MAX, ui::Unit::Pixels).changed) {
                ui::settings_changed();
            }
        });
        ui::row("Minimap", [&] {
            if (ui::checkbox("##minimap", preferences.minimap).changed) ui::settings_changed();
        });
        ui::row("Line numbers", [&] {
            if (ui::segmented<LineNumbers>("##line_numbers", preferences.line_numbers, LINE_NUMBERS).changed) {
                ui::settings_changed();
            }
        });
        ui::row("Around the caret", [&] {
            if (ui::stepper("##around_caret", preferences.around_caret, 0, AROUND_CARET_MAX, ui::Unit::Lines).changed) {
                ui::settings_changed();
            }
        });
        ui::hint("Lines kept in sight above and below the caret");
    }
    ui::subheading("Keys");
    if (auto table = ui::PropertyTable("##keys")) {
        ui::row("Vim keys", [&] {
            if (ui::checkbox("##vim", preferences.vim).changed) ui::settings_changed();
        });
        if (!keys.empty()) {
            ui::row("Your maps", [&] {
                // The path is cut to leave Open in reach, however long it is.
                const float room = ui::space_left().x - ui::item_spacing().x - ui::button_width("Open");
                {
                    const ui::FontScope mono(theme::mono_font(), theme::SMALL_TEXT);
                    ui::align_text_to_frame();
                    ui::cut_text(shown_path(keys), room, theme::text_secondary());
                }
                ui::same_line();
                open_keys = ui::button("Open");
            });
            ui::hint("keys.vim, in Vim's map syntax. Saved here, it applies at once.");
        }
    }
    return open_keys;
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
