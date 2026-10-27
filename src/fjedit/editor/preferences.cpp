#include "fjedit/editor/preferences.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <iterator>
#include <string_view>
#include <vector>

namespace fjedit {

namespace {

namespace ui = ftk::ui;

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

std::string_view line_numbers_name(LineNumbers numbers) {
    return std::ranges::find(LINE_NUMBERS, numbers, &ui::Choice<LineNumbers>::value)->name;
}

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

} // namespace fjedit
