#include "fjedit/widget/code_view.hpp"

#include "fjedit/core/commands.hpp"
#include "fjedit/core/find.hpp"
#include "fjedit/core/structure.hpp"
#include "fjedit/widget/code_colours.hpp"
#include "fjedit/widget/minimap.hpp"
#include "fjedit/widget/text_layout.hpp"

#include "ftk/ui/core/context.hpp"
#include "ftk/ui/kit/icons.hpp"
#include "ftk/ui/kit/scope.hpp"
#include "ftk/ui/kit/scrollbar.hpp"
#include "ftk/ui/kit/text.hpp"
#include "ftk/ui/theme.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fjedit {

namespace {

namespace core = ftk::ui::core;
namespace theme = ftk::theme;
using ftk::ui::Color;

// The carets show for this long of each blink, out of the whole.
constexpr double BLINK = 1.2;
constexpr double BLINK_ON = 0.8;

// Across the gutter: room before the line numbers, and the fold column
// after them; then room before the text.
constexpr float GUTTER_PAD = 8.0f;
constexpr float FOLD_COLUMN = 16.0f;
constexpr float TEXT_PAD = 6.0f;
// How much room the caret keeps from the text's edge as it scrolls along.
constexpr float REVEAL_MARGIN = 24.0f;
constexpr float CARET_WIDTH = 2.0f;
// The scrollbar down the view's right edge (sheet 39).
constexpr float SCROLLBAR_WIDTH = 10.0f;
// How far up from a row's bottom the line under composing text is.
constexpr float COMPOSING_UNDERLINE = 3.0f;

// A closed fold's mark: a pill holding an ellipsis in a smaller face, set
// off from the text either side.
constexpr float PILL_TEXT = 11.0f;
constexpr float PILL_HEIGHT = 16.0f;
constexpr float PILL_PAD = 6.0f;
constexpr float PILL_MARGIN = 4.0f;
constexpr std::string_view ELLIPSIS = "\xE2\x8B\xAF"; // ⋯

// The kit's mono face at `size`, on the code's lines.
core::TextStyle mono(float size) {
    const ftk::ui::FontScope scope(theme::mono_font(), size);
    core::TextStyle style = core::current().text().style();
    style.line_height = CODE_LINE;
    return style;
}

// The faces the view draws in besides the code's, and what it measures by
// them.
struct Faces {
    core::TextStyle numbers;
    core::TextStyle pill;
    float digit_width{0.0f};
    float pill_width{0.0f};
};

// The selections and folds brought up to date with edits made in another
// view.
void catch_up(const Document& document, EditorView& view) {
    view.folds.catch_up(document);
    if (view.seen == document.version()) return;
    if (const auto edits = document.edits_since(view.seen)) {
        for (const Edit& edit : *edits) view.selections.map(edit);
    } else {
        view.selections.clamp(document.buffer());
    }
    view.seen = document.version();
}

// Where the parts of the view go this frame.
struct Frame {
    core::Rect rect;
    // Right of the line numbers.
    float numbers_right{0.0f};
    // Where the text is drawn, and where a line's start is, scrolled.
    core::Rect text;
    float origin_x{0.0f};
};

Frame place(core::Rect rect, const EditorView& view, const Buffer& buffer, float digit_width) {
    std::size_t digits = 1;
    for (std::size_t n = buffer.line_count(); n >= 10; n /= 10) ++digits;
    Frame frame;
    frame.rect = rect;
    frame.numbers_right = rect.min.x + GUTTER_PAD + static_cast<float>(std::max<std::size_t>(digits, 2)) * digit_width;
    frame.text = {.min = {frame.numbers_right + FOLD_COLUMN, rect.min.y}, .max = rect.max};
    frame.origin_x = frame.text.min.x + TEXT_PAD - view.scroll.x;
    return frame;
}

// The line the row at `y`, a height in the window, begins with.
std::size_t line_under(const Frame& frame, const EditorView& view, const Buffer& buffer, float y) {
    const float down = y - frame.rect.min.y + view.scroll.y;
    const auto row = static_cast<std::size_t>(std::max(0.0f, std::floor(down / CODE_LINE)));
    return view.folds.line_of(std::min(row, view.folds.row_count(buffer.line_count()) - 1));
}

// The position under `point`, a place in the window: on a folded row, in its
// first line.
Position position_under(const Frame& frame, const EditorView& view, const Buffer& buffer, const TextLayout& layout,
                        glm::vec2 point) {
    return layout.position_at(buffer, line_under(frame, view, buffer, point.y), point.x - frame.origin_x);
}

// How fast a drag past the view's top or bottom scrolls: this share of how
// far past it the pointer is, each frame.
constexpr float DRAG_SCROLL = 0.25f;

// The row `line` begins whole: its lines through the last one's break.
Span whole_row(const Buffer& buffer, const Folds& folds, std::size_t line) {
    const std::size_t last = folds.row_lines(line).last;
    const Position end = last + 1 < buffer.line_count() ? Position{last + 1, 0} : Position{last, buffer.line(last).size()};
    return {.from = {line, 0}, .to = end};
}

// What a folded row draws after its first line, for each fold along it: a
// pill for what the fold hides, then the closer's line from its closer when
// the fold ends on a line that begins with one (`begin_play() {⋯ }`).
struct FoldMark {
    // The line the fold begins on.
    std::size_t fold{0};
    // Along the row, in layout units.
    float pill_from{0.0f};
    float pill_to{0.0f};
    // Where the closer's line is drawn from, when it is.
    std::optional<Position> closer{};
    float closer_x{0.0f};
};

struct DrawnRow {
    std::vector<FoldMark> marks;
    // Where what the row draws ends.
    float end{0.0f};
};

// The byte of the closing bracket line `line` begins with, after its
// indent; nothing when it begins with something else.
std::optional<std::size_t> leading_closer(const Document& document, std::size_t line) {
    const std::string_view text = document.buffer().line(line);
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos || std::string_view(")]}").find(text[first]) == std::string_view::npos) {
        return std::nullopt;
    }
    if (in_string_or_comment(document.syntax(), line, first)) return std::nullopt;
    return first;
}

