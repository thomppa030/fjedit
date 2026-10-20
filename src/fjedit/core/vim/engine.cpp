#include "fjedit/core/vim/engine.hpp"

#include "fjedit/core/vim/notation.hpp"
#include "fjedit/core/vim/operators.hpp"
#include "fjedit/core/vim/text_objects.hpp"
#include "fjedit/core/vim/walk.hpp"

#include "ftk/base/utf8.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit::vim {

namespace {

using ftk::ui::Key;

// Counts stop growing here, long before they could overflow.
constexpr std::size_t COUNT_MAX = 99999;

// The goal along the line that keeps a caret at the end of every line it
// moves up or down to, as `$` leaves it.
constexpr float LINE_END_GOAL = std::numeric_limits<float>::infinity();

bool pressed(const Chord& key, Key k, ftk::ui::Modifiers modifiers = {}) {
    return key.text.empty() && key.key == k && key.modifiers == modifiers;
}

// Esc, and the keys Vim takes for it: Ctrl-[ and Ctrl-C.
bool escape(const Chord& key) {
    return pressed(key, Key::Escape) || pressed(key, Key::LeftBracket, {.ctrl = true}) ||
           pressed(key, Key::C, {.ctrl = true});
}

// Where normal mode keeps a caret that is at `at`: on a character, the
// line's last when past it.
Position on_character(const Buffer& buffer, Position at) {
    const std::string_view line = buffer.line(at.line);
    if (!line.empty() && at.byte >= line.size()) return last_character(buffer, at.line);
    return at;
}

// Past the character at `at`: the next one, or the next line's start after a
// line's end, whose break a selection then takes; `at` at the text's end.
Position past(const Buffer& buffer, Position at) {
    if (at.byte < buffer.line(at.line).size()) return right_in_line(buffer, at, 1);
    if (at.line + 1 < buffer.line_count()) return {.line = at.line + 1, .byte = 0};
    return at;
}

// The character before `at`, the line break before a line's start.
Position before(const Buffer& buffer, Position at) {
    if (at.byte > 0) return left_in_line(buffer, at, 1);
    if (at.line > 0) return {.line = at.line - 1, .byte = buffer.line(at.line - 1).size()};
    return at;
}

// How wide the character at `at` is along its line; at a line's end, a
// width enough to take the end.
float cell_width(const Layout& layout, const Buffer& buffer, Position at) {
    if (at.byte >= buffer.line(at.line).size()) return 1.0f;
    return layout.x_of(buffer, right_in_line(buffer, at, 1)) - layout.x_of(buffer, at);
}

// A normal-mode command as its keys spell it: a register, a count, an
// operator with a count of its own, and what it acts on or does.
struct Command {
    // The register named with ", or " itself.
    char reg{'"'};
    std::optional<std::size_t> count;
    // The operator: d c y < > = g~ gu gU; empty for none.
    std::string op;
    std::optional<std::size_t> op_count;
    // What it is or acts on, by its keys in Vim's notation: a motion ("w",
    // "gg"), a text object ("iw"), a command ("x", "<C-r>"), or "line" for
    // the operator doubled (dd).
    std::string name;
    // The character f, t, F, T and r take.
    std::string argument;
};

enum class Parsed : uint8_t {
    // A command begun: the next key goes on with it.
    Waiting,
    // Keys that make no command.
    Invalid,
    Whole,
};

bool digit(const Chord& key) {
    return key.text.size() == 1 && key.text[0] >= '0' && key.text[0] <= '9';
}

// A count from `keys[i]` on, which never begins with 0: 0 alone goes to the
// line's start.
void read_count(std::span<const Chord> keys, std::size_t& i, std::optional<std::size_t>& count) {
    while (i < keys.size() && digit(keys[i]) && (count || keys[i].text != "0")) {
        const auto value = static_cast<std::size_t>(keys[i].text[0] - '0');
        count = std::min(count.value_or(0) * 10 + value, COUNT_MAX);
        ++i;
    }
}

// The keys ending at `end`: Whole when that is the last, else too many.
Parsed ends_at(std::span<const Chord> keys, std::size_t end) {
    return end == keys.size() ? Parsed::Whole : Parsed::Invalid;
}

// The character a key types as f, t and r take it: what it types, or a line
// break or a tab for Enter and Tab; empty for any other key.
std::string argument_of(const Chord& key) {
    if (!key.text.empty()) return key.text;
    if (key.modifiers == ftk::ui::Modifiers{} && key.key == Key::Enter) return "\n";
    if (key.modifiers == ftk::ui::Modifiers{} && key.key == Key::Tab) return "\t";
    return {};
}

// A motion or a command from `keys[i]` on.
Parsed parse_name(std::span<const Chord> keys, std::size_t i, Command& command) {
    const Chord& key = keys[i];
    const bool prefix = key.text == "g" || key.text == "z";
    const bool takes_character = key.text == "f" || key.text == "t" || key.text == "F" || key.text == "T" ||
                                 (key.text == "r" && command.op.empty());
    if (!prefix && !takes_character) {
        command.name = notation(key);
        return ends_at(keys, i + 1);
    }
    if (i + 1 == keys.size()) return Parsed::Waiting;
    const Chord& next = keys[i + 1];
    if (prefix) {
        // z<CR> is the one that ends on a key typing nothing.
        if (key.text == "z" && pressed(next, Key::Enter)) {
            command.name = "z<CR>";
            return ends_at(keys, i + 2);
        }
        if (next.text.empty()) return Parsed::Invalid;
        command.name = key.text + next.text;
    } else {
        command.argument = argument_of(next);
        if (command.argument.empty()) return Parsed::Invalid;
        command.name = key.text;
    }
    return ends_at(keys, i + 2);
}

// A register named with " at `keys[i]`, and a count after it: nothing when
// they are whole, else what the keys come to so far.
std::optional<Parsed> read_register_and_count(std::span<const Chord> keys, std::size_t& i, Command& command) {
    if (keys[i].text == "\"") {
        if (i + 1 == keys.size()) return Parsed::Waiting;
        const std::string& name = keys[i + 1].text;
        if (name.size() != 1 || !Registers::valid(name[0])) return Parsed::Invalid;
        command.reg = name[0];
        i += 2;
    }
    read_count(keys, i, command.count);
    if (i == keys.size()) return Parsed::Waiting;
    return std::nullopt;
}

// What `keys`, typed so far in visual mode, come to: an operator acts at
// once, on the selections, and a text object needs no operator before it.
Parsed parse_visual(std::span<const Chord> keys, Command& command) {
    std::size_t i = 0;
    if (const std::optional<Parsed> unfinished = read_register_and_count(keys, i, command)) return *unfinished;
    if (keys[i].text == "i" || keys[i].text == "a") {
        if (i + 1 == keys.size()) return Parsed::Waiting;
        if (keys[i + 1].text.empty()) return Parsed::Invalid;
        command.name = keys[i].text + keys[i + 1].text;
        return ends_at(keys, i + 2);
    }
    return parse_name(keys, i, command);
}

// What `keys`, typed so far, come to.
Parsed parse(std::span<const Chord> keys, Command& command) {
    std::size_t i = 0;
    if (const std::optional<Parsed> unfinished = read_register_and_count(keys, i, command)) return *unfinished;

    const std::string& key = keys[i].text;
    if (key == "d" || key == "c" || key == "y" || key == "<" || key == ">" || key == "=") {
        command.op = key;
        ++i;
    } else if (key == "g") {
        if (i + 1 == keys.size()) return Parsed::Waiting;
        const std::string& next = keys[i + 1].text;
        if (next == "~" || next == "u" || next == "U") {
            command.op = key + next;
            i += 2;
        }
    }
    if (command.op.empty()) return parse_name(keys, i, command);

    read_count(keys, i, command.op_count);
    if (i == keys.size()) return Parsed::Waiting;
    // The operator doubled acts on lines: dd, >>, g~~ and g~g~.
    const std::string last(1, command.op.back());
    if (keys[i].text == last) {
        command.name = "line";
        return ends_at(keys, i + 1);
    }
    if (command.op.size() == 2 && keys[i].text == "g") {
        if (i + 1 == keys.size()) return Parsed::Waiting;
        if (keys[i + 1].text == last) {
            command.name = "line";
            return ends_at(keys, i + 2);
        }
    }
    // A text object.
    if (keys[i].text == "i" || keys[i].text == "a") {
        if (i + 1 == keys.size()) return Parsed::Waiting;
        if (keys[i + 1].text.empty()) return Parsed::Invalid;
        command.name = keys[i].text + keys[i + 1].text;
        return ends_at(keys, i + 2);
    }
    return parse_name(keys, i, command);
}

// Where a motion took a caret, the goal along the line it keeps for going
// up and down, and how an operator over it takes the text.
struct Went {
    Position to;
    std::optional<float> goal;
    MotionKind kind{MotionKind::Exclusive};
};

// `count` shown lines down from `selection`'s caret, or up, at its goal
// along the line; as far as there are, nothing when there are none.
std::optional<Went> vertical(const CommandContext& c, const Selection& selection, bool down, std::size_t count) {
    const Buffer& buffer = c.document.buffer();
    std::size_t line = selection.caret.line;
    std::size_t moved = 0;
    for (; moved < count; ++moved) {
        const std::optional<std::size_t> next = down ? c.layout.line_below(buffer, line, 1) : c.layout.line_above(buffer, line, 1);
        if (!next) break;
        line = *next;
    }
    if (moved == 0) return std::nullopt;
    const float goal = selection.goal.value_or(c.layout.x_of(buffer, selection.caret));
    return Went{.to = c.layout.position_at(buffer, line, goal), .goal = goal, .kind = MotionKind::Linewise};
}

// `count` characters on, or back, going over line breaks as Space and
// Backspace do in normal mode, onto characters only.
Position across_lines(const Buffer& buffer, Position at, bool on, std::size_t count) {
    for (; count > 0; --count) {
        if (on) {
            const Position next = right_in_line(buffer, at, 1);
            if (next.byte < buffer.line(at.line).size()) {
                at = next;
            } else if (at.line + 1 < buffer.line_count()) {
                at = {.line = at.line + 1, .byte = 0};
            } else {
                break;
            }
        } else if (at.byte > 0) {
            at = left_in_line(buffer, at, 1);
        } else if (at.line > 0) {
            at = last_character(buffer, at.line - 1);
        } else {
            break;
        }
    }
    return at;
}

// A line's first character that isn't blank, as a linewise motion goes to.
Went to_line(const Buffer& buffer, std::size_t line) {
    return {.to = first_non_blank(buffer, std::min(line, buffer.line_count() - 1)), .goal = {}, .kind = MotionKind::Linewise};
}

// The row `line` is drawn on, and the line a row begins with, through the
// view's closed folds; the text's last row.
std::size_t row_of(const CommandContext& c, std::size_t line) {
    return c.folds != nullptr ? c.folds->row_of(line) : line;
}
std::size_t line_of(const CommandContext& c, std::size_t row) {
    return c.folds != nullptr ? c.folds->line_of(row) : row;
}
std::size_t last_row(const CommandContext& c) {
    return row_of(c, c.document.buffer().line_count() - 1);
}

// H, M and L: the row `count` from the view's top or bottom, or its middle,
// of the rows the text fills. Kept from an edge by the rows kept around the
// caret, unless the text's end is in sight there or an operator waits.
std::optional<Went> in_sight(const CommandContext& c, const Command& command) {
    if (c.in_view == nullptr) return std::nullopt;
    const InView& view = *c.in_view;
    const std::size_t last = last_row(c);
    const std::size_t top = std::min(view.top, last);
    const std::size_t bottom = std::min(top + std::max<std::size_t>(view.rows, 1) - 1, last);
    const bool moving = command.op.empty();
    const std::size_t n = command.count.value_or(1) - 1;
    std::size_t row = top + (bottom - top) / 2;
    if (command.name == "H") {
        row = std::min(top + std::max(n, top > 0 && moving ? view.around : 0), bottom);
    } else if (command.name == "L") {
        row = bottom - std::min(std::max(n, bottom < last && moving ? view.around : 0), bottom - top);
    }
    return to_line(c.document.buffer(), line_of(c, row));
}

// Where Vim's scrolling command `name` puts the view's top row and each
// caret of `carets`: Ctrl-D and Ctrl-U half a view (or `count` rows, kept in
// `half` for the next), Ctrl-F and Ctrl-B a view less two rows, the carets
// moving with the text or into sight; Ctrl-E and Ctrl-Y a row, the carets
// moving only to stay in sight; zz, zt and zb the primary caret's row to the
// middle, the top or the bottom (with a count, line `count` first), z., z<CR>
// and z- the same at its first non-blank. Nothing for another name, or with
// no view.
std::optional<std::vector<Selection>> scroll_view(CommandContext& c, std::string_view name,
                                                  std::optional<std::size_t> count, std::span<const Selection> carets,
                                                  std::size_t& half) {
    if (c.in_view == nullptr || carets.empty()) return std::nullopt;
    InView& view = *c.in_view;
    const Buffer& buffer = c.document.buffer();
    const std::size_t last = last_row(c);
    const std::size_t rows = std::max<std::size_t>(view.rows, 1);
    const std::size_t top = std::min(view.top, last);
    // A caret on `row`, at its goal along the line.
    const auto on_row = [&](const Selection& selection, std::size_t row) {
        const float goal = selection.goal.value_or(c.layout.x_of(buffer, selection.caret));
        const Position to = c.layout.position_at(buffer, line_of(c, std::min(row, last)), goal);
        return Selection{.anchor = to, .caret = to, .goal = goal};
    };
    std::vector<Selection> out(carets.begin(), carets.end());

    if (name == "<C-d>" || name == "<C-u>") {
        if (count) half = *count;
        const std::size_t n = half > 0 ? half : std::max<std::size_t>(rows / 2, 1);
        const bool down = name == "<C-d>";
        // Down no further than shows the text's last row at the bottom.
        const std::size_t last_top = last + 1 > rows ? last + 1 - rows : 0;
        view.top = down ? std::max(top, std::min(top + n, last_top)) : top - std::min(n, top);
        for (Selection& selection : out) {
            const std::size_t row = row_of(c, selection.caret.line);
            selection = on_row(selection, down ? std::min(row + n, last) : row - std::min(n, row));
        }
        return out;
    }
    const bool page = name == "<C-f>" || name == "<C-b>";
    if (page || name == "<C-e>" || name == "<C-y>") {
        const std::size_t n = (page ? std::max<std::size_t>(rows > 2 ? rows - 2 : 1, 1) : 1) * count.value_or(1);
        const bool down = name == "<C-f>" || name == "<C-e>";
        view.top = down ? std::min(top + n, last) : top - std::min(n, top);
        // The carets kept in sight, as far from the edges as the rows kept
        // around the caret, where the text goes on past them.
        const std::size_t bottom = std::min(view.top + rows - 1, last);
        const std::size_t highest = std::min(view.top + (view.top > 0 ? view.around : 0), bottom);
        const std::size_t lowest = std::max(bottom - (bottom < last ? std::min(view.around, bottom) : 0), highest);
        for (Selection& selection : out) {
            const std::size_t row = row_of(c, selection.caret.line);
            if (row < highest) selection = on_row(selection, highest);
            if (row > lowest) selection = on_row(selection, lowest);
        }
        return out;
    }
    const bool middle = name == "zz" || name == "z.";
    const bool to_top = name == "zt" || name == "z<CR>";
    if (!middle && !to_top && name != "zb" && name != "z-") return std::nullopt;
    const bool first_text = name == "z." || name == "z<CR>" || name == "z-";
    Selection& primary = out[std::min(c.selections.primary_index(), out.size() - 1)];
    if (count) primary = on_row(primary, row_of(c, std::min(*count - 1, buffer.line_count() - 1)));
    if (first_text) primary = Selection::at(first_non_blank(buffer, primary.caret.line));
    const std::size_t row = row_of(c, primary.caret.line);
    if (middle) {
        view.top = row - std::min(row, (rows - 1) / 2);
    } else if (to_top) {
        view.top = row - std::min(row, view.around);
    } else {
        view.top = row + view.around + 1 > rows ? std::min(row + view.around + 1 - rows, row) : 0;
    }
    return out;
}

// The match `count` matches on from `at` that `pattern` finds, the way
// `forward` says, going round the text's ends: the first that begins after
// `at`, or the last before it. Nothing when it finds none, `told` then
// saying so for the pattern Vim reads as `text`; a note in it when the
// search went round.
std::optional<Span> search_from(const Buffer& buffer, const Pattern& pattern, std::string_view text, Position at,
                                bool forward, std::size_t count, Message& told) {
    const std::vector<Span> all = find_all(buffer, pattern.regex, pattern.options);
    if (all.empty()) {
        told = {.text = std::format("E486: Pattern not found: {}", text), .error = true};
        return std::nullopt;
    }
    bool round = false;
    auto found = all.begin();
    for (std::size_t i = 0; i < count; ++i) {
        if (forward) {
            found = std::ranges::upper_bound(all, at, {}, &Span::from);
            if (found == all.end()) {
                found = all.begin();
                round = true;
            }
        } else {
            found = std::ranges::lower_bound(all, at, {}, &Span::from);
            if (found == all.begin()) {
                found = all.end();
                round = true;
            }
            --found;
        }
        at = found->from;
    }
    if (round) {
        told = {.text = forward ? "search hit BOTTOM, continuing at TOP" : "search hit TOP, continuing at BOTTOM",
                .error = false};
    }
    return *found;
}

// Where the motion `command` takes `selection`'s caret; nothing for a
// command that isn't a motion, or a motion that can't go. f, t, F and T
// keep what they look for in `remembered`, which ; and , look for again;
// n, N, * and # go on with its search, * and # making it.
std::optional<Went> motion(const CommandContext& c, const Command& command, const Selection& selection,
                           detail::Remembered& remembered) {
    const Buffer& buffer = c.document.buffer();
    const Position at = selection.caret;
    const std::size_t n = command.count.value_or(1);
    const std::size_t last = buffer.line_count() - 1;
    const std::string_view name = command.name;
    const auto went = [](Position to, MotionKind kind = MotionKind::Exclusive) {
        return Went{.to = to, .goal = {}, .kind = kind};
    };

    if (name == "h" || name == "<Left>") return went(left_in_line(buffer, at, n));
    if (name == "l" || name == "<Right>") return went(right_in_line(buffer, at, n));
    if (name == "<Space>") return went(across_lines(buffer, at, true, n));
    if (name == "<BS>") return went(across_lines(buffer, at, false, n));
    if (name == "j" || name == "<Down>" || name == "gj") return vertical(c, selection, true, n);
    if (name == "k" || name == "<Up>" || name == "gk") return vertical(c, selection, false, n);
    if (name == "w" || name == "W") {
        const bool big = name == "W";
        // cw on a word changes to its end, as ce; an operator's w stops at
        // the end of the line it leaves.
        if (command.op == "c" && detail::Walk(buffer, at, big).kind() != detail::BLANK) {
            return went(word_end_after(buffer, at, n, big, true), MotionKind::Inclusive);
        }
        return went(word_start_after(buffer, at, n, big, !command.op.empty()));
    }
    if (name == "b" || name == "B") return went(word_start_before(buffer, at, n, name == "B"));
    if (name == "e" || name == "E") return went(word_end_after(buffer, at, n, name == "E"), MotionKind::Inclusive);
    if (name == "ge" || name == "gE") {
        return went(word_end_before(buffer, at, n, name == "gE"), MotionKind::Inclusive);
    }
    if (name == "0" || name == "<Home>") return went({.line = at.line, .byte = 0});
    if (name == "^") return went(first_non_blank(buffer, at.line));
    if (name == "$" || name == "<End>") {
        const std::size_t line = std::min(at.line + n - 1, last);
        return Went{.to = last_character(buffer, line), .goal = LINE_END_GOAL, .kind = MotionKind::Inclusive};
    }
    if (name == "gg") return to_line(buffer, command.count ? *command.count - 1 : 0);
    if (name == "G") return to_line(buffer, command.count ? *command.count - 1 : last);
    if (name == "<CR>" || name == "+") {
        if (at.line + n > last) return std::nullopt;
        return to_line(buffer, at.line + n);
    }
    if (name == "-") {
        if (n > at.line) return std::nullopt;
        return to_line(buffer, at.line - n);
    }
    if (name == "_") return to_line(buffer, at.line + n - 1);
    if (name == "H" || name == "M" || name == "L") return in_sight(c, command);
    if (name == "}" || name == "{") {
        const Moved moved = name == "}" ? paragraph_after(buffer, at, n) : paragraph_before(buffer, at, n);
        return went(moved.to, moved.kind);
    }
    if (name == "%") {
        // With a count, that much of the way through the text.
        if (command.count) return to_line(buffer, (*command.count * buffer.line_count() + 99) / 100 - 1);
        const std::optional<Position> paired = paired_bracket(buffer, c.document.syntax(), at);
        if (!paired) return std::nullopt;
        return went(*paired, MotionKind::Inclusive);
    }

    // The search: n and N go on with it; * and # look for the word at the
    // caret, whole (g* and g# as it is), # from the word's start so that
    // word isn't found again.
    const bool word = name == "*" || name == "#" || name == "g*" || name == "g#";
    if (word || name == "n" || name == "N") {
        Position from = at;
        if (word) {
            const Span taken = word_at(buffer, at);
            if (taken.empty()) {
                remembered.message = {.text = "E348: No string under cursor", .error = true};
                return std::nullopt;
            }
            const std::string text = buffer.text(taken);
            const bool whole = name.size() == 1;
            const std::string written = whole ? "\\<" + text + "\\>" : text;
            // smartcase is for patterns typed.
            ftk::Result<Pattern> read =
                read_pattern(written, {.ignore_case = remembered.settings.ignore_case, .smart_case = false});
            if (!read) return std::nullopt;
            remembered.search = Search{.text = written, .pattern = std::move(*read), .forward = name.ends_with('*')};
            if (!name.ends_with('*')) from = taken.from;
        }
        if (!remembered.search) {
            remembered.message = {.text = "E35: No previous regular expression", .error = true};
            return std::nullopt;
        }
        remembered.shown = true;
        const Search& search = *remembered.search;
        const bool forward = search.forward == (name != "N");
        const std::optional<Span> found =
            search_from(buffer, search.pattern, search.text, from, forward, n, remembered.message);
        if (!found) return std::nullopt;
        return went(found->from);
    }

    const bool finding = name == "f" || name == "t" || name == "F" || name == "T";
    if (!finding && name != ";" && name != ",") return std::nullopt;
    if (finding) {
        remembered.chars =
            CharSearch{.target = command.argument, .forward = name == "f" || name == "t", .till = name == "t" || name == "T"};
    }
    if (!remembered.chars) return std::nullopt;
    CharSearch looking = *remembered.chars;
    if (name == ",") looking.forward = !looking.forward;
    const std::optional<Position> found = find_in_line(buffer, at, looking, n, !finding);
    if (!found) return std::nullopt;
    return went(*found, looking.forward ? MotionKind::Inclusive : MotionKind::Exclusive);
}

Operator operator_named(std::string_view op) {
    if (op == "d") return Operator::Delete;
    if (op == "c") return Operator::Change;
    if (op == "y") return Operator::Yank;
    if (op == ">") return Operator::ShiftRight;
    if (op == "<") return Operator::ShiftLeft;
    if (op == "=") return Operator::Indent;
    if (op == "gu") return Operator::Lower;
    if (op == "gU") return Operator::Upper;
    return Operator::ToggleCase;
}

// The operator `command` names over what it names at every caret: the
// operator doubled takes lines, from the caret's; a text object what it
// takes around the caret; a motion what it passes over. The counts before
// and after the operator multiply. True when a change has begun insert
// mode.
bool run_operator(const Command& command, CommandContext& c, Registers& registers, detail::Remembered& remembered) {
    const Buffer& buffer = c.document.buffer();
    const bool counted = command.count || command.op_count;
    const std::size_t n = std::min(command.count.value_or(1) * command.op_count.value_or(1), COUNT_MAX);
    Command moving = command;
    moving.count = counted ? std::optional<std::size_t>(n) : std::nullopt;
    const bool object = command.name.size() == 2 && (command.name[0] == 'i' || command.name[0] == 'a');

    const Operator op = operator_named(command.op);
    std::vector<std::optional<Range>> ranges;
    bool any = false;
    // A range with nothing in it: x on an empty line. It changes nothing
    // and leaves the registers, but a change there still inserts.
    bool empty = false;
    for (const Selection& selection : c.selections.all()) {
        std::optional<Range> range;
        if (command.name == "line") {
            const std::size_t last = std::min(selection.caret.line + n - 1, buffer.line_count() - 1);
            range = Range{.span = {selection.caret, {last, 0}}, .linewise = true};
        } else if (object) {
            if (const std::optional<Taken> taken = text_object(buffer, c.document.syntax(), selection.caret, command.name, n)) {
                range = object_range(buffer, *taken);
            }
        } else if (const std::optional<Went> went = motion(c, moving, selection, remembered)) {
            range = motion_range(buffer, selection.caret, went->to, went->kind);
        }
        if (range && !range->linewise && range->span.empty()) {
            empty = true;
            range.reset();
        }
        any = any || range.has_value();
        ranges.push_back(range);
    }
    c.document.history().close();
    if (!any) {
        if (op != Operator::Change || !empty) return false;
        c.document.history().hold();
        return true;
    }
    // The motions Vim always puts a delete's text in register 1 for.
    const bool numbered = command.name == "%" || command.name == "{" || command.name == "}";
    operate(op, ranges, command.reg, c, registers, 1, numbered);
    return op == Operator::Change;
}

// Whether the keys type text in `mode`.
constexpr bool typing(Mode mode) {
    return mode == Mode::Insert || mode == Mode::Replace;
}

// Whether `command` is a change `.` makes again: an operator but y, a small
// change, or a command that inserts.
bool changes(const Command& command) {
    if (!command.op.empty()) return command.op != "y";
    static constexpr std::string_view CHANGES[] = {
        "x", "<Del>", "X", "s", "S", "C", "D", "p", "P", "J", "r", "~", "<C-a>", "<C-x>",
        "i", "<Insert>", "a", "I", "A", "o", "O", "R",
    };
    return std::ranges::contains(CHANGES, std::string_view(command.name));
}

// The keys that spell `command` after its register and counts.
std::vector<Chord> keys_of(const Command& command) {
    std::vector<Chord> keys;
    for (const char ch : command.op) keys.push_back(Chord::typed(std::string_view(&ch, 1)));
    if (command.name == "line") {
        keys.push_back(Chord::typed(std::string_view(command.op).substr(command.op.size() - 1)));
    } else if (const std::optional<std::vector<Chord>> named = parse_notation(command.name)) {
        keys.insert(keys.end(), named->begin(), named->end());
    }
    if (!command.argument.empty()) keys.push_back(Chord::typed(command.argument));
    return keys;
}

// The counts before an operator and after it, as one (2d3w deletes six).
std::optional<std::size_t> count_of(const Command& command) {
    if (!command.count && !command.op_count) return std::nullopt;
    return std::min(command.count.value_or(1) * command.op_count.value_or(1), COUNT_MAX);
}

// What a command line typed for a search holds: the pattern, up to the
// first `kind` (/ or ?) neither escaped nor in a collection, and the offset
// after that one.
struct Typed {
    std::string_view pattern;
    std::string_view offset;
    bool ended{false};
};
Typed split_search(std::string_view text, char kind) {
    std::size_t at = 0;
    while (at < text.size()) {
        const char c = text[at];
        if (c == '\\') {
            at += 2;
        } else if (c == '[') {
            // A collection, its ] first taken as itself; without one, the [
            // is itself.
            std::size_t end = at + 1;
            if (end < text.size() && text[end] == '^') ++end;
            if (end < text.size() && text[end] == ']') ++end;
            while (end < text.size() && text[end] != ']') end += text[end] == '\\' ? 2 : 1;
            at = end < text.size() ? end + 1 : at + 1;
        } else if (c == kind) {
            return {.pattern = text.substr(0, at), .offset = text.substr(at + 1), .ended = true};
        } else {
            ++at;
        }
    }
    return {.pattern = text, .offset = {}, .ended = false};
}

// The keys of `command` before what it names: its register, its counts as
// one, its operator.
std::vector<Chord> keys_before(const Command& command) {
    std::vector<Chord> keys;
    if (command.reg != '"') {
        keys.push_back(Chord::typed("\""));
        keys.push_back(Chord::typed(std::string_view(&command.reg, 1)));
    }
    if (const std::optional<std::size_t> count = count_of(command)) {
        const std::string digits = std::to_string(*count);
        for (const char digit : digits) keys.push_back(Chord::typed(std::string_view(&digit, 1)));
    }
    for (const char ch : command.op) keys.push_back(Chord::typed(std::string_view(&ch, 1)));
    return keys;
}

} // namespace

