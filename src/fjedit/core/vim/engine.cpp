#include "fjedit/core/vim/engine.hpp"

#include "fjedit/core/vim/notation.hpp"
#include "fjedit/core/vim/operators.hpp"
#include "fjedit/core/vim/text_objects.hpp"
#include "fjedit/core/vim/walk.hpp"

#include <algorithm>
#include <limits>
#include <span>
#include <string_view>

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
    const bool prefix = key.text == "g";
    const bool takes_character = key.text == "f" || key.text == "t" || key.text == "F" || key.text == "T" ||
                                 (key.text == "r" && command.op.empty());
    if (!prefix && !takes_character) {
        command.name = notation(key);
        return ends_at(keys, i + 1);
    }
    if (i + 1 == keys.size()) return Parsed::Waiting;
    const Chord& next = keys[i + 1];
    if (prefix) {
        if (next.text.empty()) return Parsed::Invalid;
        command.name = key.text + next.text;
    } else {
        command.argument = argument_of(next);
        if (command.argument.empty()) return Parsed::Invalid;
        command.name = key.text;
    }
    return ends_at(keys, i + 2);
}

// What `keys`, typed so far, come to.
Parsed parse(std::span<const Chord> keys, Command& command) {
    std::size_t i = 0;
    if (keys[i].text == "\"") {
        if (i + 1 == keys.size()) return Parsed::Waiting;
        const std::string& name = keys[i + 1].text;
        if (name.size() != 1 || !Registers::valid(name[0])) return Parsed::Invalid;
        command.reg = name[0];
        i += 2;
    }
    read_count(keys, i, command.count);
    if (i == keys.size()) return Parsed::Waiting;

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

// Where the motion `command` takes `selection`'s caret; nothing for a
// command that isn't a motion, or a motion that can't go. f, t, F and T
// keep what they look for in `search`, which ; and , look for again.
std::optional<Went> motion(const CommandContext& c, const Command& command, const Selection& selection,
                           std::optional<CharSearch>& search) {
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

    const bool finding = name == "f" || name == "t" || name == "F" || name == "T";
    if (!finding && name != ";" && name != ",") return std::nullopt;
    if (finding) {
        search = CharSearch{.target = command.argument, .forward = name == "f" || name == "t", .till = name == "t" || name == "T"};
    }
    if (!search) return std::nullopt;
    CharSearch looking = *search;
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
bool run_operator(const Command& command, CommandContext& c, Registers& registers, std::optional<CharSearch>& search) {
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
        } else if (const std::optional<Went> went = motion(c, moving, selection, search)) {
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

} // namespace

std::optional<std::string> Engine::take(const Chord& key, CommandContext& context) {
    if (mode_ == Mode::Insert) {
        take_insert(key, context);
    } else {
        take_normal(key, context);
    }
    return std::nullopt;
}

void Engine::settle(CommandContext& context) const {
    if (mode_ != Mode::Normal) return;
    const Buffer& buffer = context.document.buffer();
    std::vector<Selection> settled;
    bool changed = false;
    for (const Selection& selection : context.selections.all()) {
        if (!selection.empty()) {
            settled.push_back(selection);
            continue;
        }
        const Position at = on_character(buffer, selection.caret);
        changed = changed || at != selection.caret;
        settled.push_back({.anchor = at, .caret = at, .goal = selection.goal});
    }
    if (changed) context.selections.set(std::move(settled), context.selections.primary_index());
}

CaretShape Engine::caret_shape() const {
    if (mode_ == Mode::Insert) return CaretShape::Bar;
    return operator_waits_ ? CaretShape::HalfBlock : CaretShape::Block;
}

std::string Engine::pending() const {
    std::string out;
    for (const Chord& key : keys_) out += notation(key);
    return out;
}

bool Engine::wants_character() const {
    if (mode_ != Mode::Normal || keys_.empty()) return false;
    const std::string& last = keys_.back().text;
    if (last != "f" && last != "t" && last != "F" && last != "T" && last != "r" && last != "\"") return false;
    Command command;
    return parse(keys_, command) == Parsed::Waiting;
}

void Engine::take_normal(const Chord& key, CommandContext& c) {
    keys_.push_back(key);
    Command command;
    const Parsed parsed = parse(keys_, command);
    operator_waits_ = parsed == Parsed::Waiting && !command.op.empty();
    if (parsed == Parsed::Waiting) return;
    keys_.clear();
    if (parsed == Parsed::Invalid) return;

    if (!command.op.empty()) {
        if (run_operator(command, c, *registers_, search_)) {
            mode_ = Mode::Insert;
        } else {
            settle(c);
        }
        return;
    }

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
        if (run_operator(expanded, c, *registers_, search_)) {
            mode_ = Mode::Insert;
        } else {
            settle(c);
        }
        return;
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
    // there is one step of the history with what began it.
    const auto insert = [&](auto place) {
        std::vector<Selection> placed;
        for (const Selection& selection : c.selections.all()) placed.push_back(Selection::at(place(selection.caret)));
        c.selections.set(std::move(placed), c.selections.primary_index());
        c.document.history().hold();
        mode_ = Mode::Insert;
    };

    if (escape(key)) {
        // Several carets give way to the primary.
        if (c.selections.size() > 1) c.selections.keep_primary();
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
        return;
    }
    if (name == "u" || name == "<C-r>") {
        for (std::size_t i = 0; i < n; ++i) (void)run_command(name == "u" ? "edit.undo" : "edit.redo", c);
        settle(c);
        return;
    }

    std::vector<Selection> moved;
    bool any = false;
    for (const Selection& selection : c.selections.all()) {
        if (const std::optional<Went> went = motion(c, command, selection, search_)) {
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

void Engine::take_insert(const Chord& key, CommandContext& c) {
    if (escape(key)) {
        // Back to normal mode, each caret onto the character before it.
        c.document.history().close();
        std::vector<Selection> back;
        for (const Selection& selection : c.selections.all()) {
            back.push_back(Selection::at(left_in_line(c.document.buffer(), selection.caret, 1)));
        }
        c.selections.set(std::move(back), c.selections.primary_index());
        mode_ = Mode::Normal;
        settle(c);
        return;
    }
    if (!key.text.empty()) {
        CommandContext typed = c;
        typed.text = key.text;
        (void)run_command("edit.type", typed);
        return;
    }
    struct InsertKey {
        std::string_view key;
        std::string_view command;
    };
    static constexpr InsertKey EDITS[] = {
        {"<CR>", "edit.newline"},      {"<BS>", "edit.backspace"},     {"<Del>", "edit.delete"},
        {"<Tab>", "edit.tab"},         {"<S-Tab>", "edit.outdent"},    {"<Left>", "caret.left"},
        {"<Right>", "caret.right"},    {"<Up>", "caret.up"},           {"<Down>", "caret.down"},
        {"<Home>", "caret.line_start"}, {"<End>", "caret.line_end"},   {"<C-Left>", "caret.word_left"},
        {"<C-Right>", "caret.word_right"},
    };
    const std::string name = notation(key);
    for (const InsertKey& edit : EDITS) {
        if (edit.key == name) {
            (void)run_command(edit.command, c);
            return;
        }
    }
}

} // namespace fjedit::vim