// What the row `line` begins draws: its first line, and the marks of the
// folds closed along it.
DrawnRow drawn_row(const Document& document, const Folds& folds, const TextLayout& layout, float pill_width,
                   std::size_t line) {
    const Buffer& buffer = document.buffer();
    DrawnRow row;
    row.end = layout.x_of(buffer, {line, buffer.line(line).size()});
    const std::size_t last = folds.row_lines(line).last;
    for (std::size_t at = line; at < last;) {
        const std::optional<FoldRange> fold = folds.closed_at(at);
        if (!fold) break;
        FoldMark mark{.fold = at, .pill_from = row.end + PILL_MARGIN};
        mark.pill_to = mark.pill_from + pill_width;
        row.end = mark.pill_to;
        if (const std::optional<std::size_t> byte = leading_closer(document, fold->last)) {
            const std::string_view text = buffer.line(fold->last);
            mark.closer = Position{fold->last, *byte};
            mark.closer_x = mark.pill_to + PILL_MARGIN;
            row.end = layout.advance(text, *byte, text.size(), mark.closer_x);
        }
        row.marks.push_back(mark);
        at = fold->last;
    }
    return row;
}

// A press on a fold's chevron opens or closes it, and one on a closed fold's
// pill opens that fold; true when the press was either. Carets a fold closes
// over go to its first line's end.
bool take_fold_press(const Frame& frame, const Document& document, EditorView& view, const TextLayout& layout,
                     float pill_width, std::size_t line, glm::vec2 point) {
    if (point.x >= frame.numbers_right && point.x < frame.text.min.x) {
        if (view.folds.open_row(line)) return true;
        if (!view.folds.close(document, line)) return false;
        move_out_of_folds(view.selections, view.folds, document.buffer());
        return true;
    }
    if (point.x < frame.text.min.x) return false;
    const float x = point.x - frame.origin_x;
    for (const FoldMark& mark : drawn_row(document, view.folds, layout, pill_width, line).marks) {
        if (x >= mark.pill_from && x < mark.pill_to) return view.folds.open(mark.fold);
    }
    return false;
}

// The word at `at`, or just `at` where there is none.
Span word_or_place(const Buffer& buffer, Position at) {
    const Span word = word_at(buffer, at);
    return word.empty() ? Span{at, at} : word;
}

// From `origin`, what a press took, to `unit`, the word or line the drag is
// over: whole units both ends, the caret at the drag's end.
Selection by_units(Span origin, Span unit) {
    if (unit.from < origin.from) return {.anchor = origin.to, .caret = unit.from};
    return {.anchor = origin.from, .caret = std::max(unit.to, origin.to)};
}

// What the mouse does: a press places the caret, selects a word (twice) or a
// row (three times, or on the line numbers); with Shift it takes the primary
// selection there; with Alt it begins a column, or adds a caret if it doesn't
// drag. Dragging goes on in the same unit, and past the top or bottom it
// scrolls. A press on a fold's chevron or pill opens or closes it instead.
void take_mouse(core::Context& ui, const core::Interaction& in, const Frame& frame, Document& document,
                EditorView& view, const TextLayout& layout, float pill_width) {
    using By = EditorView::Drag::By;
    const Buffer& buffer = document.buffer();
    EditorView::Drag& drag = view.drag;
    const glm::vec2 point = in.position;
    const ftk::ui::Modifiers held = ui.modifiers();

    const bool fold_press = in.pressed && in.button == ftk::ui::MouseButton::Left &&
                            take_fold_press(frame, document, view, layout, pill_width,
                                            line_under(frame, view, buffer, point.y), point);
    if (in.pressed && in.button == ftk::ui::MouseButton::Left && !fold_press) {
        const Position at = position_under(frame, view, buffer, layout, point);
        const core::Pointer* mouse = ui.find_pointer(core::PointerType::Mouse, 0);
        const int clicks = mouse != nullptr ? mouse->click_count[0] : 1;
        drag = {.active = true, .origin = {at, at}};
        if (held.alt) {
            drag.by = By::Column;
            drag.line = at.line;
            drag.x = point.x - frame.origin_x;
            drag.before = view.selections;
        } else if (point.x < frame.text.min.x || clicks >= 3) {
            drag.by = By::Line;
            drag.origin = whole_row(buffer, view.folds, at.line);
            view.selections = Selections({.anchor = drag.origin.from, .caret = drag.origin.to});
        } else if (clicks == 2) {
            drag.by = By::Word;
            drag.origin = word_or_place(buffer, at);
            view.selections = Selections({.anchor = drag.origin.from, .caret = drag.origin.to});
        } else if (held.shift) {
            const Position anchor = view.selections.primary().anchor;
            drag.origin = {anchor, anchor};
            view.selections = Selections({.anchor = anchor, .caret = at});
        } else {
            view.selections = Selections(Selection::at(at));
        }
        document.history().close();
    }

    if (drag.active && in.held && in.dragging) {
        drag.moved = true;
        const Position at = position_under(frame, view, buffer, layout, point);
        switch (drag.by) {
        case By::Character: view.selections = Selections({.anchor = drag.origin.from, .caret = at}); break;
        case By::Word: view.selections = Selections(by_units(drag.origin, word_or_place(buffer, at))); break;
        case By::Line: view.selections = Selections(by_units(drag.origin, whole_row(buffer, view.folds, at.line))); break;
        case By::Column:
            view.selections = column_selection(buffer, layout, drag.line, drag.x, at.line, point.x - frame.origin_x);
            break;
        }
        if (point.y < frame.rect.min.y) view.scroll.y -= (frame.rect.min.y - point.y) * DRAG_SCROLL;
        if (point.y > frame.rect.max.y) view.scroll.y += (point.y - frame.rect.max.y) * DRAG_SCROLL;
        if (point.y < frame.rect.min.y || point.y > frame.rect.max.y) ui.request_frame();
    }

    if (in.released || in.cancelled) {
        if (drag.active && drag.by == By::Column && !drag.moved && !in.cancelled) {
            view.selections = drag.before;
            view.selections.add(Selection::at(drag.origin.from));
        }
        drag.active = false;
    }
}

