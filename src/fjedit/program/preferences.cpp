#include "fjedit/program/preferences.hpp"

#include "fjedit/program/icons.hpp"

#include "ftk/ui/kit/feedback.hpp"
#include "ftk/ui/kit/field.hpp"
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
#include <format>
#include <string_view>
#include <vector>

namespace fjedit {

namespace {

namespace ui = ftk::ui;
namespace theme = ftk::theme;

// In the settings file as
//     [fjedit][Code]
//     TextSize=13
//     Minimap=1
//     Clangd=clangd
constexpr const char* SECTION = "fjedit";
constexpr std::string_view ENTRY = "Code";

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
    int number = 0;
    if (std::from_chars(value.data(), value.data() + value.size(), number).ec != std::errc{}) return;
    if (key == "TextSize") {
        preferences.text_size = std::clamp(number, TEXT_SIZE_MIN, TEXT_SIZE_MAX);
    } else if (key == "Minimap") {
        preferences.minimap = number != 0;
    }
}

// The Code editor page's settings, as rows a search can filter.
void draw_code_editor(Preferences& preferences) {
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
    }
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
                      std::format("Clangd={}", preferences.clangd)}}};
            }};
}

void PreferencesWindow::draw(Preferences& preferences) {
    if (!open_) return;
    auto window = ui::PagedWindow("Preferences###preferences", &open_, SIZE);
    if (!window) return;

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
        draw_code_editor(preferences);
        found_ = filter.matches();
        if (searching && found_ == 0) ui::empty_state(ui::icon::search, "No settings match", "Try another word");
    }

    ui::note_bar(icon::this_machine, "Kept on this machine, never in a project. Changes apply at once.");
}

} // namespace fjedit
