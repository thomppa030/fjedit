#include "fjedit/widget/code_view.hpp"

#include "fjedit/core/commands.hpp"
#include "fjedit/core/find.hpp"
#include "fjedit/core/structure.hpp"
#include "fjedit/widget/text_layout.hpp"

#include "ftk/ui/core/context.hpp"
#include "ftk/ui/kit/text.hpp"
#include "ftk/ui/theme.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

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

// The colour of a kind of token (sheet 39, K1).
Color colour_of(TokenKind kind) {
    switch (kind) {
    case TokenKind::Text: return theme::text();
    case TokenKind::Keyword: return theme::code_keyword();
    case TokenKind::Type: return theme::code_type();
    case TokenKind::Function: return theme::code_function();
    case TokenKind::String: return theme::code_string();
    case TokenKind::Number: return theme::code_number();
    case TokenKind::Comment: return theme::code_comment();
    case TokenKind::Meta: return theme::code_meta();
    case TokenKind::Punctuation: return theme::code_punctuation();
    case TokenKind::Member:
    case TokenKind::Uniform: return theme::code_member();
    case TokenKind::Builtin: return theme::code_builtin();
    }
    return theme::text();
}

// The kit's mono face at `size`, on the code's lines.
core::TextStyle mono(float size) {
    const ftk::ui::FontScope scope(theme::mono_font(), size);
    core::TextStyle style = core::current().text().style();
    style.line_height = CODE_LINE;
    return style;
}