// What the keys pressed so far come to: in the finding mode first while
// the find is open, then in the view's mode.
KeyMatch match_keys(const Keymap& keymap, const EditorView& view) {
    KeyMatch match = keymap.find(view.mode, view.pending);
    if (view.find.open) {
        const KeyMatch finding = keymap.find(Keymap::FINDING, view.pending);
        if (finding.command) return finding;
        match.waiting = match.waiting || finding.waiting;
    }
    return match;
}

// The keys pressed this frame, through the keymap: a whole binding runs its
// command, the start of a longer one waits for the next key.
template <typename Run>
void take_keys(core::Context& ui, const Keymap& keymap, EditorView& view, Run&& run) {
    const ftk::ui::Modifiers held = ui.modifiers();
    for (std::size_t k = 0; k < ftk::ui::KEY_COUNT; ++k) {
        const auto key = static_cast<ftk::ui::Key>(k);
        if (!ui.key_repeated(key)) continue;
        view.pending.push_back({.key = key, .modifiers = held});
        const KeyMatch match = match_keys(keymap, view);
        if (match.command) {
            const std::string command(*match.command);
            view.pending.clear();
            run(command);
        } else if (!match.waiting) {
            view.pending.clear();
        }
    }
}

// Selects `match`, as the find goes to it.
void select_match(EditorView& view, Span match) {
    view.selections = Selections({.anchor = match.from, .caret = match.to});
}

// Runs a command the view does itself: opens, steps through or closes the
// find, or opens go to line.
void run_view_command(std::string_view name, const Document& document, EditorView& view) {
    FindState& find = view.find;
    if (name == "goto.open") {
        view.go_to_line.open = true;
        view.go_to_line.focus = true;
        view.go_to_line.text.clear();
    } else if (name == "find.open" || name == "find.replace") {
        open_find(find, document.buffer(), view.selections,
                  name == "find.open" ? FindState::Focus::Query : FindState::Focus::Replacement);
    } else if (name == "find.close") {
        find.open = false;
    } else if (name == "find.next" || name == "find.previous") {
        if (find.query.empty()) {
            open_find(find, document.buffer(), view.selections, FindState::Focus::Query);
            return;
        }
        refresh_matches(find, document);
        const std::optional<Span> match =
            name == "find.next" ? next_match(find, view.selections) : previous_match(find, view.selections);
        if (match) select_match(view, *match);
    }
}

// Does what was done on the find's plate this frame: goes to a match,
// replaces, or closes it.
void take_find_plate(const FindPlate& plate, Document& document, EditorView& view, const TextLayout& layout) {
    FindState& find = view.find;
    refresh_matches(find, document);
    const FindOptions options = find_options(find.toggles);
    CommandContext context{.document = document, .selections = view.selections, .layout = layout,
                           .folds = &view.folds};
    if (plate.searched) {
        if (const std::optional<Span> match = match_from(find, view.selections.primary().span().from)) {
            select_match(view, *match);
        }
    }
    if (plate.next) {
        if (const std::optional<Span> match = next_match(find, view.selections)) select_match(view, *match);
    }
    if (plate.previous) {
        if (const std::optional<Span> match = previous_match(find, view.selections)) select_match(view, *match);
    }
    if (plate.replace) {
        // The match selected is replaced and the find goes on past it; with
        // none selected, it only goes on to the next.
        Position on = view.selections.primary().span().to;
        if (const std::optional<std::size_t> current = current_match(find, view.selections)) {
            const std::vector<Span> replaced =
                replace_matches(context, std::span(&find.matches[*current], 1), find.query, find.replacement, options);
            refresh_matches(find, document);
            on = replaced.front().to;
        }
        if (const std::optional<Span> match = match_from(find, on)) select_match(view, *match);
    }
    if (plate.replace_all) {
        (void)replace_matches(context, find.matches, find.query, find.replacement, options);
        refresh_matches(find, document);
    }
    if (plate.closed) find.open = false;
}

