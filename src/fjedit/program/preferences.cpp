#include "fjedit/program/preferences.hpp"

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
#include <charconv>
#include <cstdlib>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fjedit {

namespace {

namespace ui = ftk::ui;
namespace theme = ftk::theme;

// In the settings file as
//     [fjedit][Code]
//     TextSize=13
//     Minimap=1
//     LineNumbers=Relative
//     AroundCaret=20
//     VimKeys=1
//     Clangd=clangd
//     Fjls=fjls
constexpr const char* SECTION = "fjedit";
constexpr std::string_view ENTRY = "Code";

// The folders opened lately, in a section of their own, as
//     [fjedit-folders][Recent]
//     Folder=/home/user/game
constexpr const char* FOLDERS_SECTION = "fjedit-folders";
constexpr std::string_view FOLDERS_ENTRY = "Recent";
constexpr std::string_view FOLDER_KEY = "Folder=";

// The ways of numbering lines, as the settings file and the Preferences
// window name them.
constexpr ui::Choice<LineNumbers> LINE_NUMBERS[] = {
    {LineNumbers::Absolute, "Absolute"},
    {LineNumbers::Relative, "Relative"},
};

std::string_view line_numbers_name(LineNumbers numbers) {
    return std::ranges::find(LINE_NUMBERS, numbers, &ui::Choice<LineNumbers>::value)->name;
}

// The window as sheet 39 first shows it.
constexpr glm::vec2 SIZE{780.0f, 500.0f};

void read_line(Preferences& preferences, std::string_view entry, std::string_view line) {
    if (entry != ENTRY) return;
    const std::size_t equals = line.find('=');
    if (equals == std::string_view::npos) return;
    const std::string_view key = line.substr(0, equals);
    const std::string_view value = line.substr(equals + 1);
    if (key == "Clangd") {
        if (!value.empty()) preferences.clangd = value;
        return;
    }
    if (key == "Fjls") {
        if (!value.empty()) preferences.fjls = value;
        return;
    }
    if (key == "LineNumbers") {
        const auto* found = std::ranges::find(LINE_NUMBERS, value, &ui::Choice<LineNumbers>::name);
        if (found != std::end(LINE_NUMBERS)) preferences.line_numbers = found->value;
        return;
    }
    int number = 0;
    if (std::from_chars(value.data(), value.data() + value.size(), number).ec != std::errc{}) return;
    if (key == "TextSize") {
        preferences.text_size = std::clamp(number, TEXT_SIZE_MIN, TEXT_SIZE_MAX);
    } else if (key == "Minimap") {
        preferences.minimap = number != 0;
    } else if (key == "AroundCaret") {
        preferences.around_caret = std::clamp(number, 0, AROUND_CARET_MAX);
    } else if (key == "VimKeys") {
        preferences.vim = number != 0;
    }
}

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
        server_row("fjls", preferences.fjls, fjls, "Starts with the first script header or Fjell file opened",
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

ui::SettingsSection preferences_section(Preferences& preferences) {
    return {.name = SECTION,
            .read = [&preferences](std::string_view entry, std::string_view line) {
                read_line(preferences, entry, line);
            },
            .write = [&preferences] {
                return std::vector<ui::SettingsEntry>{
                    {std::string(ENTRY),
                     {std::format("TextSize={}", preferences.text_size),
                      std::format("Minimap={}", preferences.minimap ? 1 : 0),
                      std::format("LineNumbers={}", line_numbers_name(preferences.line_numbers)),
                      std::format("AroundCaret={}", preferences.around_caret),
                      std::format("VimKeys={}", preferences.vim ? 1 : 0),
                      std::format("Clangd={}", preferences.clangd),
                      std::format("Fjls={}", preferences.fjls)}}};
            }};
}

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
