#include "fjedit/editor/code_status.hpp"

#include "ftk/ui/kit/text.hpp"
#include "ftk/ui/theme.hpp"

#include <format>

namespace fjedit {

namespace ui = ftk::ui;
namespace theme = ftk::theme;

void draw_vim_status(const CodeStatus& status) {
    if (!status.vim) return;
    const CodeStatus::Vim& vim = *status.vim;
    ui::status_pill(nullptr, vim.mode.c_str(), vim.tone);
    if (vim.recording) ui::status_item(nullptr, std::format("recording @{}", *vim.recording));
    if (!vim.keys.empty()) {
        const ui::FontScope mono(theme::mono_font(), theme::SMALL_TEXT);
        ui::status_item(nullptr, vim.keys);
    }
    // What the last key had to say, where Vim says it.
    if (!vim.said.text.empty()) {
        ui::status_message(vim.said.error ? ui::StatusTone::Error : ui::StatusTone::Plain, vim.said.text);
    }
    if (vim.keys_error) {
        const vim::KeysError& first = *vim.keys_error;
        ui::status_note(ui::StatusTone::Warning, first.message,
                        first.line == 0 ? std::string("keys.vim") : std::format("keys.vim:{}", first.line));
    }
    ui::status_rule();
}

void draw_place_status(const CodeStatus& status, bool indent_and_encoding) {
    if (!status.place) return;
    const CodeStatus::Place& place = *status.place;
    ui::status_item(nullptr, std::format("Ln {}, Col {}", place.line, place.column));
    ui::status_rule();
    if (indent_and_encoding) {
        ui::status_item(nullptr, std::format("{}: {}", place.indent.tabs ? "Tabs" : "Spaces", place.indent.width));
        ui::status_rule();
        ui::status_item(nullptr, place.byte_order_mark ? "UTF-8 with BOM" : "UTF-8");
        ui::status_rule();
    }
    ui::status_item(nullptr, place.language);
    // Each language service the file asks, as a dot in how it is.
    for (const ServiceShown& service : place.services) {
        ui::status_rule();
        switch (service.status.state) {
            case ServiceState::Ready:
                ui::status_state(ui::StatusTone::Success, service.name, service.name + " is running");
                break;
            case ServiceState::Starting:
                ui::status_state(ui::StatusTone::Warning, service.name, service.status.note);
                break;
            case ServiceState::Stopped:
                ui::status_state(ui::StatusTone::Error, service.name, service.status.note);
                break;
        }
    }
}

} // namespace fjedit