std::string_view keymap_mode(Mode mode) {
    if (typing(mode)) return Keymap::VIM_INSERT;
    if (visual(mode)) return Keymap::VIM_VISUAL;
    if (mode == Mode::Command) return Keymap::VIM_COMMAND;
    return Keymap::VIM_NORMAL;
}

std::optional<std::vector<Chord>> keys_for(std::string_view name, Mode mode) {
    // The clipboard is Vim's + register.
    struct CommandKeys {
        std::string_view command;
        std::string_view normal;
        std::string_view visual;
        std::string_view insert;
    };
    static constexpr CommandKeys COMMAND_KEYS[] = {
        {"edit.undo", "u", "", "<C-o>u"},
        {"edit.redo", "<C-r>", "", "<C-o><C-r>"},
        {"edit.cut", "\"+dd", "\"+d", ""},
        {"edit.copy", "\"+yy", "\"+y", ""},
        {"edit.paste", "\"+P", "\"+p", "<C-r>+"},
    };
    for (const CommandKeys& keys : COMMAND_KEYS) {
        if (keys.command != name) continue;
        const std::string_view in_mode = typing(mode) ? keys.insert : visual(mode) ? keys.visual : keys.normal;
        if (in_mode.empty()) return std::nullopt;
        return parse_notation(in_mode);
    }
    return std::nullopt;
}