// The wheel scrolls: down the text, or along it with Shift. A notch up or
// left is positive; the scroll grows down and right.
void take_wheel(core::Context& ui, EditorView& view) {
    if (const core::Pointer* mouse = ui.find_pointer(core::PointerType::Mouse, 0)) {
        const float step = ui.pointer_style().wheel_step;
        const bool along = ui.modifiers().shift;
        view.scroll.x += (along ? -mouse->wheel.y : mouse->wheel.x) * step;
        view.scroll.y += (along ? 0.0f : -mouse->wheel.y) * step;
    }
}

// The minimap and the scrollbar down the view's right side, `side`, with
// ticks for the find's matches and the caret. They scroll the view, as the
// wheel does over the minimap; what they do shows from the next frame.
void draw_side(core::Context& ui, const Document& document, EditorView& view, core::Rect rect, core::Rect side) {
    const Folds& folds = view.folds;
    const std::size_t rows = folds.row_count(document.buffer().line_count());
    std::vector<MinimapMark> ticks;
    std::vector<ftk::ui::ScrollMark> marks;
    const auto mark = [&](std::size_t row, ftk::ui::Color colour) {
        if (!ticks.empty() && ticks.back().row == row && ticks.back().colour == colour) return;
        ticks.push_back({.row = row, .colour = colour});
        marks.push_back({.at = static_cast<float>(row) * CODE_LINE, .colour = colour});
    };
    if (view.find.open) {
        for (const Span& match : view.find.matches) mark(folds.row_of(match.from.line), theme::accent());
    }
    mark(folds.row_of(view.selections.primary().caret.line), theme::text());

    const glm::vec2 scrolled = view.scroll;
    ui.draw_list().rect_filled(side.min, side.max, theme::surface_base());
    if (view.minimap) {
        const core::Rect map{.min = side.min, .max = {side.max.x - SCROLLBAR_WIDTH, side.max.y}};
        const Minimap drawn = minimap(ui.id("##minimap"), document, folds, map, view.scroll.y, rect.height(), CODE_LINE, ticks);
        if (drawn.scroll) view.scroll.y = *drawn.scroll;
        if (drawn.hovered) take_wheel(ui, view);
    }
    float offset = view.scroll.y;
    const float room = static_cast<float>(rows - 1) * CODE_LINE;
    if (ftk::ui::vertical_scrollbar("##scrollbar", {side.max.x - SCROLLBAR_WIDTH, side.min.y}, side.max, offset,
                                    rect.height(), room, marks)) {
        view.scroll.y = offset;
    }
    if (view.scroll != scrolled) ui.request_frame();
}

// Scrolls so the primary caret is in sight, then keeps the scroll inside
// the text.
void reveal(EditorView& view, const Frame& frame, const Buffer& buffer, const TextLayout& layout) {
    if (view.reveal) {
        const Position caret = view.selections.primary().caret;
        const float top = static_cast<float>(view.folds.row_of(caret.line)) * CODE_LINE;
        if (top < view.scroll.y) {
            view.scroll.y = top;
        } else if (top + CODE_LINE > view.scroll.y + frame.rect.height()) {
            view.scroll.y = top + CODE_LINE - frame.rect.height();
        }
        const float x = layout.x_of(buffer, caret);
        const float width = frame.text.width() - 2.0f * TEXT_PAD;
        if (x < view.scroll.x + REVEAL_MARGIN) {
            view.scroll.x = x - REVEAL_MARGIN;
        } else if (x > view.scroll.x + width - REVEAL_MARGIN) {
            view.scroll.x = x - width + REVEAL_MARGIN;
        }
        view.reveal = false;
    }
    const float bottom = static_cast<float>(view.folds.row_count(buffer.line_count()) - 1) * CODE_LINE;
    view.scroll.y = std::clamp(view.scroll.y, 0.0f, std::max(bottom, 0.0f));
    view.scroll.x = std::max(view.scroll.x, 0.0f);
}

// Draws `text`'s bytes `from` to `to` in `colour`, the first at `x` along
// from `origin_x`; returns where the last ends. Tabs draw nothing and reach
// on to their stops.
float draw_run(core::Context& ui, const TextLayout& layout, std::string_view text, std::size_t from, std::size_t to,
               float x, float origin_x, float y, Color colour) {
    core::DrawList& dl = ui.draw_list();
    while (from < to) {
        const std::size_t tab = text.find('\t', from);
        const std::size_t end = std::min(tab, to);
        if (end > from) {
            ui.text().draw(dl, {origin_x + x, y}, colour, text.substr(from, end - from), layout.style());
            x = layout.advance(text, from, end, x);
        }
        if (end == to) break;
        x = layout.advance(text, end, end + 1, x);
        from = end + 1;
    }
    return x;
}

// Line `line`'s text from byte `from` up to byte `to`, coloured by its
// tokens, the first byte drawn at `x` along from `origin_x`; returns where
// it ends.
float draw_tokens(core::Context& ui, const Document& document, const TextLayout& layout, std::size_t line,
                  std::size_t from, std::size_t to, float x, float origin_x, float y) {
    const std::string_view text = document.buffer().line(line);
    to = std::min(to, text.size());
    std::size_t at = from;
    for (const Token& token : document.syntax().tokens(line)) {
        if (token.end <= at) continue;
        if (token.begin >= to) break;
        const std::size_t begin = std::max<std::size_t>(token.begin, at);
        const std::size_t end = std::min<std::size_t>(token.end, to);
        x = draw_run(ui, layout, text, at, begin, x, origin_x, y, theme::text());
        x = draw_run(ui, layout, text, begin, end, x, origin_x, y, code_colour(token.kind));
        at = end;
    }
    return draw_run(ui, layout, text, at, to, x, origin_x, y, theme::text());
}

