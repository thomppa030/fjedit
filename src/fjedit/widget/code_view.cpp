#include "fjedit/widget/code_view.hpp"

#include "fjedit/core/commands.hpp"
#include "fjedit/core/find.hpp"
#include "fjedit/core/motion.hpp"
#include "fjedit/core/structure.hpp"
#include "fjedit/core/vim/notation.hpp"
#include "fjedit/widget/code_colours.hpp"
#include "fjedit/widget/completion.hpp"
#include "fjedit/widget/minimap.hpp"
#include "fjedit/widget/next_keys.hpp"
#include "fjedit/widget/text_layout.hpp"

#include "ftk/base/utf8.hpp"
#include "ftk/ui/core/context.hpp"
#include "ftk/ui/kit/icons.hpp"
#include "ftk/ui/kit/layout.hpp"
#include "ftk/ui/kit/plate.hpp"
#include "ftk/ui/kit/scope.hpp"
#include "ftk/ui/kit/scrollbar.hpp"
#include "ftk/ui/kit/text.hpp"
#include "ftk/ui/theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <map>
#include <optional>
#include <span>
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
// A problem's message after its line (sheet 39, G1): in the interface's
// face at 12.5 beside code at 13, three spaces past the code, its icon
// first. Its code is underlined with a wave a pixel and a half high either
// way, its turns two apart, three above the row's foot.
constexpr float MESSAGE_TEXT = 12.5f;
constexpr float MESSAGE_SPACES = 3.0f;
constexpr float MESSAGE_ICON_GAP = 5.0f;
constexpr float WAVE_HEIGHT = 1.5f;
constexpr float WAVE_STEP = 2.0f;
constexpr float WAVE_RISE = 3.0f;
// The hover card (sheet 39, H): asked for once the mouse has rested this
// long on a word; padded 12 by 10, as wide as it needs between 160 and
// 480, and its edge 4 left of the word's, as is completion's.
constexpr double HOVER_DELAY = 0.5;
constexpr glm::vec2 CARD_PAD{12.0f, 10.0f};
constexpr float CARD_NARROWEST = 160.0f;
constexpr float CARD_WIDEST = 480.0f;
constexpr float CARD_SHIFT = 4.0f;

// The kit's mono face at `size`, on lines `line` apart.
core::TextStyle face(ftk::ui::Font font, float size, float line) {
    const ftk::ui::FontScope scope(font, size);
    core::TextStyle style = core::current().text().style();
    style.line_height = line;
    return style;
}

core::TextStyle mono(float size, float line) {
    return face(theme::mono_font(), size, line);
}

// What sheet 39's measures come to for code at `text_size`: they scale
// together, the lines and the line numbers with the code.
struct Sizes {
    float text;
    float line;
    float gutter;
    float pill;
    float pill_height;
    float message;

    explicit Sizes(float text_size)
        : text(text_size), line(code_line(text_size)),
          gutter(text_size * GUTTER_TEXT / CODE_TEXT), pill(text_size * PILL_TEXT / CODE_TEXT),
          pill_height(std::round(text_size * PILL_HEIGHT / CODE_TEXT)), message(text_size * MESSAGE_TEXT / CODE_TEXT) {}
};

// The faces the view draws in besides the code's, and what it measures by
// them.
struct Faces {
    core::TextStyle numbers;
    core::TextStyle pill;
    core::TextStyle message;
    float digit_width{0.0f};
    float pill_width{0.0f};
    float pill_height{0.0f};
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
    // How far apart the lines are.
    float line{CODE_LINE};
    // Right of the line numbers.
    float numbers_right{0.0f};
    // Where the text is drawn, and where a line's start is, scrolled.
    core::Rect text;
    float origin_x{0.0f};
};

Frame place(core::Rect rect, const EditorView& view, const Buffer& buffer, float digit_width, float line) {
    std::size_t digits = 1;
    for (std::size_t n = buffer.line_count(); n >= 10; n /= 10) ++digits;
    Frame frame;
    frame.rect = rect;
    frame.line = line;
    frame.numbers_right = rect.min.x + GUTTER_PAD + static_cast<float>(std::max<std::size_t>(digits, 2)) * digit_width;
    frame.text = {.min = {frame.numbers_right + FOLD_COLUMN, rect.min.y}, .max = rect.max};
    frame.origin_x = frame.text.min.x + TEXT_PAD - view.scroll.x;
    return frame;
}