std::optional<std::string> Engine::take(const Chord& key, CommandContext& context) {
    // A key of Ctrl-O's one command, which may go through visual mode and
    // the command line.
    const bool for_one_command = one_command_ && (mode_ == Mode::Normal || visual(mode_) || mode_ == Mode::Command);
    remembered_.message = {};
    dispatch(key, context);
    // A change is whole once no insert goes on with it.
    if (making_ && !typing(mode_)) last_change_ = std::exchange(making_, std::nullopt);
    if (for_one_command && typing(mode_)) {
        // The command went into insert mode itself.
        one_command_ = false;
    } else if (for_one_command && mode_ == Mode::Normal && keys_.empty()) {
        end_one_command(context);
    }
    return std::nullopt;
}

void Engine::dispatch(const Chord& key, CommandContext& context) {
    const bool was_typing = typing(mode_);
    if (mode_ == Mode::Insert) {
        take_insert(key, context);
    } else if (mode_ == Mode::Replace) {
        take_replace(key, context);
    } else if (visual(mode_)) {
        take_visual(key, context);
    } else if (mode_ == Mode::Command) {
        take_command(key, context);
    } else {
        take_normal(key, context);
    }
    // An insert begun, nothing typed in it yet.
    if (!was_typing && typing(mode_)) inserted_.clear();
}