// The selections brought up to date with edits made in another view.
void catch_up(const Document& document, EditorView& view) {
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

// The position under `point`, a place in the window.
Position position_under(const Frame& frame, const EditorView& view, const Buffer& buffer, const TextLayout& layout,
                        glm::vec2 point) {
    const float y = point.y - frame.rect.min.y + view.scroll.y;
    const auto line = static_cast<std::size_t>(std::max(0.0f, std::floor(y / CODE_LINE)));
    return layout.position_at(buffer, std::min(line, buffer.line_count() - 1), point.x - frame.origin_x);
}

// How fast a drag past the view's top or bottom scrolls: this share of how
// far past it the pointer is, each frame.
constexpr float DRAG_SCROLL = 0.25f;

// Line `line` whole, through its break.
Span whole_line(const Buffer& buffer, std::size_t line) {
    const Position end = line + 1 < buffer.line_count() ? Position{line + 1, 0} : Position{line, buffer.line(line).size()};
    return {.from = {line, 0}, .to = end};
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
// line (three times, or on the line numbers); with Shift it takes the primary
// selection there; with Alt it begins a column, or adds a caret if it doesn't
// drag. Dragging goes on in the same unit, and past the top or bottom it
// scrolls.
void take_mouse(core::Context& ui, const core::Interaction& in, const Frame& frame, Document& document,
                EditorView& view, const TextLayout& layout) {
    using By = EditorView::Drag::By;
    const Buffer& buffer = document.buffer();
    EditorView::Drag& drag = view.drag;
    const glm::vec2 point = in.position;
    const ftk::ui::Modifiers held = ui.modifiers();

    if (in.pressed && in.button == ftk::ui::MouseButton::Left) {
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
            drag.origin = whole_line(buffer, at.line);
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
        case By::Line: view.selections = Selections(by_units(drag.origin, whole_line(buffer, at.line))); break;
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

// The keys pressed this frame, through the keymap: a whole binding runs its
// command, the start of a longer one waits for the next key.
template <typename Run>
void take_keys(core::Context& ui, const Keymap& keymap, EditorView& view, Run&& run) {
    const ftk::ui::Modifiers held = ui.modifiers();
    for (std::size_t k = 0; k < ftk::ui::KEY_COUNT; ++k) {
        const auto key = static_cast<ftk::ui::Key>(k);
        if (!ui.key_repeated(key)) continue;
        view.pending.push_back({.key = key, .modifiers = held});
        const KeyMatch match = keymap.find(view.mode, view.pending);
        if (match.command) {
            const std::string command(*match.command);
            view.pending.clear();
            run(command);
        } else if (!match.waiting) {
            view.pending.clear();
        }
    }
}

// Scrolls so the primary caret is in sight, then keeps the scroll inside
// the text.
void reveal(EditorView& view, const Frame& frame, const Buffer& buffer, const TextLayout& layout) {
    if (view.reveal) {
        const Position caret = view.selections.primary().caret;
        const float top = static_cast<float>(caret.line) * CODE_LINE;
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
    const float bottom = static_cast<float>(buffer.line_count() - 1) * CODE_LINE;
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

// The lines in sight: from `first` up to, not including, `last`, the first
// drawn at `top`.
struct Shown {
    std::size_t first{0};
    std::size_t last{0};
    float top{0.0f};
};

Shown shown(const Frame& frame, const EditorView& view, const Buffer& buffer) {
    const auto first = static_cast<std::size_t>(view.scroll.y / CODE_LINE);
    const auto rows = static_cast<std::size_t>(std::ceil(frame.rect.height() / CODE_LINE)) + 1;
    return {.first = std::min(first, buffer.line_count()),
            .last = std::min(first + rows, buffer.line_count()),
            .top = frame.rect.min.y - (view.scroll.y - static_cast<float>(first) * CODE_LINE)};
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

// Indent guides: one at the start of each level of every line's indent; the
// guide of the block the primary caret is in brighter.
void draw_guides(core::DrawList& dl, const Document& document, const EditorView& view, const Frame& frame,
                 const TextLayout& layout, std::size_t first, std::size_t last, float top) {
    const Buffer& buffer = document.buffer();
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

    for (std::size_t line = first; line < last; ++line) {
        const float y = top + static_cast<float>(line - first) * CODE_LINE;
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
                const TextLayout& layout, std::size_t first, std::size_t last, float top) {
    const Buffer& buffer = document.buffer();
    const Selection& primary = view.selections.primary();
    auto box = [&](Position from, Position to) {
        const float y = top + static_cast<float>(from.line - first) * CODE_LINE;
        return core::Rect{.min = {frame.origin_x + layout.x_of(buffer, from), y},
                          .max = {frame.origin_x + layout.x_of(buffer, to), y + CODE_LINE}};
    };

    if (primary.empty()) {
        const Span word = word_at(buffer, primary.caret);
        if (!word.empty()) {
            const FindOptions whole{.match_case = true, .whole_word = true};
            for (const Span& use : find_in_lines(buffer, buffer.text(word), first, last, whole)) {
                if (use == word || use.from.line != use.to.line) continue;
                const core::Rect r = box(use.from, use.to);
                dl.rect_filled(r.min, r.max, theme::code_occurrence(), DECORATION_ROUNDING);
            }
        }
    }

    if (const std::optional<BracketPair> pair = matching_bracket(buffer, document.syntax(), primary.caret)) {
        for (const Position at : {pair->open, pair->close}) {
            if (at.line < first || at.line >= last) continue;
            const core::Rect r = box(at, {at.line, at.byte + 1});
            dl.rect(r.min + glm::vec2{0.0f, 1.0f}, r.max - glm::vec2{0.0f, 1.0f}, theme::code_bracket(),
                    DECORATION_ROUNDING);
        }
    }
}

void draw(core::Context& ui, const Document& document, const EditorView& view, const Frame& frame,
          const TextLayout& layout, core::TextStyle numbers, bool focused) {
    const Buffer& buffer = document.buffer();
    core::DrawList& dl = ui.draw_list();
    const Shown lines = shown(frame, view, buffer);
    auto row_top = [&](std::size_t line) { return lines.top + static_cast<float>(line - lines.first) * CODE_LINE; };
    auto caret_on = [&](std::size_t line) {
        return std::any_of(view.selections.all().begin(), view.selections.all().end(),
                           [&](const Selection& s) { return s.caret.line == line; });
    };

    dl.rect_filled(frame.rect.min, frame.rect.max, theme::surface_base());
    dl.push_clip(frame.rect.min, frame.rect.max);

    // The caret's line, a shade lighter across the gutter and the text,
    // while it selects nothing.
    for (const Selection& selection : view.selections.all()) {
        const std::size_t line = selection.caret.line;
        if (!selection.empty() || line < lines.first || line >= lines.last) continue;
        dl.rect_filled({frame.rect.min.x, row_top(line)}, {frame.rect.max.x, row_top(line) + CODE_LINE},
                       theme::code_current_line());
    }

    // Line numbers, right-aligned; the caret's line's brighter.
    for (std::size_t line = lines.first; line < lines.last; ++line) {
        const std::string number = std::to_string(line + 1);
        const float width = ui.text().caret_x(number, numbers, number.size());
        const Color colour = caret_on(line) ? theme::text_secondary() : theme::text_disabled();
        ui.text().draw(dl, {frame.numbers_right - width, row_top(line)}, colour, number, numbers);
    }

    dl.push_clip(frame.text.min, frame.text.max);

    draw_guides(dl, document, view, frame, layout, lines.first, lines.last, lines.top);
    draw_marks(dl, document, view, frame, layout, lines.first, lines.last, lines.top);

    // Selections, the line break taking a space's width.
    for (const Selection& selection : view.selections.all()) {
        if (selection.empty()) continue;
        const Span span = selection.span();
        for (std::size_t line = std::max(span.from.line, lines.first); line <= span.to.line && line < lines.last;
             ++line) {
            const float from = line == span.from.line ? layout.x_of(buffer, span.from) : 0.0f;
            const float to = line == span.to.line ? layout.x_of(buffer, span.to)
                                                  : layout.x_of(buffer, {line, buffer.line(line).size()}) +
                                                        layout.space_width();
            dl.rect_filled({frame.origin_x + from, row_top(line)}, {frame.origin_x + to, row_top(line) + CODE_LINE},
                           theme::selection());
        }
    }

    // The text, coloured by its tokens.
    for (std::size_t line = lines.first; line < lines.last; ++line) {
        const std::string_view text = buffer.line(line);
        const float y = row_top(line);
        float x = 0.0f;
        std::size_t at = 0;
        for (const Token& token : document.syntax().tokens(line)) {
            x = draw_run(ui, layout, text, at, token.begin, x, frame.origin_x, y, theme::text());
            x = draw_run(ui, layout, text, token.begin, token.end, x, frame.origin_x, y, colour_of(token.kind));
            at = token.end;
        }
        (void)draw_run(ui, layout, text, at, text.size(), x, frame.origin_x, y, theme::text());
    }

    // The carets, while the view has the keyboard, blinking together.
    if (focused && std::fmod(ui.time() - view.moved, BLINK) <= BLINK_ON) {
        for (const Selection& selection : view.selections.all()) {
            const std::size_t line = selection.caret.line;
            if (line < lines.first || line >= lines.last) continue;
            const float x = std::round(frame.origin_x + layout.x_of(buffer, selection.caret));
            dl.rect_filled({x - CARET_WIDTH * 0.5f, row_top(line) + 1.0f},
                           {x + CARET_WIDTH * 0.5f, row_top(line) + CODE_LINE - 1.0f}, theme::text());
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
    const core::TextStyle code = mono(CODE_TEXT);
    const core::TextStyle numbers = mono(GUTTER_TEXT);
    TextLayout layout(ui.text(), code, document.indent().width);
    const Buffer& buffer = document.buffer();

    catch_up(document, view);
    const Frame frame = place(rect, view, buffer, ui.text().caret_x("0", numbers, 1));
    layout.set_page_lines(std::max<std::size_t>(1, static_cast<std::size_t>(rect.height() / CODE_LINE) - 1));

    const core::Interaction in = ui.interact(id, rect);
    const core::Focus focus = ui.focusable(id, {.text = true});
    if (in.hovered && in.position.x >= frame.text.min.x) ui.set_cursor_shape(ftk::ui::MouseCursor::Text);

    const uint64_t version = document.version();
    const Selections before = view.selections;

    if (focus.focused) {
        // Tab indents here rather than moving the keyboard on.
        ui.claim_key(ftk::ui::Key::Tab, id);
        Clipboard clipboard{.get = [&] { return ui.clipboard(); }, .set = [&](std::string text) { ui.set_clipboard(text); }};
        auto run = [&](std::string_view command, std::string_view text = {}) {
            CommandContext context{.document = document, .selections = view.selections, .layout = layout,
                                   .clipboard = &clipboard, .text = text};
            (void)run_command(command, context);
        };
        // Typed text first, keys after: what came of the keys in this frame
        // follows what was typed. Text typed with Ctrl alone is a shortcut's;
        // with Ctrl and Alt it is AltGr's, as Windows reports it.
        const ftk::ui::Modifiers held = ui.modifiers();
        const bool shortcut = (held.ctrl || held.super) && !held.alt;
        if (!ui.typed().empty() && !shortcut) run("edit.type", ui.typed());
        take_keys(ui, keymap, view, run);
    }

    take_mouse(ui, in, frame, document, view, layout);

    // The wheel scrolls: down the text, or along it with Shift. A notch up
    // or left is positive; the scroll grows down and right.
    if (in.hovered) {
        if (const core::Pointer* mouse = ui.find_pointer(core::PointerType::Mouse, 0)) {
            const float step = ui.pointer_style().wheel_step;
            const bool along = ui.modifiers().shift;
            view.scroll.x += (along ? -mouse->wheel.y : mouse->wheel.x) * step;
            view.scroll.y += (along ? 0.0f : -mouse->wheel.y) * step;
        }
    }

    const bool edited = document.version() != version;
    if (edited || view.selections != before) {
        view.moved = ui.time();
        view.reveal = true;
    }
    view.seen = document.version();
    reveal(view, frame, buffer, layout);
    const Frame drawn = place(rect, view, buffer, ui.text().caret_x("0", numbers, 1));
    draw(ui, document, view, drawn, layout, numbers, focus.focused);

    if (focus.focused) {
        // The input method's window goes at the primary caret, and the
        // carets blink.
        const Position caret = view.selections.primary().caret;
        const float x = drawn.origin_x + layout.x_of(buffer, caret);
        const float y = rect.min.y + static_cast<float>(caret.line) * CODE_LINE - view.scroll.y;
        ui.set_text_input_area({.min = {x, y}, .max = {x + 1.0f, y + CODE_LINE}});
        const double since = ui.time() - view.moved;
        const double phase = std::fmod(since, BLINK);
        ui.request_frame_at(view.moved + std::floor(since / BLINK) * BLINK + (phase <= BLINK_ON ? BLINK_ON : BLINK));
    }
    ui.semantics(id, core::Role::TextField, label, {}, {.focused = focus.focused}, rect);
    return {.edited = edited, .focused = focus.focused};
}

} // namespace fjedit
