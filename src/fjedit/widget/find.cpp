#include "fjedit/widget/find.hpp"

#include "ftk/ui/draw_list.hpp"
#include "ftk/ui/kit/button.hpp"
#include "ftk/ui/kit/icons.hpp"
#include "ftk/ui/kit/layout.hpp"
#include "ftk/ui/kit/plate.hpp"
#include "ftk/ui/theme.hpp"

#include <algorithm>
#include <format>
#include <iterator>
#include <string_view>

namespace fjedit {

namespace {

namespace ui = ftk::ui;
namespace theme = ftk::theme;

// The plate's width (sheet 39), and the least room the count takes.
constexpr float PLATE_WIDTH = 420.0f;
constexpr float COUNT_WIDTH = 52.0f;

// The first match that begins at or after `at`.
std::vector<Span>::const_iterator first_from(const std::vector<Span>& matches, Position at) {
    return std::ranges::lower_bound(matches, at, {}, &Span::from);
}

// `text` as a regular expression that matches it as it is.
std::string escaped(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (std::string_view("\\^$.|?*+()[]{}").find(c) != std::string_view::npos) out += '\\';
        out += c;
    }
    return out;
}

} // namespace

std::string match_count(const FindState& find, const Selections& selections) {
    if (find.query.empty()) return {};
    if (find.matches.empty()) return "No results";
    if (const std::optional<std::size_t> current = current_match(find, selections)) {
        return std::format("{} of {}", *current + 1, find.matches.size());
    }
    return std::format("{} found", find.matches.size());
}

FindOptions find_options(const ui::FindToggles& toggles) {
    return {.match_case = toggles.match_case, .whole_word = toggles.whole_word, .regex = toggles.regex};
}

void open_find(FindState& find, const Buffer& buffer, const Selections& selections, FindState::Focus focus) {
    const Selection& primary = selections.primary();
    const Span span = primary.span();
    std::string seed;
    if (!primary.empty() && span.from.line == span.to.line) {
        seed = buffer.text(span);
    } else if (primary.empty()) {
        seed = buffer.text(word_at(buffer, primary.caret));
    }
    if (!seed.empty()) find.query = find.toggles.regex ? escaped(seed) : seed;
    find.open = true;
    find.focus = focus;
    find.stale = true;
}

void refresh_matches(FindState& find, const Document& document) {
    if (!find.stale && find.version == document.version()) return;
    FindOptions options = find_options(find.toggles);
    options.group = find.group;
    find.matches = find_all(document.buffer(), find.query, options);
    find.version = document.version();
    find.stale = false;
}

std::optional<std::size_t> current_match(const FindState& find, const Selections& selections) {
    const Span span = selections.primary().span();
    const auto found = first_from(find.matches, span.from);
    if (found == find.matches.end() || *found != span) return std::nullopt;
    return static_cast<std::size_t>(std::distance(find.matches.begin(), found));
}

std::optional<Span> match_from(const FindState& find, Position from) {
    if (find.matches.empty()) return std::nullopt;
    const auto found = first_from(find.matches, from);
    return found == find.matches.end() ? find.matches.front() : *found;
}

std::optional<Span> next_match(const FindState& find, const Selections& selections) {
    return match_from(find, selections.primary().span().to);
}

std::optional<Span> previous_match(const FindState& find, const Selections& selections) {
    if (find.matches.empty()) return std::nullopt;
    const auto found = first_from(find.matches, selections.primary().span().from);
    return found == find.matches.begin() ? find.matches.back() : *std::prev(found);
}

FindPlate find_plate(FindState& find, const Selections& selections, ui::core::Rect over) {
    FindPlate out;
    const float width = std::min(PLATE_WIDTH, over.width() - 2.0f * ui::PLATE_INSET);
    if (auto plate = ui::Plate("##find", over.min, over.max, ui::PlatePlace::TopRight, width)) {
        const float gap = ui::item_spacing().x;
        const float button = ui::frame_height();

        // The query, the count, then previous, next and close.
        const std::string count = match_count(find, selections);
        const float count_width = std::max(COUNT_WIDTH, ui::text_size(count).x);
        ui::next_width(plate.inner_width() - count_width - 3.0f * (button + gap) - gap);
        const ui::FindField field = ui::find_field("##query", find.query, find.toggles,
                                                   {.focus = find.focus == FindState::Focus::Query});
        out.searched = field.changed;
        out.next = field.next;
        out.previous = field.previous;
        out.closed = field.closed;
        ui::same_line();
        const glm::vec2 at = ui::cursor();
        ui::DrawList::window().text({at.x + (count_width - ui::text_size(count).x) * 0.5f, at.y + ui::frame_padding().y},
                                    theme::text_secondary(), count);
        ui::reserve({count_width, button});
        ui::same_line();
        out.previous |= ui::icon_button("##previous", ui::icon::previous_match, "Previous match (Shift Enter)");
        ui::same_line();
        out.next |= ui::icon_button("##next", ui::icon::next_match, "Next match (Enter)");
        ui::same_line();
        out.closed |= ui::icon_button("##close", ui::icon::clear, "Close (Escape)");

        // The replacement, Replace and Replace all.
        const std::string replace_label = std::string(ui::icon::replace) + "  Replace";
        const float replace_width = ui::text_size(replace_label).x + 2.0f * ui::frame_padding().x;
        const float all_width = ui::text_size("Replace all").x + 2.0f * ui::frame_padding().x;
        ui::next_width(plate.inner_width() - replace_width - all_width - 2.0f * gap);
        const ui::ReplaceField replacement =
            ui::replace_field("##replacement", find.replacement,
                              {.hint = "Replace", .focus = find.focus == FindState::Focus::Replacement});
        out.replace = replacement.replace;
        out.closed |= replacement.closed;
        ui::same_line();
        out.replace |= ui::action(ui::icon::replace, "Replace");
        ui::same_line();
        out.replace_all = ui::button("Replace all", ui::ButtonKind::Ghost);
    }
    if (out.searched) find.stale = true;
    find.focus = FindState::Focus::None;
    return out;
}

} // namespace fjedit