const Pattern* Engine::shown_search() const {
    if (typing_ && typing_->line.kind != ':') return preview_ ? &*preview_ : nullptr;
    return remembered_.shown && remembered_.search ? &remembered_.search->pattern : nullptr;
}

void Engine::search_for(Search search) {
    remembered_.search = std::move(search);
    remembered_.shown = true;
}

void Engine::begin_command_line(char kind, std::vector<Chord> before, bool operating, CommandContext& c,
                                std::string text) {
    typing_ = Typing{.line = {.kind = kind, .text = std::move(text)},
                     .from = mode_,
                     .selections = {c.selections.all().begin(), c.selections.all().end()},
                     .primary = c.selections.primary_index(),
                     .visual = visual_,
                     .before = std::move(before),
                     .operating = operating,
                     .history_at = {},
                     .history_prefix = {}};
    preview_.reset();
    mode_ = Mode::Command;
}

void Engine::take_command(const Chord& key, CommandContext& c) {
    Typing& line = *typing_;
    std::string& text = line.line.text;
    if (register_next_) {
        // Ctrl-R's register: its text typed, to its first line's end.
        register_next_ = false;
        if (key.text.size() == 1) {
            if (const std::optional<Yanked> yanked = registers_->get(key.text[0], c.clipboard)) {
                const std::string all = yanked->text();
                text += all.substr(0, all.find('\n'));
            }
        }
        return preview(c);
    }
    if (escape(key)) return leave_command_line(false, c);
    if (pressed(key, Key::Enter)) return leave_command_line(true, c);
    if (!key.text.empty()) {
        text += key.text;
        line.history_at.reset();
        return preview(c);
    }
    const std::string named = notation(key);
    const auto word = [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
    };
    if (named == "<BS>") {
        // On an empty line, Backspace leaves it.
        if (text.empty()) return leave_command_line(false, c);
        std::size_t end = text.size() - 1;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
        text.erase(end);
    } else if (named == "<C-w>") {
        // The word before the end, with the blanks after it; or one
        // character that is no word's.
        std::size_t end = text.size();
        while (end > 0 && text[end - 1] == ' ') --end;
        if (end > 0 && word(text[end - 1])) {
            while (end > 0 && word(text[end - 1])) --end;
        } else if (end > 0) {
            --end;
        }
        text.erase(end);
    } else if (named == "<C-u>") {
        text.clear();
    } else if (named == "<C-r>") {
        register_next_ = true;
        return;
    } else if (named == "<Up>" || named == "<Down>") {
        // The lines typed before on a line of this kind that begin with
        // what is typed, older with Up; Down past the latest gives back what
        // was typed.
        const std::vector<std::string>& history = line.line.kind == ':' ? commands_ : history_;
        if (!line.history_at) {
            line.history_prefix = text;
            line.history_at = history.size();
        }
        std::size_t at = *line.history_at;
        const bool up = named == "<Up>";
        while (true) {
            if (up ? at == 0 : at >= history.size()) return;
            at = up ? at - 1 : at + 1;
            if (at == history.size()) {
                text = line.history_prefix;
                break;
            }
            if (history[at].starts_with(line.history_prefix)) {
                text = history[at];
                break;
            }
        }
        line.history_at = at;
    } else {
        return;
    }
    preview(c);
}

void Engine::preview(CommandContext& c) {
    const Typing& line = *typing_;
    if (line.line.kind == ':') return;
    c.selections.set(std::vector<Selection>(line.selections), line.primary);
    preview_.reset();
    const std::string_view pattern = split_search(line.line.text, line.line.kind).pattern;
    if (pattern.empty()) return;
    ftk::Result<Pattern> read = read_pattern(pattern, remembered_.settings);
    if (!read) return;
    preview_ = std::move(*read);
    // From normal mode, the match the search goes to is selected, as a
    // find selects the one it is on.
    if (line.from != Mode::Normal || line.operating) return;
    Message unsaid;
    const std::optional<Span> found = search_from(c.document.buffer(), *preview_, pattern,
                                                  c.selections.primary().caret, line.line.kind == '/', 1, unsaid);
    if (found) c.selections = Selections({.anchor = found->from, .caret = found->to, .goal = {}});
}

void Engine::leave_command_line(bool run, CommandContext& c) {
    Typing line = std::move(*typing_);
    typing_.reset();
    preview_.reset();
    register_next_ = false;
    c.selections.set(std::move(line.selections), line.primary);
    mode_ = line.from;
    if (visual(mode_)) {
        visual_ = std::move(line.visual);
        show_visual(c);
    }
    if (!run) return;
    if (line.line.kind == ':') {
        if (line.line.text.empty()) return;
        std::erase(commands_, line.line.text);
        commands_.push_back(line.line.text);
        return run_ex(line.line.text, c);
    }
    const Typed typed = split_search(line.line.text, line.line.kind);
    if (!typed.offset.empty()) {
        remembered_.message = {.text = std::format("Search offsets aren't supported: {}", typed.offset), .error = true};
        return;
    }
    const std::string text(typed.pattern);
    if (text.empty()) {
        // Nothing typed: the last search again, the way this line goes.
        if (!remembered_.search) {
            remembered_.message = {.text = "E35: No previous regular expression", .error = true};
            return;
        }
        remembered_.search->forward = line.line.kind == '/';
    } else {
        ftk::Result<Pattern> read = read_pattern(text, remembered_.settings);
        if (!read) {
            remembered_.message = {.text = read.error(), .error = true};
            return;
        }
        remembered_.search = Search{.text = text, .pattern = std::move(*read), .forward = line.line.kind == '/'};
        std::erase(history_, text);
        history_.push_back(text);
    }
    remembered_.shown = true;
    // As n goes, after the keys of the command it is the motion of.
    for (const Chord& key : line.before) dispatch(key, c);
    dispatch(Chord::typed("n"), c);
}

ftk::Result<std::size_t> Engine::line_at(const ExAddress& address, std::size_t current, CommandContext& c) {
    const Buffer& buffer = c.document.buffer();
    const auto count = static_cast<long>(buffer.line_count());
    long line = 0;
    switch (address.kind) {
        case ExAddress::Kind::Number: line = static_cast<long>(address.number); break;
        case ExAddress::Kind::Current: line = static_cast<long>(current) + 1; break;
        case ExAddress::Kind::Last: line = count; break;
        case ExAddress::Kind::Mark: {
            // '< and '> are the last visual selection's first and last lines.
            if ((address.mark != '<' && address.mark != '>') || last_visual_.empty()) {
                return ftk::make_error("E20: Mark not set");
            }
            std::size_t first = buffer.line_count();
            std::size_t last = 0;
            for (const VisualEnds& ends : last_visual_) {
                first = std::min({first, ends.anchor.line, ends.caret.line});
                last = std::max({last, ends.anchor.line, ends.caret.line});
            }
            line = static_cast<long>(address.mark == '<' ? first : last) + 1;
            break;
        }
        case ExAddress::Kind::Forward:
        case ExAddress::Kind::Backward: {
            // The first line after the caret's the pattern matches in, or
            // before it, going round; it is the last search then.
            const bool forward = address.kind == ExAddress::Kind::Forward;
            Search search;
            if (address.pattern.empty()) {
                if (!remembered_.search) return ftk::make_error("E35: No previous regular expression");
                search = *remembered_.search;
            } else {
                ftk::Result<Pattern> read = read_pattern(address.pattern, remembered_.settings);
                if (!read) return ftk::make_error(read.error());
                search = Search{.text = address.pattern, .pattern = std::move(*read), .forward = forward};
            }
            Message told;
            const Position from = forward ? Position{current, buffer.line(current).size()} : Position{current, 0};
            const std::optional<Span> found = search_from(buffer, search.pattern, search.text, from, forward, 1, told);
            if (!found) return ftk::make_error(told.text);
            search.forward = forward;
            remembered_.search = std::move(search);
            remembered_.shown = true;
            line = static_cast<long>(found->from.line) + 1;
            break;
        }
    }
    line += address.offset;
    if (line < 0 || line > count) return ftk::make_error("E16: Invalid range");
    return static_cast<std::size_t>(line);
}