// The rows in sight: the line each begins with, the first drawn at `top`.
struct Shown {
    std::size_t first_row{0};
    std::vector<std::size_t> lines;
    float top{0.0f};

    [[nodiscard]] float y(std::size_t index) const { return top + static_cast<float>(index) * CODE_LINE; }

    // Which of them `line` is drawn on, when it is in sight and not hidden.
    [[nodiscard]] std::optional<std::size_t> index_of(const Folds& folds, std::size_t line) const {
        if (folds.hidden(line)) return std::nullopt;
        const std::size_t row = folds.row_of(line);
        if (row < first_row || row >= first_row + lines.size()) return std::nullopt;
        return row - first_row;
    }
};

Shown shown(const Frame& frame, const EditorView& view, const Buffer& buffer) {
    const std::size_t rows = view.folds.row_count(buffer.line_count());
    const std::size_t count = static_cast<std::size_t>(std::ceil(frame.rect.height() / CODE_LINE)) + 1;
    Shown out;
    out.first_row = std::min(static_cast<std::size_t>(view.scroll.y / CODE_LINE), rows);
    out.top = frame.rect.min.y - (view.scroll.y - static_cast<float>(out.first_row) * CODE_LINE);
    if (out.first_row == rows) return out;
    for (std::size_t line = view.folds.line_of(out.first_row); out.lines.size() < count && line < buffer.line_count();
         line = view.folds.row_lines(line).last + 1) {
        out.lines.push_back(line);
    }
    return out;
}

// How far a blank line looks up and down for a line with text, whose indent
// it borrows for its guides.
constexpr std::size_t GUIDE_LOOK = 200;
constexpr float DECORATION_ROUNDING = 2.0f;

// How many levels of indent line `line` is in: its leading spaces and tabs,
// in levels of `width` cells. A blank line is as deep as the shallower of the
// lines with text either side of it, so guides run on through it.
std::size_t indent_level(const Buffer& buffer, std::size_t line, std::size_t width) {
    auto of = [&](std::size_t index) -> std::optional<std::size_t> {
        const std::string_view text = buffer.line(index);
        const std::size_t first = text.find_first_not_of(" \t");
        if (first == std::string_view::npos) return std::nullopt;
        return column_of(text, first, width) / width;
    };
    if (const auto level = of(line)) return *level;
    std::size_t above = 0;
    for (std::size_t i = line, looked = 0; i-- > 0 && looked < GUIDE_LOOK; ++looked) {
        if (const auto level = of(i)) {
            above = *level;
            break;
        }
    }
    std::size_t below = 0;
    for (std::size_t i = line + 1, looked = 0; i < buffer.line_count() && looked < GUIDE_LOOK; ++i, ++looked) {
        if (const auto level = of(i)) {
            below = *level;
            break;
        }
    }
    return std::min(above, below);
}

// Indent guides: one at the start of each level of every row's indent; the
// guide of the block the primary caret is in brighter.
void draw_guides(core::DrawList& dl, const Document& document, const EditorView& view, const Frame& frame,
                 const TextLayout& layout, const Shown& rows) {
    const Buffer& buffer = document.buffer();
    const std::size_t first = rows.lines.front();
    const std::size_t last = view.folds.row_lines(rows.lines.back()).last + 1;
    const std::size_t width = std::max<std::size_t>(document.indent().width, 1);
    const float step = layout.space_width() * static_cast<float>(width);

    // The block the caret is in: the guide just left of its line's text, on
    // the lines around it indented deeper than that guide.
    const std::size_t caret = view.selections.primary().caret.line;
    const std::size_t caret_level = indent_level(buffer, caret, width);
    std::size_t active_from = 1;
    std::size_t active_to = 0;
    if (caret_level > 0) {
        const std::size_t guide = caret_level - 1;
        active_from = caret;
        while (active_from > first && indent_level(buffer, active_from - 1, width) > guide) --active_from;
        active_to = caret;
        while (active_to + 1 < last && indent_level(buffer, active_to + 1, width) > guide) ++active_to;
    }

    for (std::size_t index = 0; index < rows.lines.size(); ++index) {
        const std::size_t line = rows.lines[index];
        const float y = rows.y(index);
        const std::size_t levels = indent_level(buffer, line, width);
        for (std::size_t level = 0; level < levels; ++level) {
            const bool active = level + 1 == caret_level && line >= active_from && line <= active_to;
            const float x = std::round(frame.origin_x + static_cast<float>(level) * step);
            dl.rect_filled({x, y}, {x + 1.0f, y + CODE_LINE},
                           active ? theme::code_indent_guide_active() : theme::code_indent_guide());
        }
    }
}

