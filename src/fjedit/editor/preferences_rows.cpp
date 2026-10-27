#include "fjedit/editor/preferences_rows.hpp"

#include "ftk/ui/kit/button.hpp"
#include "ftk/ui/kit/choice.hpp"
#include "ftk/ui/kit/feedback.hpp"
#include "ftk/ui/kit/field.hpp"
#include "ftk/ui/kit/layout.hpp"
#include "ftk/ui/kit/row.hpp"
#include "ftk/ui/kit/section.hpp"
#include "ftk/ui/kit/settings_file.hpp"
#include "ftk/ui/kit/text.hpp"
#include "ftk/ui/kit/text_field.hpp"
#include "ftk/ui/theme.hpp"

#include <cstdlib>
#include <system_error>
#include <utility>

namespace fjedit {

namespace {

namespace ui = ftk::ui;
namespace theme = ftk::theme;

// A server's path, or the program's own as it is, and how it is under it:
// `ready` when it runs, `waiting` before it has started.
void server_row(const char* name, std::string& path, const LanguageServerShown& shown, const char* waiting,
                const std::string& ready, ui::Severity ready_severity) {
    ui::row(name, [&] {
        if (!shown.own.empty()) {
            ui::align_text_to_frame();
            ui::text(shown.own);
            ui::same_line();
            const ui::FontScope mono(theme::mono_font(), theme::SMALL_TEXT);
            ui::align_text_to_frame();
            ui::cut_text(shown_path(shown.own_path), ui::space_left().x, theme::text_secondary());
            return;
        }
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

} // namespace

LanguageServerShown clangd_shown(const CodeFiles& files, std::span<const std::string_view> places) {
    LanguageServerShown shown{.status = files.services().status_of("clangd")};
    for (const std::string_view place : places) {
        std::error_code error;
        if (std::filesystem::exists(files.root() / place, error)) {
            shown.reads = place;
            break;
        }
    }
    return shown;
}

LanguageServerShown fjls_shown(const CodeFiles& files, std::string own) {
    LanguageServerShown shown{.status = files.services().status_of("fjls")};
    if (!files.own_fjls().empty()) {
        shown.own = std::move(own);
        shown.own_path = files.own_fjls();
    }
    return shown;
}

void draw_server_rows(Preferences& preferences, const LanguageServerShown& clangd, const LanguageServerShown& fjls) {
    ui::subheading("Language servers");
    if (auto table = ui::PropertyTable("##server")) {
        server_row("clangd", preferences.clangd, clangd, "Starts with the first C or C++ file opened",
                   clangd.reads.empty() ? std::string("Running, with no compile commands found")
                                        : "Running, reads " + clangd.reads,
                   clangd.reads.empty() ? ui::Severity::Warning : ui::Severity::Success);
        server_row("fjls", preferences.fjls, fjls, "Starts with the first script header, fjell file or GLSL file opened",
                   "Running", ui::Severity::Success);
    }
}

void draw_text_rows(Preferences& preferences) {
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
}

bool draw_key_rows(Preferences& preferences, const std::filesystem::path& keys) {
    bool open_keys = false;
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

std::string shown_path(const std::filesystem::path& path) {
    const std::string whole = path.generic_string();
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') return whole;
    const std::string start = std::filesystem::path(home).generic_string();
    if (!whole.starts_with(start + "/")) return whole;
    return "~" + whole.substr(start.size());
}

} // namespace fjedit