ftk::Result<std::pair<std::size_t, std::size_t>> Engine::lines_of(const ExRange& range, bool whole, CommandContext& c) {
    const std::size_t count = c.document.buffer().line_count();
    const std::size_t current = c.selections.primary().caret.line;
    if (range.whole || (whole && !range.from)) return std::pair<std::size_t, std::size_t>{0, count - 1};
    if (!range.from) return std::pair{current, current};
    const ftk::Result<std::size_t> first = line_at(*range.from, current, c);
    if (!first) return std::unexpected(first.error());
    // Line 0 is the first, where a range names it.
    const std::size_t from = *first == 0 ? 0 : *first - 1;
    const ftk::Result<std::size_t> last = range.to ? line_at(*range.to, range.from_first ? from : current, c) : first;
    if (!last) return std::unexpected(last.error());
    const std::size_t to = *last == 0 ? 0 : *last - 1;
    return std::pair{std::min(from, to), std::max(from, to)};
}

void Engine::run_ex(std::string_view text, CommandContext& c) {
    const ftk::Result<ExCommand> read = parse_ex(text);
    if (!read) {
        remembered_.message = {.text = read.error(), .error = true};
        return;
    }
    const ExCommand& command = *read;
    const std::string& name = command.name;
    const ftk::Result<std::pair<std::size_t, std::size_t>> lines = lines_of(command.range, false, c);
    if (!lines) {
        remembered_.message = {.text = lines.error(), .error = true};
        return;
    }
    auto [first, last] = *lines;
    const std::size_t count = c.document.buffer().line_count();
    // Vim's notes for more than two lines.
    const auto say = [&](std::size_t n, std::string_view what) {
        if (n > 2) remembered_.message = {.text = std::format("{} {}", n, what), .error = false};
    };
    // What follows a name: a count, the lines from its range's last on.
    const auto counted = [&](std::string_view argument) {
        while (!argument.empty() && argument.front() == ' ') argument.remove_prefix(1);
        if (argument.empty()) return true;
        if (argument.find_first_not_of("0123456789") != std::string_view::npos) {
            remembered_.message = {.text = std::format("E488: Trailing characters: {}", argument), .error = true};
            return false;
        }
        std::size_t n = 0;
        for (const char digit : argument) n = std::min<std::size_t>(n * 10 + static_cast<std::size_t>(digit - '0'), count);
        if (n == 0) {
            remembered_.message = {.text = "E939: Positive count required", .error = true};
            return false;
        }
        first = last;
        last = std::min(last + n - 1, count - 1);
        return true;
    };
    // Vim's line acts at one caret, the primary's.
    c.selections = Selections(Selection::at(c.selections.primary().caret));
    c.document.history().close();

    if (name.empty()) {
        // A range alone goes to its last line.
        c.selections = Selections(Selection::at(first_non_blank(c.document.buffer(), last)));
        return settle(c);
    }
    if (name == "delete" || name == "yank") {
        std::string_view argument = command.argument;
        char reg = '"';
        if (!argument.empty() && (argument[0] < '0' || argument[0] > '9') && Registers::valid(argument[0])) {
            reg = argument[0];
            argument.remove_prefix(1);
        }
        if (!counted(argument)) return;
        const std::optional<Range> range = Range{.span = {{first, 0}, {last, 0}}, .linewise = true};
        const Selections before = c.selections;
        operate(name == "delete" ? Operator::Delete : Operator::Yank, std::span(&range, 1), reg, c, *registers_);
        if (name == "yank") c.selections = before;
        say(last - first + 1, name == "delete" ? "fewer lines" : "lines yanked");
        return settle(c);
    }
    if (name == ">" || name == "<") {
        // Each > or < after the first one more level.
        std::string_view argument = command.argument;
        std::size_t levels = 1;
        while (!argument.empty() && argument.front() == name[0]) {
            ++levels;
            argument.remove_prefix(1);
        }
        if (!counted(argument)) return;
        const std::optional<Range> range = Range{.span = {{first, 0}, {last, 0}}, .linewise = true};
        operate(name == ">" ? Operator::ShiftRight : Operator::ShiftLeft, std::span(&range, 1), '"', c, *registers_, levels);
        c.selections = Selections(Selection::at(first_non_blank(c.document.buffer(), last)));
        say(last - first + 1, std::format("lines {}ed {} time{}", name, levels, levels == 1 ? "" : "s"));
        return settle(c);
    }
    if (name == "move" || name == "copy" || name == "t") {
        // The line they go below, as a range names one.
        const ftk::Result<ExCommand> target = parse_ex(command.argument);
        if (!target || !target->range.from || target->range.to || target->range.whole || !target->name.empty()) {
            remembered_.message = {.text = "E14: Invalid address", .error = true};
            return;
        }
        const ftk::Result<std::size_t> below = line_at(*target->range.from, c.selections.primary().caret.line, c);
        if (!below) {
            remembered_.message = {.text = below.error(), .error = true};
            return;
        }
        if (name == "move") {
            if (*below > first && *below <= last) {
                remembered_.message = {.text = "E134: Cannot move a range of lines into itself", .error = true};
                return;
            }
            move_lines(first, last, *below, c);
            say(last - first + 1, "lines moved");
        } else {
            copy_lines(first, last, *below, c);
            say(last - first + 1, "more lines");
        }
        return settle(c);
    }
    if (name == "nohlsearch") {
        remembered_.shown = false;
        return;
    }
    remembered_.message = {.text = std::format("Not supported yet: :{}", name), .error = true};
}

void Engine::cancel(CommandContext& context) {
    keys_.clear();
    operator_waits_ = false;
    if (one_command_ && mode_ == Mode::Normal) end_one_command(context);
}

void Engine::completed(std::string_view replaced, std::string_view text) {
    if (mode_ != Mode::Insert) return;
    // Made again as the word's typed part taken back and the entry typed.
    for (std::size_t at = 0; at < replaced.size();) {
        (void)ftk::utf8::next(replaced, at);
        note_typed(Chord{.key = Key::Backspace, .modifiers = {}, .text = {}});
    }
    note_typed(Chord::typed(text));
}

void Engine::begin_insert() {
    keys_.clear();
    operator_waits_ = false;
    typing_.reset();
    preview_.reset();
    register_next_ = false;
    making_.reset();
    inserted_.clear();
    insert_repeats_ = 0;
    quiet_ = true;
    mode_ = Mode::Insert;
}

void Engine::end_insert(CommandContext& context) {
    quiet_ = false;
    making_.reset();
    inserted_.clear();
    register_next_ = false;
    block_insert_ = false;
    overwritten_.clear();
    mode_ = Mode::Normal;
    settle(context);
}

void Engine::note_typed(Chord key) {
    if (repeating_insert_ || quiet_) return;
    if (!replaying_ && !making_) {
        making_ = Repeat{.reg = '"', .count = {}, .keys = {Chord::typed(mode_ == Mode::Replace ? "R" : "i")},
                         .typed = {}, .visual = {}};
    }
    inserted_.push_back(std::move(key));
}

void Engine::end_change_here() {
    if (making_) {
        making_->typed = inserted_;
        last_change_ = std::exchange(making_, std::nullopt);
    }
    inserted_.clear();
}

void Engine::repeat(std::optional<std::size_t> count, CommandContext& c) {
    if (!last_change_) return;
    Repeat change = *last_change_;
    if (count) change.count = count;
    // "1p made again puts "2, then "3, as Vim steps back through deletes.
    if (change.reg >= '1' && change.reg < '9') ++change.reg;
    std::vector<Chord> keys;
    if (change.reg != '"') {
        keys.push_back(Chord::typed("\""));
        keys.push_back(Chord::typed(std::string_view(&change.reg, 1)));
    }
    if (change.count) {
        const std::string digits = std::to_string(*change.count);
        for (const char digit : digits) keys.push_back(Chord::typed(std::string_view(&digit, 1)));
    }
    keys.insert(keys.end(), change.keys.begin(), change.keys.end());
    keys.insert(keys.end(), change.typed.begin(), change.typed.end());

    replaying_ = true;
    if (change.visual) select_again(*change.visual, c);
    for (const Chord& key : keys) dispatch(key, c);
    if (typing(mode_)) dispatch(Chord{.key = Key::Escape, .modifiers = {}, .text = {}}, c);
    replaying_ = false;
    last_change_ = std::move(change);
}

void Engine::select_again(const VisualExtent& extent, CommandContext& c) {
    const Buffer& buffer = c.document.buffer();
    visual_.clear();
    for (const Selection& selection : c.selections.all()) {
        const Position from = selection.caret;
        const std::size_t last = std::min(from.line + extent.lines - 1, buffer.line_count() - 1);
        VisualEnds ends{.anchor = from, .caret = from, .goal = {}};
        if (extent.mode == Mode::VisualLine) {
            ends.caret = on_character(buffer, buffer.clamp({.line = last, .byte = from.byte}));
        } else if (extent.mode == Mode::Visual && extent.lines > 1) {
            ends.caret = on_character(buffer, c.layout.position_at(buffer, last, extent.end));
        } else {
            const float to = c.layout.x_of(buffer, from) + extent.width;
            ends.caret = on_character(buffer, c.layout.position_at(buffer, last, to));
            if (extent.to_line_ends) ends.goal = LINE_END_GOAL;
        }
        visual_.push_back(ends);
    }
    // A block is one, from the primary caret.
    if (extent.mode == Mode::VisualBlock && visual_.size() > 1) {
        visual_ = {visual_[std::min(c.selections.primary_index(), visual_.size() - 1)]};
    }
    mode_ = extent.mode;
    c.document.history().close();
    show_visual(c);
}

Engine::VisualExtent Engine::extent(const CommandContext& context) const {
    if (visual_.empty()) return {.mode = mode_, .lines = 1, .width = 0.0f, .end = 0.0f, .to_line_ends = false};
    const Buffer& buffer = context.document.buffer();
    const VisualEnds& ends = visual_[std::min(context.selections.primary_index(), visual_.size() - 1)];
    const Position first = std::min(ends.anchor, ends.caret);
    const Position last = std::max(ends.anchor, ends.caret);
    const float anchor_x = context.layout.x_of(buffer, ends.anchor);
    const float caret_x = context.layout.x_of(buffer, ends.caret);
    const bool block = mode_ == Mode::VisualBlock;
    return {.mode = mode_,
            .lines = last.line - first.line + 1,
            .width = block ? std::abs(caret_x - anchor_x) : context.layout.x_of(buffer, last) - context.layout.x_of(buffer, first),
            .end = context.layout.x_of(buffer, last),
            .to_line_ends = block && ends.goal && std::isinf(*ends.goal)};
}