// The other uses of the word at the primary caret, in sight, each in a quiet
// box; and the bracket paired with the one at the caret, both outlined.
void draw_marks(core::DrawList& dl, const Document& document, const EditorView& view, const Frame& frame,
                const TextLayout& layout, const Shown& rows) {
    const Buffer& buffer = document.buffer();
    const Selection& primary = view.selections.primary();
    const std::size_t first = rows.lines.front();
    const std::size_t last = view.folds.row_lines(rows.lines.back()).last + 1;
    auto box = [&](std::size_t index, Position from, Position to) {
        const float y = rows.y(index);
        return core::Rect{.min = {frame.origin_x + layout.x_of(buffer, from), y},
                          .max = {frame.origin_x + layout.x_of(buffer, to), y + CODE_LINE}};
    };

    if (primary.empty()) {
        const Span word = word_at(buffer, primary.caret);
        if (!word.empty()) {
            const FindOptions whole{.match_case = true, .whole_word = true};
            for (const Span& use : find_in_lines(buffer, buffer.text(word), first, last, whole)) {
                const std::optional<std::size_t> index = rows.index_of(view.folds, use.from.line);
                if (use == word || use.from.line != use.to.line || !index) continue;
                const core::Rect r = box(*index, use.from, use.to);
                dl.rect_filled(r.min, r.max, theme::code_occurrence(), DECORATION_ROUNDING);
            }
        }
    }

    if (const std::optional<BracketPair> pair = matching_bracket(buffer, document.syntax(), primary.caret)) {
        for (const Position at : {pair->open, pair->close}) {
            const std::optional<std::size_t> index = rows.index_of(view.folds, at.line);
            if (!index) continue;
            const core::Rect r = box(*index, at, {at.line, at.byte + 1});
            dl.rect(r.min + glm::vec2{0.0f, 1.0f}, r.max - glm::vec2{0.0f, 1.0f}, theme::code_bracket(),
                    DECORATION_ROUNDING);
        }
    }
}

// The find's matches in sight, each in a light wash; the one the primary
// selection is outlined in the accent, `outline` once the selections are
// drawn.
void draw_matches(core::DrawList& dl, const Document& document, const EditorView& view, const Frame& frame,
                  const TextLayout& layout, const Shown& rows, bool outline) {
    const Buffer& buffer = document.buffer();
    const std::vector<Span>& matches = view.find.matches;
    const std::optional<std::size_t> current = current_match(view.find, view.selections);
    const std::size_t end_line = view.folds.row_lines(rows.lines.back()).last + 1;
    for (auto match = std::ranges::lower_bound(matches, Position{rows.lines.front(), 0}, {}, &Span::from);
         match != matches.end() && match->from.line < end_line; ++match) {
        const std::optional<std::size_t> index = rows.index_of(view.folds, match->from.line);
        const bool is_current = current && &matches[*current] == &*match;
        if (!index || outline != is_current) continue;
        const Position to = match->to.line == match->from.line ? match->to
                                                                : Position{match->from.line, buffer.line(match->from.line).size()};
        const glm::vec2 min{frame.origin_x + layout.x_of(buffer, match->from), rows.y(*index)};
        const glm::vec2 max{frame.origin_x + layout.x_of(buffer, to), rows.y(*index) + CODE_LINE};
        if (outline) {
            dl.rect(min, max, theme::accent(), DECORATION_ROUNDING);
        } else {
            dl.rect_filled(min, max, theme::selection_secondary(), DECORATION_ROUNDING);
        }
    }
}

