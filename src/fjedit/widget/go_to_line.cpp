#include "fjedit/widget/go_to_line.hpp"

#include "fjedit/core/structure.hpp"
#include "fjedit/widget/code_view.hpp"

#include "ftk/base/utf8.hpp"
#include "ftk/ui/kit/feedback.hpp"
#include "ftk/ui/kit/item.hpp"
#include "ftk/ui/kit/layout.hpp"
#include "ftk/ui/kit/plate.hpp"
#include "ftk/ui/kit/text.hpp"
#include "ftk/ui/kit/text_field.hpp"
#include "ftk/ui/kit/tree.hpp"
#include "ftk/ui/theme.hpp"

#include <algorithm>
#include <charconv>
#include <format>

namespace fjedit {

namespace {

namespace ui = ftk::ui;
namespace theme = ftk::theme;

// The plate's width (sheet 39); for Vim's `:` line, find's (sheet 49).
constexpr float PLATE_WIDTH = 320.0f;
constexpr float COMMAND_PLATE_WIDTH = 420.0f;

// `text` without the spaces around it.
std::string_view trimmed(std::string_view text) {
    const std::size_t first = text.find_first_not_of(' ');
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

// `text` as a whole number, when it is nothing else.
std::optional<std::size_t> number(std::string_view text) {
    text = trimmed(text);
    std::size_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

} // namespace

std::optional<Position> go_to_line_target(const Buffer& buffer, std::string_view text) {
    const std::size_t colon = text.find(':');
    const std::optional<std::size_t> line = number(text.substr(0, colon));
    if (!line || *line == 0 || *line > buffer.line_count()) return std::nullopt;
    std::size_t column = 1;
    if (colon != std::string_view::npos) {
        const std::optional<std::size_t> asked = number(text.substr(colon + 1));
        if (!asked || *asked == 0) return std::nullopt;
        column = *asked;
    }
    const std::string_view content = buffer.line(*line - 1);
    std::size_t byte = 0;
    for (std::size_t at = 1; at < column && byte < content.size(); ++at) (void)ftk::utf8::next(content, byte);
    return Position{*line - 1, byte};
}

GoToLinePlate go_to_line_plate(GoToLineState& state, const Document& document, Position caret,
                               ftk::ui::core::Rect over) {
    GoToLinePlate out;
    const Buffer& buffer = document.buffer();
    const std::optional<Position> target = go_to_line_target(buffer, state.text);
    const float width = std::min(PLATE_WIDTH, over.width() - 2.0f * ui::PLATE_INSET);
    if (auto plate = ui::Plate("##go_to_line", over.min, over.max, ui::PlatePlace::TopCenter, width)) {
        ui::next_width(plate.inner_width());
        // Enter lets the keyboard go, and the plate goes with it.
        const bool entered = ui::text_entry("##line", state.text, {.prompt = ":", .focus = state.focus});
        if (entered) out.go = target;
        out.closed = entered || ui::item_deactivated();

        // Where the line is: the line asked for, or the caret's.
        const std::size_t line = target ? target->line : caret.line;
        if (state.named_line != line || state.named_version != document.version()) {
            state.function = enclosing_function(buffer, document.syntax(), line);
            state.named_line = line;
            state.named_version = document.version();
        }
        const ui::FontScope small(ui::Font::Current, theme::SMALL_TEXT);
        if (!state.text.empty() && !target) {
            ui::text(std::format("Type a line from 1 to {}", buffer.line_count()), theme::text_secondary());
        } else {
            ui::text(std::format("Line {} of {}{}", line + 1, buffer.line_count(), state.function ? ", in " : ""),
                     theme::text_secondary());
            if (state.function) {
                ui::same_line(0.0f);
                const ui::FontScope mono(theme::mono_font(), theme::SMALL_TEXT);
                ui::text(*state.function, theme::code_function());
            }
            ui::same_line(0.0f);
            ui::text(target ? " \xC2\xB7 Enter goes there" : " \xC2\xB7 Type a line to go to", theme::text_secondary());
        }
    }
    state.focus = false;
    return out;
}

std::optional<std::size_t> command_line_plate(char kind, std::string_view text, std::string_view note,
                                              const vim::NameChoices* names, const vim::Outcome* outcome,
                                              float text_size, ftk::ui::core::Rect over) {
    std::optional<std::size_t> clicked;
    // A `:` line is as wide as find's plate from its first key, so the
    // words under it never make it jump.
    const float base = kind == ':' ? COMMAND_PLATE_WIDTH : PLATE_WIDTH;
    const float width = std::min(base * text_size / CODE_TEXT, over.width() - 2.0f * ui::PLATE_INSET);
    if (auto plate = ui::Plate("##command_line", over.min, over.max, ui::PlatePlace::TopCenter, width)) {
        const bool rows = names != nullptr && !names->names.empty();
        {
            const ui::FontScope mono(theme::mono_font(), text_size);
            ui::next_width(plate.inner_width());
            const char prompt[] = {kind, '\0'};
            ui::shown_entry("Command line##vim", text, {.prompt = prompt, .caret = text.size(), .note = note});
            // The names it can become, their words in a column past the
            // longest.
            if (rows) {
                float column = 0.0f;
                for (const vim::ExCommandName& name : names->names) column = std::max(column, ui::text_size(name.name).x);
                column += ui::text_size("  ").x;
                if (auto tree = ui::Tree("##names", text_size)) {
                    for (std::size_t i = 0; i < names->names.size(); ++i) {
                        const vim::ExCommandName& name = names->names[i];
                        const std::string id(name.name);
                        const ui::TreeRowResult row = ui::tree_row({
                            .id = id.c_str(),
                            .name = name.name,
                            .description = name.words,
                            .description_at = column,
                            .selection = names->lit == i ? ui::RowSelection::Primary : ui::RowSelection::None,
                            .query = names->typed,
                        });
                        if (row.clicked) clicked = i;
                    }
                }
            }
        }
        // Under the names, only a refusal to come: the lit name says what
        // runs, and a name half typed isn't wrong yet.
        if (outcome != nullptr && (!rows || outcome->kind == vim::Outcome::Kind::Refused)) {
            if (rows) ui::separator();
            const ui::FontScope body(ui::Font::Body, text_size);
            std::optional<ui::Severity> severity;
            if (outcome->kind == vim::Outcome::Kind::Refused) severity = ui::Severity::Warning;
            if (outcome->kind == vim::Outcome::Kind::Fails) severity = ui::Severity::Error;
            ui::outcome_line(severity, outcome->text);
        }
    }
    return clicked;
}

} // namespace fjedit