void Engine::end_one_command(CommandContext& context) {
    // A caret on its line's last character goes past it when it is still on
    // the line it was at the end of, or the command went to the line's end
    // (`$`).
    one_command_ = false;
    mode_ = resume_;
    const Buffer& buffer = context.document.buffer();
    std::vector<Selection> back;
    for (std::size_t i = 0; i < context.selections.size(); ++i) {
        Selection selection = context.selections.all()[i];
        const std::size_t line = selection.caret.line;
        const bool left_here = i < left_line_ends_.size() && left_line_ends_[i] == line;
        const bool to_end = selection.goal && std::isinf(*selection.goal);
        if ((left_here || to_end) && !buffer.line(line).empty() && selection.caret == last_character(buffer, line)) {
            selection.caret.byte = buffer.line(line).size();
            selection.anchor = selection.caret;
        }
        back.push_back(selection);
    }
    context.selections.set(std::move(back), context.selections.primary_index());
    context.document.history().hold();
}

void Engine::settle(CommandContext& context, bool by_mouse) {
    if (typing(mode_)) {
        if (by_mouse) end_change_here();
        return;
    }
    if (mode_ == Mode::Command) {
        // The mouse leaves the command line, keeping what it made.
        if (!by_mouse) return;
        typing_.reset();
        preview_.reset();
        register_next_ = false;
        mode_ = Mode::Normal;
    }
    const Buffer& buffer = context.document.buffer();
    const std::span<const Selection> all = context.selections.all();
    const bool selecting = std::ranges::any_of(all, [](const Selection& s) { return !s.empty(); });
    if (visual(mode_)) {
        if (context.selections == shown_) return;
        // Carets alone end visual mode; selections become its own.
        if (!selecting) {
            last_visual_ = visual_;
            last_visual_mode_ = mode_;
            mode_ = Mode::Normal;
        } else {
            visual_ = ends_of(buffer, context.selections);
            mode_ = Mode::Visual;
            show_visual(context);
            return;
        }
    } else if (selecting && by_mouse) {
        visual_ = ends_of(buffer, context.selections);
        mode_ = Mode::Visual;
        show_visual(context);
        return;
    }
    // Normal mode: carets on characters, a selection made otherwise than by
    // the mouse a caret at its start.
    std::vector<Selection> settled;
    bool changed = false;
    for (const Selection& selection : all) {
        const Position at = on_character(buffer, selection.empty() ? selection.caret : selection.span().from);
        changed = changed || !selection.empty() || at != selection.caret;
        settled.push_back({.anchor = at, .caret = at, .goal = selection.goal});
    }
    if (changed) context.selections.set(std::move(settled), context.selections.primary_index());
}

CaretShape Engine::caret_shape() const {
    if (mode_ == Mode::Insert || mode_ == Mode::Command) return CaretShape::Bar;
    if (mode_ == Mode::Replace) return CaretShape::Underline;
    return operator_waits_ ? CaretShape::HalfBlock : CaretShape::Block;
}

std::vector<Position> Engine::cursors() const {
    std::vector<Position> out;
    if (!visual(mode_)) return out;
    for (const VisualEnds& ends : visual_) {
        out.push_back(ends.caret);
        if (mode_ == Mode::VisualBlock) break;
    }
    return out;
}

std::string Engine::pending() const {
    if (register_next_) return "<C-r>";
    std::string out;
    for (const Chord& key : keys_) out += notation(key);
    return out;
}

bool Engine::wants_character() const {
    if (register_next_) return true;
    if (mode_ == Mode::Insert || mode_ == Mode::Replace || keys_.empty()) return false;
    const std::string& last = keys_.back().text;
    if (last != "f" && last != "t" && last != "F" && last != "T" && last != "r" && last != "\"") return false;
    Command command;
    return (visual(mode_) ? parse_visual(keys_, command) : parse(keys_, command)) == Parsed::Waiting;
}

void Engine::take_normal(const Chord& key, CommandContext& c) {
    keys_.push_back(key);
    Command command;
    const Parsed parsed = parse(keys_, command);
    operator_waits_ = parsed == Parsed::Waiting && !command.op.empty();
    if (parsed == Parsed::Waiting) return;
    keys_.clear();
    if (parsed == Parsed::Invalid) return;
    // The command line, whose search is the motion: the keys before it (a
    // register, a count, an operator) come again with the search.
    if (command.name == "/" || command.name == "?") {
        return begin_command_line(command.name[0], keys_before(command), !command.op.empty(), c);
    }
    // The `:` line, a count before it the lines from the caret's on.
    if (command.name == ":" && command.op.empty()) {
        std::string lines;
        if (command.count) lines = *command.count == 1 ? "." : std::format(".,.+{}", *command.count - 1);
        return begin_command_line(':', {}, false, c, std::move(lines));
    }
    // A change noted for `.`, as its keys spell it.
    if (!replaying_ && changes(command)) {
        making_ = Repeat{.reg = command.reg, .count = count_of(command), .keys = keys_of(command), .typed = {},
                         .visual = {}};
    }

    // An operator over what `acting` names, into insert mode after a change
    // that began it. One that changed nothing (its motion couldn't go)
    // leaves the last change as the one `.` makes.
    const auto operate_on = [&](const Command& acting) {
        const auto version = c.document.version();
        if (run_operator(acting, c, *registers_, remembered_)) {
            mode_ = Mode::Insert;
            return;
        }
        if (c.document.version() == version) making_.reset();
        settle(c);
    };
    if (!command.op.empty()) return operate_on(command);

    const Buffer& buffer = c.document.buffer();
    const std::string_view name = command.name;
    const std::size_t n = command.count.value_or(1);

    // The small changes that are an operator over a motion: x is dl, s cl,
    // S cc, C c$, D d$, and Y y$ (as Neovim has it).
    struct Shorthand {
        std::string_view name;
        std::string_view op;
        std::string_view motion;
    };
    static constexpr Shorthand SHORTHANDS[] = {
        {"x", "d", "l"}, {"<Del>", "d", "l"}, {"X", "d", "h"}, {"s", "c", "l"}, {"S", "c", "line"},
        {"C", "c", "$"}, {"D", "d", "$"},     {"Y", "y", "$"},
    };
    for (const Shorthand& shorthand : SHORTHANDS) {
        if (shorthand.name != name) continue;
        Command expanded = command;
        expanded.op = shorthand.op;
        expanded.name = shorthand.motion;
        return operate_on(expanded);
    }
    // The other small changes, each a step of its own.
    const auto change = [&](auto&& make) {
        c.document.history().close();
        make();
        settle(c);
    };
    if (name == "p" || name == "P") {
        if (const std::optional<Yanked> yanked = registers_->get(command.reg, c.clipboard)) {
            change([&] { put(*yanked, name == "p", n, c); });
        }
        return;
    }
    if (name == "J") return change([&] { join_lines(n, c); });
    if (name == "r") return change([&] { (void)replace_characters(command.argument, n, c); });
    if (name == "~") return change([&] { toggle_case_characters(n, c); });
    if (name == "<C-a>" || name == "<C-x>") {
        const auto amount = static_cast<long long>(n);
        return change([&] { (void)add_to_number(name == "<C-a>" ? amount : -amount, c); });
    }

    // Into insert mode with each caret where `place` puts it; what is typed
    // there is one step of the history with what began it, and a count
    // types it that many times.
    const auto insert = [&](auto place) {
        std::vector<Selection> placed;
        for (const Selection& selection : c.selections.all()) placed.push_back(Selection::at(place(selection.caret)));
        c.selections.set(std::move(placed), c.selections.primary_index());
        c.document.history().hold();
        mode_ = Mode::Insert;
        insert_repeats_ = n - 1;
        insert_opens_lines_ = false;
    };

    if (escape(key)) {
        // Several carets give way to the primary.
        if (c.selections.size() > 1) c.selections.keep_primary();
        return;
    }
    if (name == "v") return enter_visual(Mode::Visual, c);
    if (name == "V") return enter_visual(Mode::VisualLine, c);
    if (name == "<C-v>") return enter_visual(Mode::VisualBlock, c);
    if (name == "gv") {
        // The last visual selection again, its ends kept inside the text.
        if (last_visual_.empty()) return;
        visual_ = last_visual_;
        for (VisualEnds& ends : visual_) {
            ends.anchor = on_character(buffer, buffer.clamp(ends.anchor));
            ends.caret = on_character(buffer, buffer.clamp(ends.caret));
        }
        mode_ = last_visual_mode_;
        show_visual(c);
        return;
    }
    if (name == "i" || name == "<Insert>") return insert([](Position at) { return at; });
    if (name == "a") return insert([&](Position at) { return right_in_line(buffer, at, 1); });
    if (name == "I") return insert([&](Position at) { return first_non_blank(buffer, at.line); });
    if (name == "A") return insert([&](Position at) { return Position{.line = at.line, .byte = buffer.line(at.line).size()}; });
    if (name == "o" || name == "O") {
        c.document.history().hold();
        (void)run_command(name == "o" ? "edit.insert_line_below" : "edit.insert_line_above", c);
        mode_ = Mode::Insert;
        insert_repeats_ = n - 1;
        insert_opens_lines_ = true;
        return;
    }
    if (name == "R") {
        c.document.history().hold();
        overwritten_.clear();
        mode_ = Mode::Replace;
        insert_repeats_ = n - 1;
        insert_opens_lines_ = false;
        return;
    }
    if (name == "u" || name == "<C-r>") {
        for (std::size_t i = 0; i < n; ++i) (void)run_command(name == "u" ? "edit.undo" : "edit.redo", c);
        settle(c);
        return;
    }
    if (name == ".") return repeat(command.count, c);
    // Ctrl-L hides the search's matches until the next search.
    if (name == "<C-l>") {
        remembered_.shown = false;
        return;
    }
    // The view scrolled, the carets with it.
    if (std::optional<std::vector<Selection>> scrolled = scroll_view(c, name, command.count, c.selections.all(), half_page_)) {
        c.selections.set(std::move(*scrolled), c.selections.primary_index());
        c.document.history().close();
        settle(c);
        return;
    }
    // The folds at the carets.
    struct FoldKey {
        std::string_view key;
        std::string_view command;
    };
    static constexpr FoldKey FOLD_KEYS[] = {
        {"zo", "fold.open"}, {"zc", "fold.close"}, {"za", "fold.toggle"}, {"zR", "fold.open_all"}, {"zM", "fold.close_all"},
    };
    for (const FoldKey& fold : FOLD_KEYS) {
        if (fold.key != name) continue;
        (void)run_command(fold.command, c);
        settle(c);
        return;
    }

    std::vector<Selection> moved;
    bool any = false;
    for (const Selection& selection : c.selections.all()) {
        if (const std::optional<Went> went = motion(c, command, selection, remembered_)) {
            moved.push_back({.anchor = went->to, .caret = went->to, .goal = went->goal});
            any = true;
        } else {
            moved.push_back(selection);
        }
    }
    if (!any) return;
    c.selections.set(std::move(moved), c.selections.primary_index());
    c.document.history().close();
    settle(c);
}