void draw(core::Context& ui, const Document& document, const EditorView& view, const Frame& frame,
          const TextLayout& layout, const Faces& faces, bool focused) {
    const Buffer& buffer = document.buffer();
    const Folds& folds = view.folds;
    core::DrawList& dl = ui.draw_list();
    const Shown rows = shown(frame, view, buffer);
    auto caret_on = [&](std::size_t line) {
        return std::any_of(view.selections.all().begin(), view.selections.all().end(),
                           [&](const Selection& s) { return s.caret.line == line; });
    };
    std::vector<DrawnRow> drawn;
    drawn.reserve(rows.lines.size());
    for (const std::size_t line : rows.lines) drawn.push_back(drawn_row(document, folds, layout, faces.pill_width, line));

    dl.rect_filled(frame.rect.min, frame.rect.max, theme::surface_base());
    dl.push_clip(frame.rect.min, frame.rect.max);

    // The caret's row, a shade lighter across the gutter and the text,
    // while it selects nothing.
    for (const Selection& selection : view.selections.all()) {
        const std::optional<std::size_t> index = rows.index_of(folds, selection.caret.line);
        if (!selection.empty() || !index) continue;
        dl.rect_filled({frame.rect.min.x, rows.y(*index)}, {frame.rect.max.x, rows.y(*index) + CODE_LINE},
                       theme::code_current_line());
    }

    // Line numbers, right-aligned, the caret's line's brighter; after them a
    // chevron where a fold begins, pointing along the row while it's closed.
    const std::vector<FoldRange>& ranges = document.folds();
    for (std::size_t index = 0; index < rows.lines.size(); ++index) {
        const std::size_t line = rows.lines[index];
        const std::string number = std::to_string(line + 1);
        const float width = ui.text().caret_x(number, faces.numbers, number.size());
        const Color colour = caret_on(line) ? theme::text_secondary() : theme::text_disabled();
        ui.text().draw(dl, {frame.numbers_right - width, rows.y(index)}, colour, number, faces.numbers);
        if (std::ranges::binary_search(ranges, line, {}, &FoldRange::first)) {
            const std::string_view chevron = folds.closed_at(line) ? ftk::ui::icon::fold_closed : ftk::ui::icon::fold_open;
            const float chevron_width = ui.text().caret_x(chevron, faces.numbers, chevron.size());
            ui.text().draw(dl, {std::round(frame.numbers_right + (FOLD_COLUMN - chevron_width) * 0.5f), rows.y(index)},
                           theme::text_disabled(), chevron, faces.numbers);
        }
    }

    dl.push_clip(frame.text.min, frame.text.max);

    const bool finding = view.find.open && !rows.lines.empty();
    if (!rows.lines.empty()) {
        draw_guides(dl, document, view, frame, layout, rows);
        draw_marks(dl, document, view, frame, layout, rows);
    }
    if (finding) draw_matches(dl, document, view, frame, layout, rows, false);

    // Selections, a line break taking a space's width. On a folded row, what
    // the fold hides is selected with its mark.
    for (const Selection& selection : view.selections.all()) {
        if (selection.empty()) continue;
        const Span span = selection.span();
        for (std::size_t index = 0; index < rows.lines.size(); ++index) {
            const FoldRange row = folds.row_lines(rows.lines[index]);
            if (span.to <= Position{row.first, 0} || span.from > Position{row.last, buffer.line(row.last).size()}) {
                continue;
            }
            const DrawnRow& at = drawn[index];
            const float hidden_x = at.marks.empty() ? at.end : at.marks.front().pill_from;
            const float from = span.from.line < row.first    ? 0.0f
                               : span.from.line == row.first ? layout.x_of(buffer, span.from)
                                                             : hidden_x;
            const float to = span.to.line > row.last     ? at.end + layout.space_width()
                             : span.to.line == row.first ? layout.x_of(buffer, span.to)
                                                         : at.end;
            dl.rect_filled({frame.origin_x + from, rows.y(index)}, {frame.origin_x + to, rows.y(index) + CODE_LINE},
                           theme::selection());
        }
    }

    if (finding) draw_matches(dl, document, view, frame, layout, rows, true);

    // What an input method is composing, while the view has the keyboard: it
    // shows at the primary caret, underlined, the rest of the line moved on.
    const std::string_view composing = focused ? ui.composition() : std::string_view{};
    const Position caret = view.selections.primary().caret;
    const float caret_x = layout.x_of(buffer, caret);
    const float composed_width = layout.advance(composing, 0, composing.size(), caret_x) - caret_x;

    // The text, coloured by its tokens; on a folded row, each fold's pill
    // and the closer's line after it.
    for (std::size_t index = 0; index < rows.lines.size(); ++index) {
        const float y = rows.y(index);
        const std::size_t line = rows.lines[index];
        if (!composing.empty() && line == caret.line) {
            (void)draw_tokens(ui, document, layout, line, 0, caret.byte, 0.0f, frame.origin_x, y);
            const float left = frame.origin_x + caret_x;
            ui.text().draw(dl, {left, y}, theme::text(), composing, layout.style());
            dl.line({left, y + CODE_LINE - COMPOSING_UNDERLINE}, {left + composed_width, y + CODE_LINE - COMPOSING_UNDERLINE},
                    theme::text());
            (void)draw_tokens(ui, document, layout, line, caret.byte, buffer.line(line).size(), caret_x + composed_width,
                              frame.origin_x, y);
        } else {
            (void)draw_tokens(ui, document, layout, line, 0, buffer.line(line).size(), 0.0f, frame.origin_x, y);
        }
        for (const FoldMark& mark : drawn[index].marks) {
            const float left = std::round(frame.origin_x + mark.pill_from);
            const float top = y + (CODE_LINE - PILL_HEIGHT) * 0.5f;
            dl.rect_filled({left, top}, {std::round(frame.origin_x + mark.pill_to), top + PILL_HEIGHT},
                           theme::surface_raised(), PILL_HEIGHT * 0.5f);
            ui.text().draw(dl, {left + PILL_PAD, y}, theme::text_secondary(), ELLIPSIS, faces.pill);
            if (mark.closer) {
                (void)draw_tokens(ui, document, layout, mark.closer->line, mark.closer->byte,
                                  buffer.line(mark.closer->line).size(), mark.closer_x, frame.origin_x, y);
            }
        }
    }

    // The carets, while the view has the keyboard, blinking together; the
    // primary one where the input method's cursor is in what it composes.
    if (focused && std::fmod(ui.time() - view.moved, BLINK) <= BLINK_ON) {
        const auto composed_cursor = static_cast<std::size_t>(
            std::clamp(ui.composition_cursor(), 0, static_cast<int>(composing.size())));
        for (const Selection& selection : view.selections.all()) {
            const std::optional<std::size_t> index = rows.index_of(folds, selection.caret.line);
            if (!index) continue;
            float along = layout.x_of(buffer, selection.caret);
            if (!composing.empty() && &selection == &view.selections.primary()) {
                along = layout.advance(composing, 0, composed_cursor, along);
            }
            const float x = std::round(frame.origin_x + along);
            dl.rect_filled({x - CARET_WIDTH * 0.5f, rows.y(*index) + 1.0f},
                           {x + CARET_WIDTH * 0.5f, rows.y(*index) + CODE_LINE - 1.0f}, theme::text());
        }
    }

    dl.pop_clip();
    dl.pop_clip();
}

} // namespace