// The line the row at `y`, a height in the window, begins with.
std::size_t line_under(const Frame& frame, const EditorView& view, const Buffer& buffer, float y) {
    const float down = y - frame.rect.min.y + view.scroll.y;
    const auto row = static_cast<std::size_t>(std::max(0.0f, std::floor(down / frame.line)));
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
        // Ctrl and a click on the code: where what is named there is
        // defined, the caret left where it is.
        if (held.ctrl && !held.alt && view.service != nullptr && point.x >= frame.text.min.x) {
            view.going = view.service->definition(view.path, at);
            return;
        }
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
// The word the mouse rests on, asked of the language service once it has
// rested there long enough, and read into a card when answered. The card
// goes when the mouse leaves both the word and the card, and when
// `changed`: the text, the carets or the scroll moved under it.
void take_hover(core::Context& ui, const core::Interaction& in, const Frame& frame, const Document& document,
                EditorView& view, const TextLayout& layout, bool changed) {
    EditorView::Hovering& hovering = view.hovering;
    if (view.service == nullptr || changed) {
        hovering = {};
        return;
    }
    if (hovering.by_keys) {
        if (hovering.asked && hovering.asked->answered && hovering.card.empty() && hovering.asked->answer) {
            hovering.card = read_markdown(hovering.asked->answer->markdown);
        }
        return;
    }
    const core::Pointer* mouse = ui.find_pointer(core::PointerType::Mouse, 0);
    const bool resting = mouse != nullptr && mouse->present && mouse->buttons == 0;
    const glm::vec2 point = resting ? mouse->now.position : glm::vec2{0.0f};
    if (resting && hovering.card_rect && hovering.card_rect->contains(point)) return;

    std::optional<Span> word;
    Position at;
    if (resting && in.hovered && point.x >= frame.text.min.x && !view.drag.active) {
        const Buffer& buffer = document.buffer();
        at = position_under(frame, view, buffer, layout, point);
        const Span found = word_or_place(buffer, at);
        if (!found.empty() && found.from.line == found.to.line) {
            const float left = frame.origin_x + layout.x_of(buffer, found.from);
            const float right = frame.origin_x + layout.x_of(buffer, found.to);
            if (point.x >= left && point.x <= right) word = found;
        }
    }
    if (word != hovering.word) {
        hovering = {.word = word, .at = at, .since = ui.time()};
        if (word) ui.request_frame_at(ui.time() + HOVER_DELAY);
        return;
    }
    if (!word) return;
    if (!hovering.asked && ui.time() - hovering.since >= HOVER_DELAY) {
        hovering.asked = view.service->hover(view.path, hovering.at);
    }
    if (hovering.asked && hovering.asked->answered && hovering.card.empty() && hovering.asked->answer) {
        hovering.card = read_markdown(hovering.asked->answer->markdown);
    }
}

KeyMatch match_keys(const Keymap& keymap, const EditorView& view) {
    KeyMatch match = keymap.find(view.mode, view.pending);
    if (view.completing.open) {
        const KeyMatch completing = keymap.find(Keymap::COMPLETING, view.pending);
        if (completing.command) return completing;
        match.waiting = match.waiting || completing.waiting;
    }
    if (view.find.open) {
        const KeyMatch finding = keymap.find(Keymap::FINDING, view.pending);
        if (finding.command) return finding;
        match.waiting = match.waiting || finding.waiting;
    }
    return match;
}

// The keys pressed and the text typed this frame, in the order they came.
// Text goes to `type`, unless it was typed with Ctrl alone, which makes it a
// shortcut's (with Ctrl and Alt it is AltGr's, as Windows reports it). Keys
// go through the keymap: a whole binding runs its command, the start of a
// longer one waits for the next key. A key `ends` takes first (one ending a
// rename typed in place) goes nowhere else.
template <typename Run, typename Type, typename Ends>
void take_keys(core::Context& ui, const Keymap& keymap, EditorView& view, Run&& run, Type&& type, Ends&& ends) {
    for (const core::Keystroke& stroke : ui.keystrokes()) {
        const ftk::ui::Modifiers held = stroke.modifiers;
        if (stroke.kind == core::Keystroke::Kind::Text) {
            const bool shortcut = (held.ctrl || held.super) && !held.alt;
            if (!shortcut) type(stroke.text);
            continue;
        }
        if (view.pending.empty() && ends(Chord{.key = stroke.key, .modifiers = held, .text = {}})) continue;
        view.pending.push_back({.key = stroke.key, .modifiers = held});
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

// Whether `key` types a character: a letter, a digit, Space or punctuation.
bool types_character(ftk::ui::Key key) {
    using ftk::ui::Key;
    return (key >= Key::A && key <= Key::Digit9) || key == Key::Space || key >= Key::Minus;
}

// Text typed with Ctrl alone is a shortcut's; with Ctrl and Alt it is
// AltGr's, as Windows reports it.
bool shortcut_text(const core::Keystroke& stroke) {
    return stroke.kind == core::Keystroke::Kind::Text && (stroke.modifiers.ctrl || stroke.modifiers.super) &&
           !stroke.modifiers.alt;
}

// The frame's keystrokes as Vim reads them (decision 2 of the Vim plan):
// text as the characters it types, one at a time; a key as itself when it
// types nothing or is held with Ctrl, Alt or Super, unless text came of it,
// which stands for it. So `$`, `{` and `/` are whatever keys the layout puts
// them on.
std::vector<Chord> vim_keys(std::span<const core::Keystroke> strokes) {
    std::vector<Chord> keys;
    for (std::size_t i = 0; i < strokes.size(); ++i) {
        const core::Keystroke& stroke = strokes[i];
        if (stroke.kind == core::Keystroke::Kind::Text) {
            if (shortcut_text(stroke)) continue;
            const std::string_view text = stroke.text;
            for (std::size_t at = 0; at < text.size();) {
                std::size_t end = at;
                (void)ftk::utf8::next(text, end);
                keys.push_back(Chord::typed(text.substr(at, end - at)));
                at = end;
            }
            continue;
        }
        const ftk::ui::Modifiers held = stroke.modifiers;
        if (types_character(stroke.key)) {
            const bool texted = i + 1 < strokes.size() && strokes[i + 1].kind == core::Keystroke::Kind::Text &&
                                !shortcut_text(strokes[i + 1]);
            if (texted || !(held.ctrl || held.alt || held.super)) continue;
        }
        keys.push_back({.key = stroke.key, .modifiers = held});
    }
    return keys;
}

// How long keys wait for the rest of a map or a binding before the plate
// of what can come next shows (sheet 47, S2).
constexpr double NEXT_KEYS_DELAY = 0.5;

// The frame's keys with Vim on (decision 7 of the Vim plan): through
// keys.vim's maps first, each in the mode it is typed in; then those the
// keymap binds in Vim's mode (the completion list's first while it is
// open) run their commands, and the rest go to `take`, for Vim, in the
// order they came, with any that waited. Keys that begin a longer map or a
// longer binding wait for the rest, timeoutlen at most, then go as they
// stand. A character a command waits for (an `f`'s) is never a map's or a
// binding's. The keys typed are kept for a macro being recorded; a macro
// played goes into the maps' typeahead, a frame's worth at a time, Ctrl-C
// stopping it; a command that failed lets go of what a macro or a map had
// yet to play. `acting` makes what Vim acts through; `command` runs a map's
// <Cmd> line.
template <typename Acting, typename Run, typename Take, typename Command>
void take_vim_keys(core::Context& ui, const Keymap& keymap, EditorView& view, Acting&& acting, Run&& run, Take&& take,
                   Command&& command) {
    const double now = ui.time();
    const double wait = static_cast<double>(view.vim->settings().timeout_ms) / 1000.0;
    // A key to Vim. A macro it plays goes in ahead of the keys yet to be
    // played; a command that failed lets go of what was yet to be played.
    const vim::MapPlayer* playing = nullptr;
    const auto to_vim = [&](const Chord& key) {
        take(key);
        const std::vector<Chord> played = view.vim->take_played();
        if (view.vim->failed()) view.maps.drop_played();
        if (!played.empty() && playing != nullptr) view.maps.play_ahead(played, now, *playing);
    };
    // The binding the keys pending make: run, unless a longer one begins
    // with them and the wait isn't over; no binding, they go to Vim.
    const auto bound = [&](bool timed_out) {
        KeyMatch match = keymap.find(vim::keymap_mode(view.vim->mode()), view.pending);
        if (view.completing.open) {
            const KeyMatch completing = keymap.find(Keymap::VIM_COMPLETING, view.pending);
            if (completing.command) match = completing;
            match.waiting = match.waiting || completing.waiting;
            match.longer = completing.command ? completing.longer : match.longer;
        }
        if ((match.waiting || match.longer) && !timed_out) return;
        if (match.command) {
            const std::string name(*match.command);
            view.pending.clear();
            CommandContext context = acting();
            view.vim->cancel(context);
            run(name);
            return;
        }
        for (const Chord& waited : std::exchange(view.pending, {})) to_vim(waited);
    };
    const vim::MapPlayer player{
        .mode = [&] { return view.vim->map_mode(); },
        .wants_character = [&] { return view.pending.empty() && view.vim->wants_character(); },
        .key =
            [&](const Chord& key) {
                if (view.pending.empty() && view.vim->wants_character()) return to_vim(key);
                view.pending.push_back(key);
                view.pending_since = now;
                bound(false);
            },
        .command =
            [&](std::string_view line) {
                command(line);
                if (view.vim->failed()) view.maps.drop_played();
            },
        .error = [&](std::string_view error) { view.vim->say({.text = std::string(error), .error = true}); },
        .typed = [&](std::span<const Chord> keys) { view.vim->record(keys); },
    };
    playing = &player;
    view.maps.wait_for(wait);
    // What a long macro left for this frame plays first.
    if (view.maps.more()) view.maps.play_on(now, player);
    for (const Chord& key : vim_keys(ui.keystrokes())) {
        // Ctrl-C stops a macro still playing, as in Vim.
        if (view.maps.more() && key.text.empty() && key.key == ftk::ui::Key::C &&
            key.modifiers == ftk::ui::Modifiers{.ctrl = true}) {
            view.maps.drop_played();
            continue;
        }
        // Over the keys that can come next, Esc lets those waiting go.
        if (view.showing_next_keys && key.text.empty() && key.key == ftk::ui::Key::Escape &&
            key.modifiers == ftk::ui::Modifiers{}) {
            view.maps.drop();
            view.pending.clear();
            view.showing_next_keys = false;
            continue;
        }
        view.maps.take(key, now, player);
    }
    if (view.maps.more()) ui.request_frame_at(now);
    // Keys that waited half a second show what can come next, and wait from
    // then on for the next key, as which-key holds them: in normal, visual
    // and operator-pending modes, where which-key shows. Typing in insert
    // mode or on the command line keeps to timeoutlen.
    const vim::MapModes mode = view.vim->map_mode();
    const bool plate_mode = vim::any_of(mode, vim::MapModes::Normal | vim::MapModes::Visual |
                                                  vim::MapModes::OperatorPending);
    const bool waiting = plate_mode && (!view.maps.waiting().empty() || !view.pending.empty());
    if (!waiting) {
        view.waiting_since.reset();
        view.showing_next_keys = false;
    } else if (!view.waiting_since) {
        view.waiting_since = now;
    }
    if (waiting && now >= *view.waiting_since + NEXT_KEYS_DELAY) view.showing_next_keys = true;
    if (waiting && !view.showing_next_keys) ui.request_frame_at(*view.waiting_since + NEXT_KEYS_DELAY);
    if (view.showing_next_keys) return;
    // Otherwise keys that waited as long as they may go as they stand: the
    // maps' first, then the keymap's. A frame comes when the wait runs out.
    if (view.maps.deadline() && now >= *view.maps.deadline()) view.maps.time_out(player);
    if (!view.pending.empty() && now >= view.pending_since + wait) bound(true);
    if (view.maps.deadline()) ui.request_frame_at(*view.maps.deadline());
    if (!view.pending.empty()) ui.request_frame_at(view.pending_since + wait);
}

// What a map does, as the plate of next keys says it: its keys in Vim's
// notation, its `<Cmd>` lines after a `:`.
std::string map_written(const vim::Mapping& map) {
    std::string out;
    for (const vim::MapStep& step : map.does) {
        if (!out.empty()) out += ' ';
        if (step.is_command) {
            out += ":" + step.command;
        } else {
            for (const Chord& key : step.keys) out += vim::notation(key);
        }
    }
    return out.empty() ? std::string("nothing") : out;
}

// The title of the command called `name`: the editor's, the view's, or what
// the one drawing the view calls it; its name when none does.
std::string command_title(const EditorView& view, std::string_view name) {
    if (const Command* command = find_command(name)) return std::string(command->title);
    for (const ViewCommand& command : view_commands()) {
        if (command.name == name) return std::string(command.title);
    }
    if (view.command_title) {
        if (std::string title = view.command_title(name); !title.empty()) return title;
    }
    return std::string(name);
}

// The keys that can come after those waiting, with what each runs: the
// maps' while keys wait for a longer map, else the keymap's bindings'.
// A key that begins more says the one they lead to, or how many.
std::vector<NextKey> next_keys_of(const EditorView& view, const Keymap& keymap) {
    struct Gathered {
        Chord key;
        std::optional<std::string> runs;
        std::vector<std::string> longer;
    };
    std::vector<Gathered> gathered;
    const auto gather = [&](const Chord& key) -> Gathered& {
        const auto found = std::ranges::find(gathered, key, &Gathered::key);
        if (found != gathered.end()) return *found;
        gathered.push_back({.key = key, .runs = {}, .longer = {}});
        return gathered.back();
    };
    const std::vector<Chord> held = view.maps.waiting();
    if (!held.empty()) {
        for (const vim::Mapping* map : view.maps.longer(view.vim->map_mode(), held)) {
            Gathered& next = gather(map->keys[held.size()]);
            if (map->keys.size() == held.size() + 1) {
                next.runs = map_written(*map);
            } else {
                next.longer.push_back(map_written(*map));
            }
        }
    } else {
        for (const KeyNext& next : keymap.next_keys(vim::keymap_mode(view.vim->mode()), view.pending)) {
            Gathered& key = gather(next.key);
            if (next.command) key.runs = command_title(view, *next.command);
            for (const std::string_view longer : next.longer) key.longer.push_back(command_title(view, longer));
        }
    }
    std::vector<NextKey> out;
    for (const Gathered& next : gathered) {
        const bool group = !next.longer.empty();
        std::string title = next.runs.value_or(std::string());
        if (group && !next.runs) {
            title = next.longer.size() == 1 ? next.longer.front() : std::format("{} more", next.longer.size());
        }
        out.push_back({.key = chords_text(std::span(&next.key, 1)), .title = std::move(title), .group = group});
    }
    return out;
}

// A rename asked for at the caret (sheet 48): the service asked where the
// name there stands. Nothing without a service or a name.
void start_rename(EditorView& view, const Document& document) {
    if (view.service == nullptr || view.renaming) return;
    const Position caret = view.selections.primary().caret;
    const Span word = word_at(document.buffer(), caret);
    if (word.empty()) return;
    view.renaming = EditorView::Renaming{.name = document.buffer().text(word),
                                         .at = word.from,
                                         .before = view.selections,
                                         .steps = 0,
                                         .asked = view.service->occurrences(view.path, caret),
                                         .asked_at = document.version(),
                                         .uses = {},
                                         .primary = 0,
                                         .version = 0,
                                         .begun = false};
}

// The uses of a rename typed in place, carried through what was typed
// since: each takes in what is typed at its ends.
void follow_uses(EditorView::Renaming& renaming, const Document& document) {
    if (!renaming.begun || renaming.version == document.version()) return;
    if (const auto since = document.edits_since(renaming.version)) {
        for (Span& use : renaming.uses) {
            for (const Edit& made : *since) {
                use = {.from = map(made, use.from, Bias::Before), .to = map(made, use.to, Bias::After)};
            }
        }
    }
    renaming.version = document.version();
}

// A rename begun once the service says where the name stands: its uses
// selected, to be typed over together as one step held open, with Vim in
// insert mode; only the name at the caret where the service names none.
// Given up when the name is gone from there.
void begin_rename(EditorView& view, Document& document) {
    EditorView::Renaming& renaming = *view.renaming;
    const Buffer& buffer = document.buffer();
    const auto since = document.edits_since(renaming.asked_at);
    const auto carried = [&](Span span) {
        if (since) {
            for (const Edit& made : *since) {
                span = {.from = map(made, span.from, Bias::After), .to = map(made, span.to, Bias::Before)};
            }
        }
        return Span{.from = buffer.clamp(span.from), .to = buffer.clamp(span.to)};
    };
    const Span asked = carried({.from = renaming.at, .to = {renaming.at.line, renaming.at.byte + renaming.name.size()}});
    if (buffer.text(asked) != renaming.name) {
        view.renaming.reset();
        return;
    }
    std::vector<Span> uses{asked};
    if (renaming.asked && renaming.asked->answered) {
        for (const Span& answered : renaming.asked->answer) {
            const Span use = carried(answered);
            if (use.from.line == use.to.line && buffer.text(use) == renaming.name) uses.push_back(use);
        }
    }
    std::ranges::sort(uses, {}, &Span::from);
    const auto [first, last] = std::ranges::unique(uses);
    uses.erase(first, last);
    const auto primary = static_cast<std::size_t>(std::ranges::find(uses, asked) - uses.begin());

    document.history().close();
    renaming.steps = document.history().done();
    document.history().hold();
    std::vector<Selection> selected;
    for (const Span& use : uses) selected.push_back({.anchor = use.from, .caret = use.to, .goal = {}});
    view.selections.set(std::move(selected), primary);
    if (view.vim) view.vim->begin_insert();
    renaming.uses = std::move(uses);
    renaming.primary = primary;
    renaming.version = document.version();
    renaming.begun = true;
}

// The rename typed in place ended: its typing taken back and forgotten, the
// selections as before it (with `keep`, where they are, carried back with
// the text: a caret the mouse put elsewhere), Vim back in normal mode through
// `context`. With `make`, the name typed, when it is new, for the service to
// rename to.
std::optional<Rename> end_rename(EditorView& view, Document& document, CommandContext& context, bool make,
                                 bool keep = false) {
    EditorView::Renaming renaming = std::move(*view.renaming);
    view.renaming.reset();
    if (!renaming.begun) return std::nullopt;
    follow_uses(renaming, document);
    const std::string typed = document.buffer().text(renaming.uses[renaming.primary]);
    const Selections now = view.selections;
    const uint64_t taking = document.version();
    document.history().close();
    (void)document.take_back(renaming.steps);
    view.selections = renaming.before;
    if (const auto since = document.edits_since(taking); keep && since) {
        std::vector<Selection> carried;
        for (Selection selection : now.all()) {
            for (const Edit& made : *since) {
                selection.anchor = map(made, selection.anchor, Bias::Before);
                selection.caret = map(made, selection.caret, Bias::Before);
            }
            carried.push_back(selection);
        }
        view.selections.set(std::move(carried), now.primary_index());
    }
    if (view.vim) view.vim->end_insert(context);
    if (!make || typed.empty() || typed == renaming.name) return std::nullopt;
    return Rename{.at = renaming.at, .from = renaming.name, .to = typed};
}

// Whether `key` is Enter or Esc, held with nothing.
bool enter_or_escape(const Chord& key) {
    return key.text.empty() && key.modifiers == ftk::ui::Modifiers{} &&
           (key.key == ftk::ui::Key::Enter || key.key == ftk::ui::Key::Escape);
}

// Selects `match`, as the find goes to it.
void select_match(EditorView& view, Span match) {
    view.selections = Selections({.anchor = match.from, .caret = match.to});
}

// Runs a command the view does itself: opens, steps through or closes the
// find, or opens go to line.
void run_view_command(std::string_view name, const Document& document, EditorView& view) {
    FindState& find = view.find;
    if (name == "completion.open") {
        if (view.service != nullptr) {
            open_completion(view.completing, *view.service, view.path, document, view.selections.primary().caret, {});
        }
    } else if (name == "completion.close") {
        view.completing = {};
    } else if (name == "definition.go") {
        if (view.service != nullptr) view.going = view.service->definition(view.path, view.selections.primary().caret);
    } else if (name == "hover.show") {
        // What the service says the word at the caret is, at once.
        if (view.service != nullptr) {
            const Position caret = view.selections.primary().caret;
            view.hovering = {.word = word_or_place(document.buffer(), caret),
                             .at = caret,
                             .since = 0.0,
                             .asked = view.service->hover(view.path, caret),
                             .card = {},
                             .card_rect = {},
                             .by_keys = true};
        }
    } else if (name == "format.document") {
        if (view.service != nullptr) view.formatting = view.service->format(view.path);
    } else if (name == "completion.next" || name == "completion.previous") {
        move_completion(view.completing, name == "completion.next" ? 1 : -1);
    } else if (name == "completion.page_down" || name == "completion.page_up") {
        const auto page = static_cast<std::ptrdiff_t>(COMPLETION_ROWS) - 1;
        move_completion(view.completing, name == "completion.page_down" ? page : -page);
    } else if (name == "goto.open") {
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
// Vim's search shown through the view's find, `pattern` its matches or
// none; the find's own plate, while open, keeps what it looks for.
void show_search(FindState& find, const vim::Pattern* pattern) {
    if (find.open) return;
    find.shown = pattern != nullptr;
    if (pattern == nullptr) return;
    const ftk::ui::FindToggles toggles{.match_case = pattern->options.match_case,
                                       .whole_word = pattern->options.whole_word,
                                       .regex = pattern->options.regex};
    if (find.query == pattern->regex && find.toggles == toggles && find.group == pattern->options.group) return;
    find.query = pattern->regex;
    find.toggles = toggles;
    find.group = pattern->options.group;
    find.stale = true;
}

void take_find_plate(const FindPlate& plate, Document& document, EditorView& view, const TextLayout& layout) {
    FindState& find = view.find;
    // A search on the plate takes all a match is, and is the one Vim's n
    // and N go on with.
    if (plate.searched) {
        find.group = 0;
        find.stale = true;
        if (view.vim) {
            view.vim->search_for({.text = find.query,
                                  .pattern = {.regex = find.query, .options = find_options(find.toggles), .groups = {0}},
                                  .forward = true});
        }
    }
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

// The wheel scrolls: down the text, or along it with Shift. With Ctrl it
// asks for the text to grow, a notch up, or to shrink, and scrolls nothing.
// A notch up or left is positive; the scroll grows down and right.
void take_wheel(core::Context& ui, EditorView& view) {
    if (const core::Pointer* mouse = ui.find_pointer(core::PointerType::Mouse, 0)) {
        if (ui.modifiers().ctrl) {
            view.zoom += mouse->wheel.y;
            return;
        }
        const float step = ui.pointer_style().wheel_step;
        const bool along = ui.modifiers().shift;
        view.scroll.x += (along ? -mouse->wheel.y : mouse->wheel.x) * step;
        view.scroll.y += (along ? 0.0f : -mouse->wheel.y) * step;
    }
}

// The minimap and the scrollbar down the view's right side, `side`, with
// ticks for the find's matches and the caret. They scroll the view, as the
// wheel does over the minimap; what they do shows from the next frame.
// How a problem shows on the code: its wave, its line's tint, and its
// message's colour and icon. Nothing for one less than a warning, which a
// list of problems shows alone.
struct ProblemLook {
    ftk::ui::Color wave;
    ftk::ui::Color tint;
    ftk::ui::Color text;
    std::string_view icon;
};

std::optional<ProblemLook> look_of(Severity severity) {
    switch (severity) {
        case Severity::Error:
            return ProblemLook{theme::error(), theme::code_error_line(), theme::code_error_text(), ftk::ui::icon::error};
        case Severity::Warning:
            return ProblemLook{theme::warning(), theme::code_warning_line(), theme::code_warning_text(),
                               ftk::ui::icon::warning};
        case Severity::Information:
        case Severity::Hint:
            break;
    }
    return std::nullopt;
}

// Each line's worst problem: the one its tint and message are for.
std::map<std::size_t, const Diagnostic*> worst_by_line(std::span<const Diagnostic> problems) {
    std::map<std::size_t, const Diagnostic*> worst;
    for (const Diagnostic& problem : problems) {
        if (!look_of(problem.severity)) continue;
        const auto [at, added] = worst.emplace(problem.span.from.line, &problem);
        if (!added && problem.severity < at->second->severity) at->second = &problem;
    }
    return worst;
}

// A wavy line from `left` to `right` about `y`.
void draw_wave(core::DrawList& dl, float left, float right, float y, ftk::ui::Color colour) {
    std::vector<glm::vec2> points;
    bool up = true;
    for (float x = left; x < right; x += WAVE_STEP) {
        points.push_back({x, up ? y - WAVE_HEIGHT : y + WAVE_HEIGHT});
        up = !up;
    }
    points.push_back({right, up ? y - WAVE_HEIGHT : y + WAVE_HEIGHT});
    dl.polyline(points, colour);
}

void draw_side(core::Context& ui, const Document& document, EditorView& view, core::Rect rect, core::Rect side,
               float line, std::span<const Diagnostic> problems) {
    const Folds& folds = view.folds;
    const std::size_t rows = folds.row_count(document.buffer().line_count());
    std::vector<MinimapMark> ticks;
    std::vector<ftk::ui::ScrollMark> marks;
    const auto mark = [&](std::size_t row, ftk::ui::Color colour) {
        if (!ticks.empty() && ticks.back().row == row && ticks.back().colour == colour) return;
        ticks.push_back({.row = row, .colour = colour});
        marks.push_back({.at = static_cast<float>(row) * line, .colour = colour});
    };
    for (const Diagnostic& problem : problems) {
        if (const std::optional<ProblemLook> look = look_of(problem.severity)) {
            mark(folds.row_of(problem.span.from.line), look->wave);
        }
    }
    if (view.find.open || view.find.shown) {
        for (const Span& match : view.find.matches) mark(folds.row_of(match.from.line), theme::accent());
    }
    mark(folds.row_of(view.selections.primary().caret.line), theme::text());

    const glm::vec2 scrolled = view.scroll;
    ui.draw_list().rect_filled(side.min, side.max, theme::surface_base());
    if (view.minimap) {
        const core::Rect map{.min = side.min, .max = {side.max.x - SCROLLBAR_WIDTH, side.max.y}};
        const Minimap drawn = minimap(ui.id("##minimap"), document, folds, map, view.scroll.y, rect.height(), line, ticks);
        if (drawn.scroll) view.scroll.y = *drawn.scroll;
        if (drawn.hovered) take_wheel(ui, view);
    }
    float offset = view.scroll.y;
    const float room = static_cast<float>(rows - 1) * line;
    if (ftk::ui::vertical_scrollbar("##scrollbar", {side.max.x - SCROLLBAR_WIDTH, side.min.y}, side.max, offset,
                                    rect.height(), room, marks)) {
        view.scroll.y = offset;
    }
    if (view.scroll != scrolled) ui.request_frame();
}

// How many rows the view keeps in sight above and below the caret: `around`,
// or as many as fit on both sides.
std::size_t rows_kept(std::size_t around, const Frame& frame) {
    const auto fit = static_cast<std::size_t>(std::max(0.0f, (frame.rect.height() / frame.line - 1.0f) * 0.5f));
    return std::min(around, fit);
}

// Scrolls so the primary caret is in sight with `around` rows above and
// below it, as many as fit and the text has, then keeps the scroll inside
// the text.
void reveal(EditorView& view, const Frame& frame, const Buffer& buffer, const TextLayout& layout, std::size_t around) {
    if (view.reveal) {
        const Position caret = view.selections.primary().caret;
        const std::size_t row = view.folds.row_of(caret.line);
        const std::size_t last = view.folds.row_count(buffer.line_count()) - 1;
        const std::size_t keep = rows_kept(around, frame);
        const float top = static_cast<float>(row - std::min(keep, row)) * frame.line;
        const float bottom = static_cast<float>(std::min(row + keep, last) + 1) * frame.line;
        if (top < view.scroll.y) {
            view.scroll.y = top;
        } else if (bottom > view.scroll.y + frame.rect.height()) {
            view.scroll.y = bottom - frame.rect.height();
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
    const float bottom = static_cast<float>(view.folds.row_count(buffer.line_count()) - 1) * frame.line;
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
float draw_tokens(core::Context& ui, const Document& document, std::span<const Token> tokens, const TextLayout& layout,
                  std::size_t line, std::size_t from, std::size_t to, float x, float origin_x, float y) {
    const std::string_view text = document.buffer().line(line);
    to = std::min(to, text.size());
    std::size_t at = from;
    for (const Token& token : tokens) {
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

// The tokens line `line` is coloured by: its lexer's, with the language
// service's over them where it knows the line (`colours`), made in
// `scratch`.
std::span<const Token> line_tokens(const Document& document, const SemanticTokens* colours, std::size_t line,
                                   std::vector<Token>& scratch) {
    const std::span<const Token> lexed = document.syntax().tokens(line);
    if (colours == nullptr || line >= colours->lines.size() || colours->lines[line].empty()) return lexed;
    scratch = overlaid(lexed, colours->lines[line]);
    return scratch;
}

// The rows in sight: the line each begins with, the first drawn at `top`.
struct Shown {
    std::size_t first_row{0};
    std::vector<std::size_t> lines;
    float top{0.0f};
    // How far apart they are.
    float spacing{CODE_LINE};

    [[nodiscard]] float y(std::size_t index) const { return top + static_cast<float>(index) * spacing; }

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
    const std::size_t count = static_cast<std::size_t>(std::ceil(frame.rect.height() / frame.line)) + 1;
    Shown out;
    out.spacing = frame.line;
    out.first_row = std::min(static_cast<std::size_t>(view.scroll.y / frame.line), rows);
    out.top = frame.rect.min.y - (view.scroll.y - static_cast<float>(out.first_row) * frame.line);
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
// How thick the line under a name renamed in place is (sheet 48).
constexpr float RENAMED_UNDERLINE = 2.0f;

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
            dl.rect_filled({x, y}, {x + 1.0f, y + frame.line},
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
                          .max = {frame.origin_x + layout.x_of(buffer, to), y + frame.line}};
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
        const glm::vec2 max{frame.origin_x + layout.x_of(buffer, to), rows.y(*index) + frame.line};
        if (outline) {
            dl.rect(min, max, theme::accent(), DECORATION_ROUNDING);
        } else {
            dl.rect_filled(min, max, theme::selection_secondary(), DECORATION_ROUNDING);
        }
    }
}

// The uses of a name renamed in place (sheet 48): each washed, the name
// typed underlined in the accent.
void draw_renamed(core::DrawList& dl, const Document& document, const EditorView& view, const Frame& frame,
                  const TextLayout& layout, const Shown& rows) {
    const Buffer& buffer = document.buffer();
    for (const Span& use : view.renaming->uses) {
        const std::optional<std::size_t> index = rows.index_of(view.folds, use.from.line);
        if (!index || use.to.line != use.from.line) continue;
        const glm::vec2 min{frame.origin_x + layout.x_of(buffer, use.from), rows.y(*index)};
        const glm::vec2 max{frame.origin_x + layout.x_of(buffer, use.to), rows.y(*index) + frame.line};
        dl.rect_filled(min, max, theme::selection_secondary());
        dl.rect_filled({min.x, max.y - RENAMED_UNDERLINE}, max, theme::accent());
    }
}

void draw(core::Context& ui, const Document& document, const EditorView& view, const Frame& frame,
          const TextLayout& layout, const Faces& faces, bool focused, std::span<const Diagnostic> problems,
          const SemanticTokens* colours) {
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
    // while it selects nothing; a line with a problem takes the problem's
    // tint instead.
    const std::map<std::size_t, const Diagnostic*> worst = worst_by_line(problems);
    for (const Selection& selection : view.selections.all()) {
        const std::optional<std::size_t> index = rows.index_of(folds, selection.caret.line);
        if (!selection.empty() || !index || worst.contains(rows.lines[*index])) continue;
        dl.rect_filled({frame.rect.min.x, rows.y(*index)}, {frame.rect.max.x, rows.y(*index) + frame.line},
                       theme::code_current_line());
    }
    for (std::size_t index = 0; index < rows.lines.size(); ++index) {
        const auto found = worst.find(rows.lines[index]);
        if (found == worst.end()) continue;
        dl.rect_filled({frame.rect.min.x, rows.y(index)}, {frame.rect.max.x, rows.y(index) + frame.line},
                       look_of(found->second->severity)->tint);
    }

    // Line numbers, right-aligned, the caret's line's brighter; relative,
    // the primary caret's own number goes against the gutter's left. After
    // them a chevron where a fold begins, pointing along the row while it's
    // closed.
    const std::vector<FoldRange>& ranges = document.folds();
    const std::size_t caret_line = view.selections.primary().caret.line;
    for (std::size_t index = 0; index < rows.lines.size(); ++index) {
        const std::size_t line = rows.lines[index];
        const std::string number = std::to_string(line_number_shown(view.line_numbers, folds, line, caret_line));
        const float width = ui.text().caret_x(number, faces.numbers, number.size());
        const Color colour = caret_on(line) ? theme::text_secondary() : theme::text_disabled();
        const bool left = view.line_numbers == LineNumbers::Relative && line == caret_line;
        const float x = left ? frame.rect.min.x + GUTTER_PAD : frame.numbers_right - width;
        ui.text().draw(dl, {x, rows.y(index)}, colour, number, faces.numbers);
        if (std::ranges::binary_search(ranges, line, {}, &FoldRange::first)) {
            const std::string_view chevron = folds.closed_at(line) ? ftk::ui::icon::fold_closed : ftk::ui::icon::fold_open;
            const float chevron_width = ui.text().caret_x(chevron, faces.numbers, chevron.size());
            ui.text().draw(dl, {std::round(frame.numbers_right + (FOLD_COLUMN - chevron_width) * 0.5f), rows.y(index)},
                           theme::text_disabled(), chevron, faces.numbers);
        }
    }

    dl.push_clip(frame.text.min, frame.text.max);

    const bool finding = (view.find.open || view.find.shown) && !rows.lines.empty();
    if (!rows.lines.empty()) {
        draw_guides(dl, document, view, frame, layout, rows);
        draw_marks(dl, document, view, frame, layout, rows);
    }
    if (finding) draw_matches(dl, document, view, frame, layout, rows, false);
    if (view.renaming && view.renaming->begun && !rows.lines.empty()) draw_renamed(dl, document, view, frame, layout, rows);

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
            dl.rect_filled({frame.origin_x + from, rows.y(index)}, {frame.origin_x + to, rows.y(index) + frame.line},
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
    std::vector<Token> scratch;
    for (std::size_t index = 0; index < rows.lines.size(); ++index) {
        const float y = rows.y(index);
        const std::size_t line = rows.lines[index];
        const std::span<const Token> tokens = line_tokens(document, colours, line, scratch);
        if (!composing.empty() && line == caret.line) {
            (void)draw_tokens(ui, document, tokens, layout, line, 0, caret.byte, 0.0f, frame.origin_x, y);
            const float left = frame.origin_x + caret_x;
            ui.text().draw(dl, {left, y}, theme::text(), composing, layout.style());
            dl.line({left, y + frame.line - COMPOSING_UNDERLINE}, {left + composed_width, y + frame.line - COMPOSING_UNDERLINE},
                    theme::text());
            (void)draw_tokens(ui, document, tokens, layout, line, caret.byte, buffer.line(line).size(),
                              caret_x + composed_width, frame.origin_x, y);
        } else {
            (void)draw_tokens(ui, document, tokens, layout, line, 0, buffer.line(line).size(), 0.0f, frame.origin_x, y);
        }
        for (const FoldMark& mark : drawn[index].marks) {
            const float left = std::round(frame.origin_x + mark.pill_from);
            const float top = y + (frame.line - faces.pill_height) * 0.5f;
            dl.rect_filled({left, top}, {std::round(frame.origin_x + mark.pill_to), top + faces.pill_height},
                           theme::surface_raised(), faces.pill_height * 0.5f);
            ui.text().draw(dl, {left + PILL_PAD, y}, theme::text_secondary(), ELLIPSIS, faces.pill);
            if (mark.closer) {
                std::vector<Token> closer_scratch;
                (void)draw_tokens(ui, document, line_tokens(document, colours, mark.closer->line, closer_scratch), layout,
                                  mark.closer->line, mark.closer->byte, buffer.line(mark.closer->line).size(),
                                  mark.closer_x, frame.origin_x, y);
            }
        }
    }

    // Each problem's code underlined with a wave, on the lines in sight; one
    // that takes no text still marks a space's width.
    for (const Diagnostic& problem : problems) {
        const std::optional<ProblemLook> look = look_of(problem.severity);
        if (!look) continue;
        const Span span{.from = buffer.clamp(problem.span.from), .to = buffer.clamp(problem.span.to)};
        for (std::size_t line = span.from.line; line <= span.to.line; ++line) {
            const std::optional<std::size_t> index = rows.index_of(folds, line);
            if (!index || rows.lines[*index] != line) continue;
            const std::size_t from = line == span.from.line ? span.from.byte : 0;
            const std::size_t to = line == span.to.line ? span.to.byte : buffer.line(line).size();
            const float left = layout.x_of(buffer, {line, from});
            const float right = std::max(layout.x_of(buffer, {line, to}), left + layout.space_width());
            draw_wave(dl, frame.origin_x + left, frame.origin_x + right, rows.y(*index) + frame.line - WAVE_RISE,
                      look->wave);
        }
    }

    // After a line with problems, the worst one's message: its first line,
    // its icon before it.
    for (std::size_t index = 0; index < rows.lines.size(); ++index) {
        const auto found = worst.find(rows.lines[index]);
        if (found == worst.end()) continue;
        const ProblemLook look = *look_of(found->second->severity);
        const std::string_view message = found->second->message;
        const std::string_view first_line = message.substr(0, message.find('\n'));
        const float x = frame.origin_x + drawn[index].end + MESSAGE_SPACES * layout.space_width();
        ui.text().draw(dl, {x, rows.y(index)}, look.text, look.icon, faces.message);
        const float icon_width = ui.text().caret_x(look.icon, faces.message, look.icon.size());
        ui.text().draw(dl, {x + icon_width + MESSAGE_ICON_GAP, rows.y(index)}, look.text, first_line, faces.message);
    }

    // The carets, while the view has the keyboard, in the shape Vim's mode
    // gives them (sheet 47, C1): without Vim and in insert mode a bar, the
    // bars blinking together, the primary one where the input method's
    // cursor is in what it composes; in normal mode a still block over the
    // character, the character cut out of it in the code's ground; in
    // replace mode an underline; with an operator waiting, the lower half of
    // a block.
    const vim::CaretShape shape = view.vim ? view.vim->caret_shape() : vim::CaretShape::Bar;
    const bool bar = shape == vim::CaretShape::Bar;
    if (focused && (!bar || std::fmod(ui.time() - view.moved, BLINK) <= BLINK_ON)) {
        const auto composed_cursor = static_cast<std::size_t>(
            std::clamp(ui.composition_cursor(), 0, static_cast<int>(composing.size())));
        // Each selection's caret; in visual mode, the blocks where Vim says,
        // on the character at the end each selection moves by.
        std::vector<Position> carets = view.vim ? view.vim->cursors() : std::vector<Position>{};
        if (carets.empty()) {
            for (const Selection& selection : view.selections.all()) carets.push_back(selection.caret);
        }
        const Position primary = view.selections.primary().caret;
        for (const Position& at : carets) {
            const std::optional<std::size_t> index = rows.index_of(folds, at.line);
            if (!index) continue;
            const float top = rows.y(*index) + 1.0f;
            const float bottom = rows.y(*index) + frame.line - 1.0f;
            float along = layout.x_of(buffer, at);
            if (bar) {
                if (!composing.empty() && at == primary) {
                    along = layout.advance(composing, 0, composed_cursor, along);
                }
                const float x = std::round(frame.origin_x + along);
                dl.rect_filled({x - CARET_WIDTH * 0.5f, top}, {x + CARET_WIDTH * 0.5f, bottom}, theme::text());
                continue;
            }
            // The character's cell; at a line's end, or on an empty line, a
            // space's.
            const std::string_view text = buffer.line(at.line);
            const Position next = char_right(buffer, at);
            const std::size_t end = next.line == at.line ? next.byte : at.byte;
            const float right = end > at.byte ? layout.x_of(buffer, next) : along + layout.space_width();
            const float left_x = std::round(frame.origin_x + along);
            const float right_x = std::round(frame.origin_x + right);
            if (shape == vim::CaretShape::Block) {
                dl.rect_filled({left_x, top}, {right_x, bottom}, theme::text());
                if (end > at.byte) {
                    (void)draw_run(ui, layout, text, at.byte, end, along, frame.origin_x, rows.y(*index),
                                   theme::surface_base());
                }
            } else if (shape == vim::CaretShape::Underline) {
                dl.rect_filled({left_x, bottom - CARET_WIDTH}, {right_x, bottom}, theme::text());
            } else {
                dl.rect_filled({left_x, std::round((top + bottom) * 0.5f)}, {right_x, bottom}, theme::text());
            }
        }
    }

    dl.pop_clip();
    dl.pop_clip();
}

// The hover card under the word the mouse rests on, the service's answer
// drawn in it.
void draw_hover_card(core::Context& ui, const Buffer& buffer, EditorView& view, const Frame& frame,
                     const TextLayout& layout, core::Rect code, const Sizes& sizes) {
    EditorView::Hovering& hovering = view.hovering;
    const Span word = *hovering.word;
    const float scale = sizes.text / CODE_TEXT;
    const MarkdownLook look = card_look(scale);
    const glm::vec2 pad = CARD_PAD * scale;
    const float inner = std::clamp(markdown_width(ui, hovering.card, look), CARD_NARROWEST * scale, CARD_WIDEST * scale);
    const float top = frame.rect.min.y + static_cast<float>(view.folds.row_of(word.from.line)) * sizes.line - view.scroll.y;
    const glm::vec2 anchor_min{frame.origin_x + layout.x_of(buffer, word.from) - CARD_SHIFT * scale, top};
    const glm::vec2 anchor_max{frame.origin_x + layout.x_of(buffer, word.to), top + sizes.line};
    const ftk::ui::Card card("##hover", {.view_min = code.min,
                                         .view_max = code.max,
                                         .anchor_min = anchor_min,
                                         .anchor_max = anchor_max,
                                         .width = inner + 2.0f * pad.x,
                                         .padding = pad});
    const float height = draw_markdown(ui, hovering.card, card.inner_min(), card.inner_width(), look);
    ftk::ui::reserve({card.inner_width(), height});
    // As it shows: no taller than the view lets it, what it holds scrolling.
    hovering.card_rect = core::Rect{.min = card.min(), .max = card.max(height)};
}

} // namespace

float code_line(float text_size) {
    return std::round(text_size * CODE_LINE / CODE_TEXT);
}

std::size_t line_number_shown(LineNumbers numbers, const Folds& folds, std::size_t line, std::size_t caret_line) {
    if (numbers == LineNumbers::Absolute || line == caret_line) return line + 1;
    const std::size_t row = folds.row_of(line);
    const std::size_t caret_row = folds.row_of(caret_line);
    return row > caret_row ? row - caret_row : caret_row - row;
}

CodeViewResult code_view(std::string_view label, Document& document, EditorView& view, const Keymap& keymap,
                         core::Rect rect) {
    core::Context& ui = core::current();
    const ftk::ui::Id id = ui.id(label);
    // A new text size keeps the line at the top, and how far along the
    // lines it is, where they were.
    if (view.drawn_text_size > 0.0f && view.drawn_text_size != view.text_size) {
        view.scroll.y *= code_line(view.text_size) / code_line(view.drawn_text_size);
        view.scroll.x *= view.text_size / view.drawn_text_size;
    }
    view.drawn_text_size = view.text_size;
    const Sizes sizes(view.text_size);
    TextLayout layout(ui.text(), mono(sizes.text, sizes.line), document.indent().width);
    layout.set_folds(&view.folds);
    Faces faces{.numbers = mono(sizes.gutter, sizes.line),
                .pill = mono(sizes.pill, sizes.line),
                .message = face(ftk::ui::Font::Body, sizes.message, sizes.line)};
    faces.pill_height = sizes.pill_height;
    faces.digit_width = ui.text().caret_x("0", faces.numbers, 1);
    faces.pill_width = ui.text().caret_x(ELLIPSIS, faces.pill, ELLIPSIS.size()) + 2.0f * PILL_PAD;
    const Buffer& buffer = document.buffer();

    // The code, and down its right side the minimap and the scrollbar.
    const float side_width = (view.minimap ? MINIMAP_WIDTH : 0.0f) + SCROLLBAR_WIDTH;
    const core::Rect code{.min = rect.min, .max = {std::max(rect.min.x, rect.max.x - side_width), rect.max.y}};
    const core::Rect side{.min = {code.max.x, rect.min.y}, .max = rect.max};

    catch_up(document, view);
    const Frame frame = place(code, view, buffer, faces.digit_width, sizes.line);
    layout.set_page_lines(std::max<std::size_t>(1, static_cast<std::size_t>(rect.height() / sizes.line) - 1));

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
    // Completion's entry taken, by the keys or a click: an edit of its own.
    auto accept = [&](std::optional<std::size_t> index) {
        view.folds.catch_up(document);
        CommandContext context{.document = document, .selections = view.selections, .layout = layout,
                               .clipboard = &clipboard, .folds = &view.folds, .text = {}};
        const std::optional<Completed> completed = accept_completion(view.completing, context, index);
        // Vim makes it again with the rest of the insert, for `.`.
        if (completed && view.vim) view.vim->completed(completed->replaced, completed->text);
    };
    std::optional<Rename> rename;
    // What Vim asks of the program, in order.
    std::vector<vim::Asked> vim_asked;
    // The rows wholly in sight, which Vim's H, Ctrl-D and zz move by and
    // may scroll.
    const auto first_row = static_cast<std::size_t>(std::ceil(view.scroll.y / frame.line - 0.01f));
    const auto past_last = static_cast<std::size_t>((view.scroll.y + frame.rect.height()) / frame.line + 0.01f);
    InView in_view{.top = first_row,
                   .rows = past_last > first_row ? past_last - first_row : 1,
                   .around = rows_kept(view.around_caret, frame)};
    // Vim's view of the document, for it to act through.
    const auto vim_context = [&] {
        view.folds.catch_up(document);
        return CommandContext{.document = document, .selections = view.selections, .layout = layout,
                              .clipboard = &clipboard, .folds = &view.folds, .text = {}, .in_view = &in_view};
    };
    const auto to_vim = [&](const Chord& key) {
        CommandContext context = vim_context();
        for (vim::Asked& handed : view.vim->take(key, context)) vim_asked.push_back(std::move(handed));
    };
    auto run = [&](std::string_view command, std::string_view text = {}) {
        if (command == "completion.accept") {
            accept(std::nullopt);
            return;
        }
        if (command == "symbol.rename") {
            start_rename(view, document);
            return;
        }
        if (is_view_command(command)) {
            run_view_command(command, document, view);
            return;
        }
        // With Vim on, a command Vim has keys for runs as those keys, the
        // ones the menus show: Paste in normal mode puts as "+P does.
        if (view.vim) {
            if (const std::optional<std::vector<Chord>> keys = vim::keys_for(command, view.vim->mode())) {
                CommandContext context = vim_context();
                view.vim->cancel(context);
                for (const Chord& key : *keys) to_vim(key);
                return;
            }
        }
        view.folds.catch_up(document);
        CommandContext context{.document = document, .selections = view.selections, .layout = layout,
                               .clipboard = &clipboard, .folds = &view.folds, .text = text};
        (void)run_command(command, context);
    };
    // What a menu or a palette asked for, first.
    for (const std::string& command : std::exchange(view.queued, {})) run(command);

    // Answers that came: a format made as one step if the text is as it was
    // asked about; a definition in this file selected, a jump with Vim on,
    // one in another handed out.
    std::optional<Location> go;
    if (view.formatting && view.formatting->answered) {
        const std::optional<VersionedEdit> formatted = view.formatting->answer;
        view.formatting.reset();
        if (formatted && formatted->version == document.version() && !formatted->edit.changes.empty()) {
            CommandContext context{.document = document, .selections = view.selections, .layout = layout,
                                   .clipboard = &clipboard, .folds = &view.folds, .text = {}};
            (void)apply_edit(context, formatted->edit);
        }
    }
    if (view.going && view.going->answered) {
        if (!view.going->answer.empty()) {
            const Location& found = view.going->answer.front();
            if (found.path == view.path) {
                if (view.vim) vim_asked.emplace_back(view.vim->jumped(view.selections.primary().caret, document));
                view.selections = Selections({.anchor = buffer.clamp(found.span.from), .caret = buffer.clamp(found.span.to)});
            } else {
                go = found;
            }
        }
        view.going.reset();
    }

    // A word begun, or what the service names, just typed asks what
    // completes it. A word goes on narrowing a list already open; what the
    // service names asks anew.
    const auto ask_completion = [&] {
        const Position caret = view.selections.primary().caret;
        // A name typed in a rename is no word to complete.
        if (view.service != nullptr && view.selections.all().size() == 1 && !view.renaming) {
            const auto asked = asks_completion(document.language(), view.service->completion_triggers(view.path),
                                               buffer.line(caret.line), caret.byte);
            if (asked && (asked->kind == CompletionTrigger::Kind::Character || !view.completing.open)) {
                open_completion(view.completing, *view.service, view.path, document, caret, *asked);
            }
        }
    };

    // A rename asked for begins once the service says where the name
    // stands; Enter makes it, Esc gives it up, the completion list's own
    // Enter aside.
    if (view.renaming && !view.renaming->begun && (!view.renaming->asked || view.renaming->asked->answered)) {
        begin_rename(view, document);
    }
    const auto ends_rename = [&](const Chord& key) {
        if (!view.renaming || view.completing.open || !enter_or_escape(key)) return false;
        CommandContext context = vim_context();
        rename = end_rename(view, document, context, key.key == ftk::ui::Key::Enter);
        return true;
    };

    if (focus.focused) {
        // Tab indents here rather than moving the keyboard on.
        ui.claim_key(ftk::ui::Key::Tab, id);
        if (view.vim) {
            take_vim_keys(ui, keymap, view, vim_context, run, [&](const Chord& key) {
                if (ends_rename(key)) return;
                const bool inserting = view.vim->mode() == vim::Mode::Insert;
                to_vim(key);
                // The completion list is insert mode's, and goes with it.
                if (view.vim->mode() != vim::Mode::Insert) {
                    view.completing = {};
                } else if (inserting && !key.text.empty()) {
                    ask_completion();
                }
            }, [&](std::string_view line) {
                CommandContext context = vim_context();
                for (vim::Asked& handed : view.vim->run_line(line, context)) vim_asked.push_back(std::move(handed));
            });
            // Scrolled by Vim: its first row in sight where Vim put it.
            if (in_view.top != first_row) view.scroll.y = static_cast<float>(in_view.top) * frame.line;
        } else {
            take_keys(
                ui, keymap, view, run,
                [&](std::string_view text) {
                    run("edit.type", text);
                    ask_completion();
                },
                ends_rename);
        }
    }

    const Selections before_mouse = view.selections;
    take_mouse(ui, in, frame, document, view, layout, faces.pill_width);
    // The rows kept around the caret are for the keys: a caret the mouse
    // placed never scrolls the text from under it.
    const bool placed = view.selections != before_mouse;
    // A caret the mouse or another view's edit put past a line's end goes
    // where Vim's mode keeps it; a selection the mouse made begins its
    // visual mode.
    if (view.vim) {
        CommandContext context = vim_context();
        view.vim->settle(context, placed);
    }
    // A rename typed in place follows its uses; a caret off them, by the
    // mouse or the keys, gives it up.
    if (view.renaming && view.renaming->begun) {
        follow_uses(*view.renaming, document);
        const std::vector<Span>& uses = view.renaming->uses;
        const bool on_uses = std::ranges::all_of(view.selections.all(), [&](const Selection& selection) {
            const Span span = selection.span();
            return std::ranges::any_of(uses, [&](const Span& use) { return use.from <= span.from && span.to <= use.to; });
        });
        if (!on_uses) {
            CommandContext context = vim_context();
            (void)end_rename(view, document, context, false, true);
        }
    }

    const glm::vec2 scrolled = view.scroll;
    if (in.hovered) take_wheel(ui, view);

    // A caret that came into a closed fold opens it.
    view.folds.catch_up(document);
    for (const Selection& selection : view.selections.all()) (void)view.folds.reveal(selection.caret.line);

    const bool edited = document.version() != version;
    if (edited || view.selections != before) {
        view.moved = ui.time();
        view.reveal = true;
    }
    take_hover(ui, in, frame, document, view, layout, edited || view.selections != before || view.scroll != scrolled);
    if (view.service != nullptr) {
        follow_completion(view.completing, *view.service, view.path, document, view.selections.primary().caret);
    } else {
        view.completing = {};
    }
    view.seen = document.version();
    reveal(view, frame, buffer, layout, placed ? 0 : view.around_caret);
    if (view.vim) show_search(view.find, view.vim->shown_search());
    if (view.find.open || view.find.shown) refresh_matches(view.find, document);
    const Frame drawn = place(code, view, buffer, faces.digit_width, sizes.line);
    static const std::vector<Diagnostic> NO_PROBLEMS;
    const std::vector<Diagnostic>& problems =
        view.service != nullptr ? view.service->diagnostics(view.path) : NO_PROBLEMS;
    const SemanticTokens* colours = view.service != nullptr ? view.service->tokens(view.path) : nullptr;
    draw(ui, document, view, drawn, layout, faces, focus.focused, problems, colours);

    // The kit's pieces from here are the view's: their ids are under its
    // label.
    const ftk::ui::IdScope scope(label);
    draw_side(ui, document, view, rect, side, sizes.line, problems);

    if (focus.focused) {
        // The input method's window goes at the primary caret, and the
        // carets blink.
        const Position caret = view.selections.primary().caret;
        const float x = drawn.origin_x + layout.x_of(buffer, caret);
        const float y = rect.min.y + static_cast<float>(view.folds.row_of(caret.line)) * sizes.line - view.scroll.y;
        ui.set_text_input_area({.min = {x, y}, .max = {x + 1.0f, y + sizes.line}});
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
    // Vim's command line, on go to line's plate, the matches' count in it
    // while a pattern typed reads, and under it what a `:` line will do.
    if (view.vim && view.vim->command_line() != nullptr) {
        const vim::CommandLine& line = *view.vim->command_line();
        const std::string count = view.vim->shown_search() != nullptr ? match_count(view.find, view.selections) : "";
        const std::optional<vim::Outcome> outcome = view.vim->outcome(vim_context(), view.file_facts);
        const std::optional<vim::NameChoices> names = view.vim->name_choices();
        const std::optional<vim::PathChoices> paths = view.vim->path_choices();
        const std::optional<std::size_t> chosen =
            command_line_plate(line.kind, line.text, count, names ? &*names : nullptr, paths ? &*paths : nullptr,
                               outcome ? &*outcome : nullptr, view.text_size, code);
        // A row clicked goes in as Tab would put it, the keyboard kept.
        if (chosen) {
            if (names) view.vim->choose_name(*chosen);
            if (paths) view.vim->choose_path(*chosen);
            ui.set_focus(id);
        }
    }
    // The keys that can come after those waiting, once they have waited.
    if (view.vim && view.showing_next_keys) {
        const std::vector<NextKey> next = next_keys_of(view, keymap);
        if (!next.empty()) {
            std::vector<Chord> typed = view.pending;
            for (const Chord& key : view.maps.waiting()) typed.push_back(key);
            next_keys_plate(chords_text(typed), next, view.text_size, code);
        }
    }

    // The completion list under the word being typed; an entry clicked is
    // taken.
    if (view.completing.open) {
        const Position start = view.completing.start;
        const float scale = sizes.text / CODE_TEXT;
        const float top = rect.min.y + static_cast<float>(view.folds.row_of(start.line)) * sizes.line - view.scroll.y;
        const glm::vec2 anchor_min{drawn.origin_x + layout.x_of(buffer, start) - CARD_SHIFT * scale, top};
        const glm::vec2 anchor_max{drawn.origin_x + layout.x_of(buffer, view.selections.primary().caret), top + sizes.line};
        const std::string extension = view.path.extension().string();
        if (const std::optional<std::size_t> clicked =
                completion_card(view.completing, code.min, code.max, anchor_min, anchor_max, sizes.text,
                                extension.empty() ? extension : extension.substr(1))) {
            accept(*clicked);
            // Its carets are where the edit left them already.
            view.seen = document.version();
            view.moved = ui.time();
            view.reveal = true;
            ui.set_focus(id);
            ui.request_frame();
        }
    }

    // The hover card over everything else the view draws.
    if (view.hovering.word && !view.hovering.card.empty()) {
        draw_hover_card(ui, buffer, view, drawn, layout, code, sizes);
    } else {
        view.hovering.card_rect.reset();
    }

    ui.semantics(id, core::Role::TextField, label, {}, {.focused = focus.focused}, rect);
    // Whole steps of text size go to the one drawing the view; what is left
    // of a notch waits for more.
    const int zoom = static_cast<int>(std::trunc(view.zoom));
    view.zoom -= static_cast<float>(zoom);
    return {.edited = document.version() != version,
            .focused = focus.focused,
            .go = std::move(go),
            .rename = rename,
            .asked = std::move(vim_asked),
            .zoom = zoom};
}

} // namespace fjedit