void Engine::repeat_insert(CommandContext& c) {
    const std::size_t repeats = std::exchange(insert_repeats_, 0);
    if (repeats == 0 || inserted_.empty()) return;
    const std::vector<Chord> typed = inserted_;
    repeating_insert_ = true;
    for (std::size_t i = 0; i < repeats; ++i) {
        if (insert_opens_lines_) (void)run_command("edit.insert_line_below", c);
        for (const Chord& key : typed) dispatch(key, c);
    }
    repeating_insert_ = false;
}

void Engine::leave_insert(CommandContext& c) {
    repeat_insert(c);
    if (making_) making_->typed = inserted_;
    c.document.history().close();
    std::vector<Selection> back;
    for (const Selection& selection : c.selections.all()) {
        back.push_back(Selection::at(left_in_line(c.document.buffer(), selection.caret, 1)));
    }
    c.selections.set(std::move(back), c.selections.primary_index());
    // A block's insert, made at each of its lines, leaves the caret on the
    // first.
    if (block_insert_) c.selections = Selections(c.selections.all().front());
    block_insert_ = false;
    overwritten_.clear();
    mode_ = Mode::Normal;
    settle(c);
}

void Engine::type_text(std::string_view text, CommandContext& c) {
    if (mode_ != Mode::Replace) {
        CommandContext typed = c;
        typed.text = text;
        (void)run_command("edit.type", typed);
        return;
    }
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t from = at;
        (void)ftk::utf8::next(text, at);
        const std::string_view character = text.substr(from, at - from);
        if (character == "\n") {
            // As Enter does, the break leaves Backspace nothing of before
            // to put back.
            CommandContext typed = c;
            typed.text = character;
            (void)run_command("edit.type", typed);
            overwritten_.clear();
        } else {
            overtype(character, c, overwritten_);
        }
    }
}

void Engine::take_replace(const Chord& key, CommandContext& c) {
    // A register's name after Ctrl-R, which take_insert reads.
    if (register_next_) return take_insert(key, c);
    if (escape(key)) return leave_insert(c);
    if (!key.text.empty()) {
        note_typed(key);
        return type_text(key.text, c);
    }
    if (pressed(key, Key::Backspace)) {
        note_typed(key);
        return take_back_overtyped(c, overwritten_);
    }
    // Enter breaks the line, and the other keys edit or move as in insert
    // mode; after them Backspace has nothing of before to put back.
    overwritten_.clear();
    take_insert(key, c);
}

void Engine::take_insert(const Chord& key, CommandContext& c) {
    if (register_next_) {
        // Ctrl-R's register: its text typed; any other key goes on.
        register_next_ = false;
        if (key.text.size() == 1) {
            if (const std::optional<Yanked> yanked = registers_->get(key.text[0], c.clipboard)) {
                // `.` types the text itself again, whatever the register
                // holds by then.
                const std::string text = yanked->text();
                note_typed(Chord::typed(text));
                type_text(text, c);
            }
        }
        return;
    }
    if (escape(key)) return leave_insert(c);
    if (!key.text.empty()) {
        note_typed(key);
        return type_text(key.text, c);
    }
    const std::string named = notation(key);
    if (named == "<C-r>") {
        register_next_ = true;
        return;
    }
    if (named == "<C-o>") {
        // One command in normal mode, the insert so far a step of its own
        // and a change of its own, its count done.
        repeat_insert(c);
        end_change_here();
        left_line_ends_.clear();
        for (const Selection& selection : c.selections.all()) {
            const std::size_t line = selection.caret.line;
            const bool at_end = !c.document.buffer().line(line).empty() &&
                                selection.caret.byte >= c.document.buffer().line(line).size();
            left_line_ends_.push_back(at_end ? std::optional<std::size_t>(line) : std::nullopt);
        }
        c.document.history().close();
        resume_ = mode_;
        mode_ = Mode::Normal;
        one_command_ = true;
        block_insert_ = false;
        overwritten_.clear();
        settle(c);
        return;
    }
    if (named == "<C-u>") {
        note_typed(key);
        return delete_before_on_line(c);
    }
    if (named == "<C-t>" || named == "<C-d>") {
        note_typed(key);
        return shift_caret_lines(named == "<C-t>", c);
    }
    // The keys that edit, and those that move the caret, which ends the
    // change being made there.
    struct InsertKey {
        std::string_view key;
        std::string_view command;
        bool moves;
    };
    static constexpr InsertKey EDITS[] = {
        {"<CR>", "edit.newline", false},          {"<BS>", "edit.backspace", false},
        {"<Del>", "edit.delete", false},          {"<Tab>", "edit.tab", false},
        {"<S-Tab>", "edit.outdent", false},       {"<C-w>", "edit.delete_word_left", false},
        {"<Left>", "caret.left", true},           {"<Right>", "caret.right", true},
        {"<Up>", "caret.up", true},               {"<Down>", "caret.down", true},
        {"<Home>", "caret.line_start", true},     {"<End>", "caret.line_end", true},
        {"<C-Left>", "caret.word_left", true},    {"<C-Right>", "caret.word_right", true},
    };
    for (const InsertKey& edit : EDITS) {
        if (edit.key != named) continue;
        if (edit.moves) {
            end_change_here();
        } else {
            note_typed(key);
        }
        (void)run_command(edit.command, c);
        return;
    }
}

std::vector<Engine::VisualEnds> Engine::ends_of(const Buffer& buffer, const Selections& selections) {
    std::vector<VisualEnds> ends;
    for (const Selection& selection : selections.all()) {
        if (selection.empty()) {
            ends.push_back({.anchor = selection.caret, .caret = selection.caret, .goal = selection.goal});
        } else if (selection.anchor < selection.caret) {
            ends.push_back({.anchor = selection.anchor, .caret = before(buffer, selection.caret), .goal = selection.goal});
        } else {
            ends.push_back({.anchor = before(buffer, selection.anchor), .caret = selection.caret, .goal = selection.goal});
        }
    }
    return ends;
}

void Engine::enter_visual(Mode mode, CommandContext& context) {
    if (!visual(mode_)) {
        visual_.clear();
        for (const Selection& selection : context.selections.all()) {
            visual_.push_back({.anchor = selection.caret, .caret = selection.caret, .goal = selection.goal});
        }
    }
    // A block is one, the primary caret's.
    if (mode == Mode::VisualBlock && visual_.size() > 1) {
        const std::size_t primary = std::min(context.selections.primary_index(), visual_.size() - 1);
        visual_ = {visual_[primary]};
    }
    mode_ = mode;
    context.document.history().close();
    show_visual(context);
}

void Engine::show_visual(CommandContext& context) {
    const Buffer& buffer = context.document.buffer();
    if (mode_ == Mode::VisualBlock) {
        // From the left corner's left to the right corner's right, or on to
        // each line's end after $.
        const VisualEnds& ends = visual_.front();
        const bool to_line_ends = ends.goal && std::isinf(*ends.goal);
        const float anchor_x = context.layout.x_of(buffer, ends.anchor);
        const float caret_x = to_line_ends ? LINE_END_GOAL : context.layout.x_of(buffer, ends.caret);
        const bool rightward = anchor_x <= caret_x;
        const float from = rightward ? anchor_x : anchor_x + cell_width(context.layout, buffer, ends.anchor);
        const float to = !rightward || to_line_ends ? caret_x : caret_x + cell_width(context.layout, buffer, ends.caret);
        context.selections = column_selection(buffer, context.layout, ends.anchor.line, from, ends.caret.line, to);
        shown_ = context.selections;
        return;
    }
    std::vector<Selection> made;
    for (const VisualEnds& ends : visual_) {
        if (mode_ == Mode::Visual) {
            made.push_back(ends.anchor <= ends.caret
                               ? Selection{.anchor = ends.anchor, .caret = past(buffer, ends.caret), .goal = ends.goal}
                               : Selection{.anchor = past(buffer, ends.anchor), .caret = ends.caret, .goal = ends.goal});
            continue;
        }
        // Whole lines, with their last one's break.
        const std::size_t first = std::min(ends.anchor.line, ends.caret.line);
        const std::size_t last = std::max(ends.anchor.line, ends.caret.line);
        const Position start{.line = first, .byte = 0};
        const Position end = last + 1 < buffer.line_count() ? Position{.line = last + 1, .byte = 0}
                                                             : Position{.line = last, .byte = buffer.line(last).size()};
        made.push_back(ends.anchor.line <= ends.caret.line ? Selection{.anchor = start, .caret = end, .goal = ends.goal}
                                                            : Selection{.anchor = end, .caret = start, .goal = ends.goal});
    }
    const std::size_t primary = std::min(context.selections.primary_index(), made.size() - 1);
    context.selections.set(std::move(made), primary);
    // Selections that came to overlap were made one: their ends are theirs.
    if (context.selections.size() != visual_.size()) visual_ = ends_of(buffer, context.selections);
    shown_ = context.selections;
}