CodeViewResult code_view(std::string_view label, Document& document, EditorView& view, const Keymap& keymap,
                         core::Rect rect) {
    core::Context& ui = core::current();
    const ftk::ui::Id id = ui.id(label);
    TextLayout layout(ui.text(), mono(CODE_TEXT), document.indent().width);
    layout.set_folds(&view.folds);
    Faces faces{.numbers = mono(GUTTER_TEXT), .pill = mono(PILL_TEXT)};
    faces.digit_width = ui.text().caret_x("0", faces.numbers, 1);
    faces.pill_width = ui.text().caret_x(ELLIPSIS, faces.pill, ELLIPSIS.size()) + 2.0f * PILL_PAD;
    const Buffer& buffer = document.buffer();

    // The code, and down its right side the minimap and the scrollbar.
    const float side_width = (view.minimap ? MINIMAP_WIDTH : 0.0f) + SCROLLBAR_WIDTH;
    const core::Rect code{.min = rect.min, .max = {std::max(rect.min.x, rect.max.x - side_width), rect.max.y}};
    const core::Rect side{.min = {code.max.x, rect.min.y}, .max = rect.max};

    catch_up(document, view);
    const Frame frame = place(code, view, buffer, faces.digit_width);
    layout.set_page_lines(std::max<std::size_t>(1, static_cast<std::size_t>(rect.height() / CODE_LINE) - 1));

    // Beneath what is placed after it over it: the find's plate.
    const core::Interaction in = ui.interact(id, code, {.beneath = true, .overlap = true});
    if (view.focus) {
        ui.set_focus(id);
        view.focus = false;
    }
    const core::Focus focus = ui.focusable(id, {.text = true});
    if (in.hovered && in.position.x >= frame.text.min.x) ui.set_cursor_shape(ftk::ui::MouseCursor::Text);

    const uint64_t version = document.version();
    const Selections before = view.selections;

    Clipboard clipboard{.get = [&] { return ui.clipboard(); }, .set = [&](std::string text) { ui.set_clipboard(text); }};
    auto run = [&](std::string_view command, std::string_view text = {}) {
        if (is_view_command(command)) {
            run_view_command(command, document, view);
            return;
        }
        view.folds.catch_up(document);
        CommandContext context{.document = document, .selections = view.selections, .layout = layout,
                               .clipboard = &clipboard, .folds = &view.folds, .text = text};
        (void)run_command(command, context);
    };
    // What a menu or a palette asked for, first.
    for (const std::string& command : std::exchange(view.queued, {})) run(command);

    if (focus.focused) {
        // Tab indents here rather than moving the keyboard on.
        ui.claim_key(ftk::ui::Key::Tab, id);
        // Typed text first, keys after: what came of the keys in this frame
        // follows what was typed. Text typed with Ctrl alone is a shortcut's;
        // with Ctrl and Alt it is AltGr's, as Windows reports it.
        const ftk::ui::Modifiers held = ui.modifiers();
        const bool shortcut = (held.ctrl || held.super) && !held.alt;
        if (!ui.typed().empty() && !shortcut) run("edit.type", ui.typed());
        take_keys(ui, keymap, view, run);
    }

    take_mouse(ui, in, frame, document, view, layout, faces.pill_width);

    if (in.hovered) take_wheel(ui, view);

    // A caret that came into a closed fold opens it.
    view.folds.catch_up(document);
    for (const Selection& selection : view.selections.all()) (void)view.folds.reveal(selection.caret.line);

    const bool edited = document.version() != version;
    if (edited || view.selections != before) {
        view.moved = ui.time();
        view.reveal = true;
    }
    view.seen = document.version();
    reveal(view, frame, buffer, layout);
    if (view.find.open) refresh_matches(view.find, document);
    const Frame drawn = place(code, view, buffer, faces.digit_width);
    draw(ui, document, view, drawn, layout, faces, focus.focused);

    // The kit's pieces from here are the view's: their ids are under its
    // label.
    const ftk::ui::IdScope scope(label);
    draw_side(ui, document, view, rect, side);

    if (focus.focused) {
        // The input method's window goes at the primary caret, and the
        // carets blink.
        const Position caret = view.selections.primary().caret;
        const float x = drawn.origin_x + layout.x_of(buffer, caret);
        const float y = rect.min.y + static_cast<float>(view.folds.row_of(caret.line)) * CODE_LINE - view.scroll.y;
        ui.set_text_input_area({.min = {x, y}, .max = {x + 1.0f, y + CODE_LINE}});
        const double since = ui.time() - view.moved;
        const double phase = std::fmod(since, BLINK);
        ui.request_frame_at(view.moved + std::floor(since / BLINK) * BLINK + (phase <= BLINK_ON ? BLINK_ON : BLINK));
    }

    // The find's plate over the code, after it. What is done there shows
    // from the next frame.
    if (view.find.open) {
        const Selections shown = view.selections;
        const uint64_t shown_version = document.version();
        take_find_plate(find_plate(view.find, view.selections, code), document, view, layout);
        if (!view.find.open) ui.set_focus(id);
        if (view.selections != shown || document.version() != shown_version) {
            view.moved = ui.time();
            view.reveal = true;
            ui.request_frame();
        }
        view.seen = document.version();
    }

    // Go to line's plate at the top centre. Left by Escape, it gives the
    // code the keyboard back; left by a press elsewhere, the keyboard stays
    // where that went.
    if (view.go_to_line.open) {
        const GoToLinePlate plate =
            go_to_line_plate(view.go_to_line, document, view.selections.primary().caret, code);
        if (plate.go) {
            view.selections = Selections(Selection::at(*plate.go));
            view.moved = ui.time();
            view.reveal = true;
            ui.request_frame();
        }
        if (plate.closed) {
            view.go_to_line.open = false;
            if (plate.go || ui.focused() == 0) ui.set_focus(id);
        }
    }

    ui.semantics(id, core::Role::TextField, label, {}, {.focused = focus.focused}, rect);
    return {.edited = document.version() != version, .focused = focus.focused};
}

} // namespace fjedit
