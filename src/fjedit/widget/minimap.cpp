#include "fjedit/widget/minimap.hpp"

#include "fjedit/widget/code_colours.hpp"

#include "ftk/ui/core/context.hpp"
#include "ftk/ui/kit/state.hpp"
#include "ftk/ui/theme.hpp"

#include <algorithm>
#include <cmath>

namespace fjedit {

namespace {

namespace core = ftk::ui::core;
namespace theme = ftk::theme;
using ftk::ui::Color;

// Sheet 39's measures: rows this far apart, bars this tall, a character
// this wide, all in from the left and down from the top by the padding;
// words that touch a little apart.
constexpr float PITCH = 4.0f;
constexpr float BAR = 2.0f;
constexpr float CELL = 1.4f;
constexpr float PAD = 6.0f;
constexpr float GAP = 0.4f;
constexpr float BAR_ROUNDING = 0.5f;
// Code at this much of its colour, comments fainter.
constexpr float CODE_ALPHA = 0.8f;
constexpr float COMMENT_ALPHA = 0.55f;
// A tick at the right edge.
constexpr glm::vec2 TICK{6.0f, 3.0f};

// Where things are on a minimap. Taller than its place, it scrolls with the
// view: its band goes down it as the view goes down the text.
struct Geometry {
    core::Rect rect;
    std::size_t rows;
    float row_height;
    float view_height;
    // How far the view scrolls: until its last row is at its top.
    float max_scroll;

    [[nodiscard]] float content() const { return static_cast<float>(rows) * PITCH + 2.0f * PAD; }

    // How far the minimap's rows are moved up for the view at `scroll`.
    [[nodiscard]] float shift(float scroll) const {
        const float over = content() - rect.height();
        if (over <= 0.0f || max_scroll <= 0.0f) return 0.0f;
        return std::clamp(scroll / max_scroll, 0.0f, 1.0f) * over;
    }

    [[nodiscard]] float row_y(std::size_t row, float scroll) const {
        return rect.min.y + PAD + static_cast<float>(row) * PITCH - shift(scroll);
    }

    [[nodiscard]] float band_top(float scroll) const {
        return rect.min.y + PAD + scroll / row_height * PITCH - shift(scroll);
    }
    [[nodiscard]] float band_height() const { return view_height / row_height * PITCH; }

    // The scroll that puts the band's top at `y`.
    [[nodiscard]] float scroll_for_band_top(float y) const {
        const float over = content() - rect.height();
        float scroll = (y - rect.min.y - PAD) / PITCH * row_height;
        if (over > 0.0f && max_scroll > 0.0f) {
            // The band moves down the minimap's height less a row and the
            // padding while the view goes from top to bottom.
            const float travel = std::max(rect.height() - PITCH - 2.0f * PAD, 1.0f);
            scroll = (y - rect.min.y - PAD) / travel * max_scroll;
        }
        return std::clamp(scroll, 0.0f, max_scroll);
    }
};

bool space(char c) {
    return c == ' ' || c == '\t';
}

// A bar for each run of characters of one colour between spaces on line
// `line`, at `y`.
void draw_bars(core::DrawList& dl, const Document& document, std::size_t line, core::Rect rect, float y) {
    const std::string_view text = document.buffer().line(line);
    const std::span<const Token> tokens = document.syntax().tokens(line);
    const std::size_t tab = std::max<std::size_t>(document.indent().width, 1);
    std::size_t token = 0;
    const auto kind_at = [&](std::size_t byte) {
        while (token < tokens.size() && tokens[token].end <= byte) ++token;
        return token < tokens.size() && tokens[token].begin <= byte ? tokens[token].kind : TokenKind::Text;
    };

    std::size_t column = 0;
    std::size_t byte = 0;
    // The column after the character at `byte`, and the byte after it.
    const auto step = [&] {
        column = text[byte] == '\t' ? (column / tab + 1) * tab : column + 1;
        ++byte;
        while (byte < text.size() && (static_cast<unsigned char>(text[byte]) & 0xC0) == 0x80) ++byte;
    };
    while (byte < text.size()) {
        if (space(text[byte])) {
            step();
            continue;
        }
        const float from = rect.min.x + PAD + static_cast<float>(column) * CELL;
        if (from >= rect.max.x) break;
        const TokenKind kind = kind_at(byte);
        while (byte < text.size() && !space(text[byte]) && kind_at(byte) == kind) step();
        const float to = rect.min.x + PAD + static_cast<float>(column) * CELL - GAP;
        Color colour = code_colour(kind);
        colour.a *= kind == TokenKind::Comment ? COMMENT_ALPHA : CODE_ALPHA;
        dl.rect_filled({from, y}, {std::max(to, from + 1.0f), y + BAR}, colour, BAR_ROUNDING);
    }
}

} // namespace

Minimap minimap(ftk::ui::Id id, const Document& document, const Folds& folds, core::Rect rect, float scroll,
                float view_height, float row_height, std::span<const MinimapMark> marks) {
    core::Context& ui = core::current();
    const std::size_t rows = folds.row_count(document.buffer().line_count());
    const Geometry geometry{.rect = rect,
                            .rows = rows,
                            .row_height = row_height,
                            .view_height = view_height,
                            .max_scroll = static_cast<float>(rows - 1) * row_height};

    // A press on the band drags it from where it was taken; one beside it
    // takes the band's middle there first.
    Minimap out;
    const core::Interaction in = ui.interact(id, rect, {.beneath = true});
    out.hovered = in.hovered;
    const ftk::ui::Id grab_id = core::hash_label("##grab", id);
    if (in.pressed) {
        float grab = in.press_position.y - geometry.band_top(scroll);
        if (grab < 0.0f || grab > geometry.band_height()) {
            grab = geometry.band_height() * 0.5f;
            out.scroll = geometry.scroll_for_band_top(in.press_position.y - grab);
        }
        ftk::ui::store_float(grab_id, grab);
    }
    if (in.held && in.dragging) {
        out.scroll = geometry.scroll_for_band_top(in.position.y - ftk::ui::stored_float(grab_id));
    }

    core::DrawList& dl = ui.draw_list();
    dl.rect_filled(rect.min, rect.max, theme::surface_base());
    dl.push_clip(rect.min, rect.max);
    dl.rect_filled(rect.min, {rect.min.x + 1.0f, rect.max.y}, theme::code_indent_guide());
    const float band = geometry.band_top(scroll);
    dl.rect_filled({rect.min.x + 1.0f, band}, {rect.max.x, band + geometry.band_height()}, theme::surface_raised());

    const float shift = geometry.shift(scroll);
    const auto first = static_cast<std::size_t>(std::max(0.0f, std::floor((shift - PAD) / PITCH)));
    const auto last = std::min(rows, static_cast<std::size_t>(std::ceil((shift + rect.height()) / PITCH)) + 1);
    if (first < last) {
        std::size_t line = folds.line_of(first);
        for (std::size_t row = first; row < last; ++row) {
            draw_bars(dl, document, line, rect, geometry.row_y(row, scroll));
            line = folds.row_lines(line).last + 1;
        }
    }
    for (const MinimapMark& mark : marks) {
        const float y = geometry.row_y(mark.row, scroll);
        dl.rect_filled({rect.max.x - TICK.x, y}, {rect.max.x, y + TICK.y}, mark.colour);
    }
    dl.pop_clip();
    return out;
}

} // namespace fjedit