void Engine::leave_visual(CommandContext& context) {
    last_visual_ = visual_;
    last_visual_mode_ = mode_;
    std::vector<Selection> carets;
    for (const VisualEnds& ends : visual_) {
        carets.push_back({.anchor = ends.caret, .caret = ends.caret, .goal = ends.goal});
        if (mode_ == Mode::VisualBlock) break;
    }
    const std::size_t primary = std::min(context.selections.primary_index(), carets.size() - 1);
    mode_ = Mode::Normal;
    context.selections.set(std::move(carets), primary);
    settle(context);
}

void Engine::take_visual(const Chord& key, CommandContext& c) {
    keys_.push_back(key);
    Command command;
    const Parsed parsed = parse_visual(keys_, command);
    if (parsed == Parsed::Waiting) return;
    keys_.clear();
    if (parsed == Parsed::Invalid) return;
    const Buffer& buffer = c.document.buffer();
    const std::string_view name = command.name;
    const std::size_t n = command.count.value_or(1);

    if (escape(key)) return leave_visual(c);
    // The command line, whose search moves the caret end; Ctrl-L as in
    // normal mode.
    if (name == "/" || name == "?") return begin_command_line(name[0], keys_before(command), false, c);
    // `:` from visual mode: the lines it selected, as '<,'>.
    if (name == ":") {
        leave_visual(c);
        return begin_command_line(':', {}, false, c, "'<,'>");
    }
    if (name == "<C-l>") {
        remembered_.shown = false;
        return;
    }
    // v, V and Ctrl-V: that visual mode, or out of it from itself.
    const std::optional<Mode> kind = name == "v"       ? std::optional(Mode::Visual)
                                     : name == "V"     ? std::optional(Mode::VisualLine)
                                     : name == "<C-v>" ? std::optional(Mode::VisualBlock)
                                                       : std::nullopt;
    if (kind) {
        if (*kind == mode_) return leave_visual(c);
        return enter_visual(*kind, c);
    }
    if (name == "o" || name == "O") {
        for (VisualEnds& ends : visual_) {
            std::swap(ends.anchor, ends.caret);
            ends.goal.reset();
        }
        return show_visual(c);
    }
    // A text object: each selection becomes it, whole lines for ip and ap.
    if (name.size() == 2 && (name[0] == 'i' || name[0] == 'a')) {
        for (VisualEnds& ends : visual_) {
            const std::optional<Taken> taken = text_object(buffer, c.document.syntax(), ends.caret, name, n);
            if (!taken) continue;
            if (taken->linewise && mode_ == Mode::Visual) mode_ = Mode::VisualLine;
            ends.anchor = taken->span.from;
            ends.caret = taken->linewise ? Position{.line = taken->span.to.line, .byte = 0} : before(buffer, taken->span.to);
            ends.goal.reset();
        }
        return show_visual(c);
    }
    // A change is noted for `.` with how much the selection takes.
    std::optional<Repeat> change;
    if (!replaying_ && name != "y" && name != "Y") {
        change = Repeat{.reg = command.reg, .count = command.count, .keys = keys_of(command), .typed = {},
                        .visual = extent(c)};
    }
    if (act_on_visual(name, command.argument, n, command.reg, c)) {
        making_ = std::move(change);
        return;
    }

    // The view scrolled, each selection's caret end with it.
    std::vector<Selection> caret_ends;
    for (const VisualEnds& ends : visual_) caret_ends.push_back({.anchor = ends.caret, .caret = ends.caret, .goal = ends.goal});
    if (const std::optional<std::vector<Selection>> scrolled = scroll_view(c, name, command.count, caret_ends, half_page_)) {
        for (std::size_t i = 0; i < visual_.size(); ++i) {
            visual_[i].caret = on_character(buffer, (*scrolled)[i].caret);
            visual_[i].goal = (*scrolled)[i].goal;
        }
        return show_visual(c);
    }

    // A motion moves each selection's caret end.
    bool any = false;
    for (VisualEnds& ends : visual_) {
        const Selection at{.anchor = ends.caret, .caret = ends.caret, .goal = ends.goal};
        if (const std::optional<Went> went = motion(c, command, at, remembered_)) {
            ends.caret = on_character(buffer, went->to);
            ends.goal = went->goal;
            any = true;
        }
    }
    if (any) show_visual(c);
}

bool Engine::act_on_visual(std::string_view name, std::string_view argument, std::size_t count, char reg,
                           CommandContext& c) {
    const Buffer& buffer = c.document.buffer();
    const bool block = mode_ == Mode::VisualBlock;
    const bool by_lines = mode_ == Mode::VisualLine;

    // The ranges the selections give an operator: their text; whole lines
    // in visual line mode or with `lines`; on to each line's end with
    // `to_line_ends`, as a block's X, D, Y and C take.
    const auto ranges_of = [&](bool lines, bool to_line_ends) {
        std::vector<std::optional<Range>> ranges;
        for (const Selection& selection : c.selections.all()) {
            const Span span = selection.span();
            if (to_line_ends) {
                const Position end{.line = span.from.line, .byte = buffer.line(span.from.line).size()};
                ranges.push_back(Range{.span = {span.from, end}, .linewise = false});
            } else if (lines || by_lines) {
                const bool ends_at_line_start = span.to.byte == 0 && span.to.line > span.from.line;
                const std::size_t last = ends_at_line_start ? span.to.line - 1 : span.to.line;
                ranges.push_back(Range{.span = {span.from, {last, 0}}, .linewise = true});
            } else {
                ranges.push_back(Range{.span = span, .linewise = false});
            }
        }
        return ranges;
    };
    // Out of visual mode for a change, its selection kept for gv.
    const auto leave_for_change = [&] {
        last_visual_ = visual_;
        last_visual_mode_ = mode_;
        mode_ = Mode::Normal;
        c.document.history().close();
    };
    // After a block's change, one caret at its top left.
    const auto finish = [&] {
        if (block && c.selections.size() > 1) c.selections = Selections(c.selections.all().front());
        settle(c);
    };

    struct Acting {
        std::string_view name;
        Operator op;
        bool lines;
        bool block_to_line_ends;
    };
    static constexpr Acting ACTING[] = {
        {"d", Operator::Delete, false, false},      {"x", Operator::Delete, false, false},
        {"<Del>", Operator::Delete, false, false},  {"X", Operator::Delete, true, true},
        {"D", Operator::Delete, true, true},        {"c", Operator::Change, false, false},
        {"s", Operator::Change, false, false},      {"S", Operator::Change, true, false},
        {"R", Operator::Change, true, false},       {"C", Operator::Change, true, true},
        {"y", Operator::Yank, false, false},        {"Y", Operator::Yank, true, true},
        {">", Operator::ShiftRight, true, false},   {"<lt>", Operator::ShiftLeft, true, false},
        {"=", Operator::Indent, true, false},       {"~", Operator::ToggleCase, false, false},
        {"g~", Operator::ToggleCase, false, false}, {"u", Operator::Lower, false, false},
        {"gu", Operator::Lower, false, false},      {"U", Operator::Upper, false, false},
        {"gU", Operator::Upper, false, false},
    };
    for (const Acting& acting : ACTING) {
        if (acting.name != name) continue;
        const std::vector<std::optional<Range>> ranges = ranges_of(acting.lines, block && acting.block_to_line_ends);
        leave_for_change();
        // A count shifts that many levels.
        const bool shifting = acting.op == Operator::ShiftRight || acting.op == Operator::ShiftLeft;
        operate(acting.op, ranges, reg, c, *registers_, shifting ? count : 1);
        if (acting.op == Operator::Change) {
            mode_ = Mode::Insert;
            block_insert_ = block;
        } else {
            finish();
        }
        return true;
    }
    if (name == "J") {
        // Each selection's lines joined, two at least.
        const std::vector<std::optional<Range>> ranges = ranges_of(true, false);
        const std::size_t primary = c.selections.primary_index();
        const Range& primary_range = *ranges[primary];
        const std::size_t lines =
            std::max<std::size_t>(primary_range.span.to.line - primary_range.span.from.line + 1, 2);
        leave_for_change();
        std::vector<Selection> starts;
        for (const std::optional<Range>& range : ranges) starts.push_back(Selection::at({range->span.from.line, 0}));
        c.selections.set(std::move(starts), primary);
        join_lines(lines, c);
        finish();
        return true;
    }
    if (name == "r") {
        const std::vector<std::optional<Range>> ranges = ranges_of(false, false);
        leave_for_change();
        replace_in(ranges, argument, c);
        finish();
        return true;
    }
    if (name == "p" || name == "P") {
        const std::optional<Yanked> yanked = registers_->get(reg, c.clipboard);
        if (!yanked) return true;
        const std::vector<std::optional<Range>> ranges = ranges_of(false, false);
        leave_for_change();
        // The selection gone and the register's text in its place, one step;
        // p leaves what it replaced in the unnamed register, P doesn't.
        c.document.history().hold();
        operate(Operator::Delete, ranges, name == "p" ? '"' : '_', c, *registers_);
        put(*yanked, false, count, c);
        c.document.history().close();
        finish();
        return true;
    }
    if (name == "I" || name == "A") {
        // Insert at each selection's start or end: at every line of a block.
        std::vector<Selection> carets;
        for (const Selection& selection : c.selections.all()) {
            const Span span = selection.span();
            Position at = name == "I" ? span.from : span.to;
            // A selection of lines ends after its last line's break: before it.
            if (name == "A" && at.byte == 0 && at.line > span.from.line) at = {at.line - 1, buffer.line(at.line - 1).size()};
            carets.push_back(Selection::at(at));
        }
        const std::size_t primary = c.selections.primary_index();
        last_visual_ = visual_;
        last_visual_mode_ = mode_;
        mode_ = Mode::Insert;
        block_insert_ = block;
        c.selections.set(std::move(carets), primary);
        c.document.history().hold();
        return true;
    }
    return false;
}

} // namespace fjedit::vim
