#include "fjedit/core/commands.hpp"

#include "fjedit/core/motion.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace fjedit {

namespace {

// ── Moving and extending ────────────────────────────────────────────────────

// A motion that needs only the text.
using Motion = Position (*)(const Buffer&, Position);

// Every selection's caret moved by `motion`; with `extend`, its anchor stays.
// A caret move ends the step typing was in.
void move(CommandContext& c, Motion motion, bool extend) {
    std::vector<Selection> moved;
    moved.reserve(c.selections.size());
    for (const Selection& selection : c.selections.all()) {
        const Position caret = motion(c.buffer, selection.caret);
        moved.push_back({.anchor = extend ? selection.anchor : caret, .caret = caret});
    }
    c.selections.set(std::move(moved), c.selections.primary_index());
    c.history.close();
}

// One character left or right. Without extending, a selection gives way to
// its own end first, as a caret leaving it on that side.
void step(CommandContext& c, bool right, bool extend) {
    std::vector<Selection> moved;
    moved.reserve(c.selections.size());
    for (const Selection& selection : c.selections.all()) {
        Position caret;
        if (!extend && !selection.empty()) {
            caret = right ? selection.span().to : selection.span().from;
        } else {
            caret = right ? char_right(c.buffer, selection.caret) : char_left(c.buffer, selection.caret);
        }
        moved.push_back({.anchor = extend ? selection.anchor : caret, .caret = caret});
    }
    c.selections.set(std::move(moved), c.selections.primary_index());
    c.history.close();
}

// `lines` lines up or down, each caret keeping to its goal along the line.
// Without extending, a selection moves on from its end on that side.
void vertical(CommandContext& c, bool down, std::size_t lines, bool extend) {
    std::vector<Selection> moved;
    moved.reserve(c.selections.size());
    for (const Selection& selection : c.selections.all()) {
        const Position from = extend || selection.empty() ? selection.caret
                              : down                     ? selection.span().to
                                                         : selection.span().from;
        const float goal = selection.goal.value_or(c.layout.x_of(c.buffer, from));
        const Position caret = down ? lines_down(c.buffer, c.layout, from, goal, lines)
                                    : lines_up(c.buffer, c.layout, from, goal, lines);
        moved.push_back({.anchor = extend ? selection.anchor : caret, .caret = caret, .goal = goal});
    }
    c.selections.set(std::move(moved), c.selections.primary_index());
    c.history.close();
}

Position text_start(const Buffer&, Position) {
    return {};
}

Position text_end(const Buffer& buffer, Position) {
    return buffer.end();
}

// ── Editing ─────────────────────────────────────────────────────────────────

// Makes `edit`, sets the selections `after` gives once it is made, one for
// each selection there was and in their order, and records the step.
template <typename After>
void commit(CommandContext& c, Edit edit, EditKind kind, After after) {
    if (edit.changes.empty()) return;
    const Selections before = c.selections;
    Edit undo = c.buffer.apply(edit);
    c.selections.set(after(edit), before.primary_index());
    c.history.record({.made = std::move(edit), .undo = std::move(undo), .before = before,
                      .after = c.selections, .kind = kind});
}

// Each selection replaced by its own text from `texts`, a caret after it.
void replace_each(CommandContext& c, const std::vector<std::string>& texts, EditKind kind) {
    Edit edit;
    std::size_t i = 0;
    for (const Selection& selection : c.selections.all()) edit.changes.push_back({selection.span(), texts[i++]});
    commit(c, std::move(edit), kind, [](const Edit& made) {
        std::vector<Selection> after;
        for (const Span& span : spans_after(made)) after.push_back(Selection::at(span.to));
        return after;
    });
}

void type(CommandContext& c, std::string_view text) {
    replace_each(c, std::vector<std::string>(c.selections.size(), std::string(text)), EditKind::Typing);
}

// The text a caret deletes, reaching from where it is.
using Reach = Span (*)(const Buffer&, Position);

// Deletes each selection, or for a caret, what `reach` gives from it. Spans
// that overlap are deleted as one, and each caret ends where its own span
// began.
void erase(CommandContext& c, Reach reach, EditKind kind = EditKind::Deleting) {
    std::vector<Span> spans;
    for (const Selection& selection : c.selections.all()) {
        spans.push_back(selection.empty() ? reach(c.buffer, selection.caret) : selection.span());
    }

    std::vector<Span> joined = spans;
    std::sort(joined.begin(), joined.end(), [](const Span& a, const Span& b) { return a.from < b.from; });
    Edit edit;
    for (const Span& span : joined) {
        if (span.empty()) continue;
        if (!edit.changes.empty() && span.from < edit.changes.back().span.to) {
            edit.changes.back().span.to = std::max(edit.changes.back().span.to, span.to);
        } else {
            edit.changes.push_back({.span = span, .text = ""});
        }
    }
    commit(c, std::move(edit), kind, [&](const Edit& made) {
        std::vector<Selection> after;
        for (const Span& span : spans) after.push_back(Selection::at(map(made, span.from, Bias::Before)));
        return after;
    });
}

Span to_char_left(const Buffer& buffer, Position at) {
    return {char_left(buffer, at), at};
}

Span to_char_right(const Buffer& buffer, Position at) {
    return {at, char_right(buffer, at)};
}

Span to_word_left(const Buffer& buffer, Position at) {
    return {word_left(buffer, at), at};
}

Span to_word_right(const Buffer& buffer, Position at) {
    return {at, word_right(buffer, at)};
}

// Lines `first` to `last`, both counted.
struct LineRange {
    std::size_t first{0};
    std::size_t last{0};
};

// The lines the selections cover, in order, those that overlap or follow on
// joined. A selection ending at the very start of a line leaves that line out.
std::vector<LineRange> covered_lines(const Selections& selections) {
    std::vector<LineRange> ranges;
    for (const Selection& selection : selections.all()) {
        const Span span = selection.span();
        LineRange range{.first = span.from.line, .last = span.to.line};
        if (!span.empty() && span.to.byte == 0 && span.to.line > span.from.line) --range.last;
        if (!ranges.empty() && range.first <= ranges.back().last + 1) {
            ranges.back().last = std::max(ranges.back().last, range.last);
        } else {
            ranges.push_back(range);
        }
    }
    return ranges;
}

// The spaces and tabs line `index` begins with.
std::string_view leading_space(const Buffer& buffer, std::size_t index) {
    const std::string_view line = buffer.line(index);
    std::size_t end = 0;
    while (end < line.size() && (line[end] == ' ' || line[end] == '\t')) ++end;
    return line.substr(0, end);
}

// The whole of lines `range`, without the break after the last.
std::string block_text(const Buffer& buffer, LineRange range) {
    return buffer.text({{range.first, 0}, {range.last, buffer.line(range.last).size()}});
}

void delete_lines(CommandContext& c) {
    const std::vector<LineRange> ranges = covered_lines(c.selections);
    Edit edit;
    for (const LineRange& range : ranges) {
        if (range.last + 1 < c.buffer.line_count()) {
            edit.changes.push_back({.span = {{range.first, 0}, {range.last + 1, 0}}, .text = ""});
        } else if (range.first > 0) {
            edit.changes.push_back({.span = {{range.first - 1, c.buffer.line(range.first - 1).size()},
                                             {range.last, c.buffer.line(range.last).size()}},
                                    .text = ""});
        } else {
            edit.changes.push_back({.span = {{0, 0}, c.buffer.end()}, .text = ""});
        }
    }
    // Each caret goes to the line that takes its line's place, as far along
    // as that line allows.
    std::vector<Selection> carets(c.selections.all().begin(), c.selections.all().end());
    commit(c, std::move(edit), EditKind::Other, [&](const Edit& made) {
        std::vector<Selection> after;
        for (const Selection& selection : carets) {
            const Position line_start = map(made, {selection.caret.line, 0}, Bias::Before);
            after.push_back(Selection::at(c.buffer.clamp({line_start.line, selection.caret.byte})));
        }
        return after;
    });
}

// One level of indent put in at `at`: a tab, or spaces on to the next stop.
std::string indent_at(const CommandContext& c, Position at) {
    if (c.indent.tabs) return "\t";
    const std::size_t column = column_of(c.buffer.line(at.line), at.byte, c.indent.width);
    return std::string(c.indent.width - column % c.indent.width, ' ');
}

// Each selection carried through an indent or outdent: an end at a line's
// very start stays there, any other moves with the line's text.
std::vector<Selection> through_indent(const std::vector<Selection>& selections, const Edit& made) {
    auto carry = [&](Position at) { return map(made, at, at.byte == 0 ? Bias::Before : Bias::After); };
    std::vector<Selection> after;
    for (const Selection& selection : selections) {
        after.push_back({.anchor = carry(selection.anchor), .caret = carry(selection.caret)});
    }
    return after;
}

// Tab: across lines, every line that has text gains a level of indent;
// within a line, the selection or caret gives way to one.
void tab(CommandContext& c) {
    const bool lines = std::any_of(c.selections.all().begin(), c.selections.all().end(),
                                   [](const Selection& s) { return s.span().from.line != s.span().to.line; });
    if (!lines) {
        std::vector<std::string> texts;
        for (const Selection& selection : c.selections.all()) texts.push_back(indent_at(c, selection.span().from));
        replace_each(c, texts, EditKind::Typing);
        return;
    }

    const std::string level = c.indent.tabs ? "\t" : std::string(c.indent.width, ' ');
    Edit edit;
    for (const LineRange& range : covered_lines(c.selections)) {
        for (std::size_t line = range.first; line <= range.last; ++line) {
            if (!c.buffer.line(line).empty()) edit.changes.push_back({.span = {{line, 0}, {line, 0}}, .text = level});
        }
    }
    const std::vector<Selection> selections(c.selections.all().begin(), c.selections.all().end());
    commit(c, std::move(edit), EditKind::Other, [&](const Edit& made) { return through_indent(selections, made); });
}

// Every line the selections touch loses a level of indent: a tab, or up to
// a level's worth of spaces.
void outdent(CommandContext& c) {
    Edit edit;
    for (const LineRange& range : covered_lines(c.selections)) {
        for (std::size_t line = range.first; line <= range.last; ++line) {
            const std::string_view text = c.buffer.line(line);
            std::size_t end = 0;
            if (!text.empty() && text[0] == '\t') {
                end = 1;
            } else {
                while (end < text.size() && end < c.indent.width && text[end] == ' ') ++end;
            }
            if (end > 0) edit.changes.push_back({.span = {{line, 0}, {line, end}}, .text = ""});
        }
    }
    const std::vector<Selection> selections(c.selections.all().begin(), c.selections.all().end());
    commit(c, std::move(edit), EditKind::Other, [&](const Edit& made) { return through_indent(selections, made); });
}

// The lines the selections cover, swapped with the line above or below them;
// nothing moves when a block is already at the top or bottom.
void move_lines(CommandContext& c, bool down) {
    const std::vector<LineRange> ranges = covered_lines(c.selections);
    if (!down && ranges.front().first == 0) return;
    if (down && ranges.back().last + 1 >= c.buffer.line_count()) return;

    Edit edit;
    for (const LineRange& range : ranges) {
        const std::string block = block_text(c.buffer, range);
        if (down) {
            const std::size_t below = range.last + 1;
            edit.changes.push_back({.span = {{range.first, 0}, {below, c.buffer.line(below).size()}},
                                    .text = std::string(c.buffer.line(below)) + "\n" + block});
        } else {
            const std::size_t above = range.first - 1;
            edit.changes.push_back({.span = {{above, 0}, {range.last, c.buffer.line(range.last).size()}},
                                    .text = block + "\n" + std::string(c.buffer.line(above))});
        }
    }
    const std::vector<Selection> selections(c.selections.all().begin(), c.selections.all().end());
    commit(c, std::move(edit), EditKind::Other, [&](const Edit&) {
        std::vector<Selection> after;
        for (Selection selection : selections) {
            selection.anchor.line = down ? selection.anchor.line + 1 : selection.anchor.line - 1;
            selection.caret.line = down ? selection.caret.line + 1 : selection.caret.line - 1;
            selection.goal.reset();
            after.push_back(selection);
        }
        return after;
    });
}

// The lines the selections cover, copied below themselves, the selections
// going with the copy; or above, the selections staying on the upper copy.
void copy_lines(CommandContext& c, bool down) {
    const std::vector<LineRange> ranges = covered_lines(c.selections);
    Edit edit;
    for (const LineRange& range : ranges) {
        const std::string block = block_text(c.buffer, range);
        if (down) {
            const Position end{range.last, c.buffer.line(range.last).size()};
            edit.changes.push_back({.span = {end, end}, .text = "\n" + block});
        } else {
            edit.changes.push_back({.span = {{range.first, 0}, {range.first, 0}}, .text = block + "\n"});
        }
    }
    const std::vector<Selection> selections(c.selections.all().begin(), c.selections.all().end());
    commit(c, std::move(edit), EditKind::Other, [&](const Edit&) {
        // Lines added above a selection: every block before its own, and its
        // own when the copy went below it.
        std::vector<Selection> after;
        for (Selection selection : selections) {
            std::size_t added = 0;
            for (const LineRange& range : ranges) {
                const std::size_t size = range.last - range.first + 1;
                if (range.last < selection.span().from.line) {
                    added += size;
                } else if (down && range.first <= selection.span().from.line) {
                    added += size;
                }
            }
            selection.anchor.line += added;
            selection.caret.line += added;
            selection.goal.reset();
            after.push_back(selection);
        }
        return after;
    });
}

// A new line below or above each caret's line, indented as that line is,
// the caret on it.
void insert_line(CommandContext& c, bool below) {
    std::vector<std::size_t> lines;
    for (const Selection& selection : c.selections.all()) {
        if (lines.empty() || lines.back() != selection.caret.line) lines.push_back(selection.caret.line);
    }
    Edit edit;
    for (const std::size_t line : lines) {
        const std::string indent(leading_space(c.buffer, line));
        if (below) {
            const Position end{line, c.buffer.line(line).size()};
            edit.changes.push_back({.span = {end, end}, .text = "\n" + indent});
        } else {
            edit.changes.push_back({.span = {{line, 0}, {line, 0}}, .text = indent + "\n"});
        }
    }
    commit(c, std::move(edit), EditKind::Other, [&](const Edit& made) {
        const std::vector<Span> spans = spans_after(made);
        std::vector<Selection> after;
        for (const Selection& selection : c.selections.all()) {
            const std::size_t i = static_cast<std::size_t>(
                std::find(lines.begin(), lines.end(), selection.caret.line) - lines.begin());
            // Below, the new line ends the inserted text; above, it begins it,
            // the caret after its indent (the text less its break).
            const std::size_t indent = made.changes[i].text.size() - 1;
            after.push_back(Selection::at(below ? spans[i].to : Position{spans[i].from.line, indent}));
        }
        return after;
    });
}

// What copy and cut take: the selected text, or with nothing selected the
// carets' whole lines, each with its break.
std::string copied(const CommandContext& c) {
    const bool carets = std::all_of(c.selections.all().begin(), c.selections.all().end(),
                                    [](const Selection& s) { return s.empty(); });
    std::string text;
    if (carets) {
        std::size_t last = std::numeric_limits<std::size_t>::max();
        for (const Selection& selection : c.selections.all()) {
            if (selection.caret.line == last) continue;
            last = selection.caret.line;
            text += c.buffer.line(last);
            text += '\n';
        }
        return text;
    }
    bool first = true;
    for (const Selection& selection : c.selections.all()) {
        if (selection.empty()) continue;
        if (!first) text += '\n';
        text += c.buffer.text(selection.span());
        first = false;
    }
    return text;
}

void copy(CommandContext& c) {
    if (c.clipboard != nullptr && c.clipboard->set) c.clipboard->set(copied(c));
}

void cut(CommandContext& c) {
    if (c.clipboard == nullptr || !c.clipboard->set) return;
    copy(c);
    const bool carets = std::all_of(c.selections.all().begin(), c.selections.all().end(),
                                    [](const Selection& s) { return s.empty(); });
    if (carets) {
        delete_lines(c);
    } else {
        erase(c, [](const Buffer&, Position at) { return Span{at, at}; }, EditKind::Other);
    }
}

// The clipboard's text at every selection; with as many lines as there are
// selections, one line at each.
void paste(CommandContext& c) {
    if (c.clipboard == nullptr || !c.clipboard->get) return;
    const std::string text = c.clipboard->get();
    if (text.empty()) return;

    std::vector<std::string> lines;
    std::size_t begin = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            lines.emplace_back(text.substr(begin, i - begin));
            begin = i + 1;
        }
    }
    if (!text.empty() && text.back() == '\n') lines.pop_back();
    for (std::string& line : lines) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
    }

    const bool spread = c.selections.size() > 1 && lines.size() == c.selections.size();
    replace_each(c, spread ? lines : std::vector<std::string>(c.selections.size(), text), EditKind::Other);
}

void undo(CommandContext& c) {
    if (std::optional<Selections> restored = c.history.undo(c.buffer)) c.selections = *restored;
}

void redo(CommandContext& c) {
    if (std::optional<Selections> restored = c.history.redo(c.buffer)) c.selections = *restored;
}

const std::array COMMANDS = {
    Command{"caret.left", "Move left", [](CommandContext& c) { step(c, false, false); }},
    Command{"caret.right", "Move right", [](CommandContext& c) { step(c, true, false); }},
    Command{"caret.up", "Move up", [](CommandContext& c) { vertical(c, false, 1, false); }},
    Command{"caret.down", "Move down", [](CommandContext& c) { vertical(c, true, 1, false); }},
    Command{"caret.word_left", "Move to the word before", [](CommandContext& c) { move(c, word_left, false); }},
    Command{"caret.word_right", "Move to the word after", [](CommandContext& c) { move(c, word_right, false); }},
    Command{"caret.line_start", "Move to the line's start", [](CommandContext& c) { move(c, line_start, false); }},
    Command{"caret.line_end", "Move to the line's end", [](CommandContext& c) { move(c, line_end, false); }},
    Command{"caret.page_up", "Move a page up",
            [](CommandContext& c) { vertical(c, false, c.layout.page_lines(), false); }},
    Command{"caret.page_down", "Move a page down",
            [](CommandContext& c) { vertical(c, true, c.layout.page_lines(), false); }},
    Command{"caret.text_start", "Move to the start of the file", [](CommandContext& c) { move(c, text_start, false); }},
    Command{"caret.text_end", "Move to the end of the file", [](CommandContext& c) { move(c, text_end, false); }},

    Command{"select.left", "Select left", [](CommandContext& c) { step(c, false, true); }},
    Command{"select.right", "Select right", [](CommandContext& c) { step(c, true, true); }},
    Command{"select.up", "Select up", [](CommandContext& c) { vertical(c, false, 1, true); }},
    Command{"select.down", "Select down", [](CommandContext& c) { vertical(c, true, 1, true); }},
    Command{"select.word_left", "Select to the word before", [](CommandContext& c) { move(c, word_left, true); }},
    Command{"select.word_right", "Select to the word after", [](CommandContext& c) { move(c, word_right, true); }},
    Command{"select.line_start", "Select to the line's start", [](CommandContext& c) { move(c, line_start, true); }},
    Command{"select.line_end", "Select to the line's end", [](CommandContext& c) { move(c, line_end, true); }},
    Command{"select.page_up", "Select a page up",
            [](CommandContext& c) { vertical(c, false, c.layout.page_lines(), true); }},
    Command{"select.page_down", "Select a page down",
            [](CommandContext& c) { vertical(c, true, c.layout.page_lines(), true); }},
    Command{"select.text_start", "Select to the start of the file",
            [](CommandContext& c) { move(c, text_start, true); }},
    Command{"select.text_end", "Select to the end of the file", [](CommandContext& c) { move(c, text_end, true); }},

    Command{"edit.type", "Type", [](CommandContext& c) { type(c, c.text); }},
    Command{"edit.newline", "New line", [](CommandContext& c) { type(c, "\n"); }},
    Command{"edit.tab", "Indent", tab},
    Command{"edit.outdent", "Outdent", outdent},
    Command{"edit.backspace", "Delete the character before", [](CommandContext& c) { erase(c, to_char_left); }},
    Command{"edit.delete", "Delete the character after", [](CommandContext& c) { erase(c, to_char_right); }},
    Command{"edit.delete_word_left", "Delete the word before", [](CommandContext& c) { erase(c, to_word_left); }},
    Command{"edit.delete_word_right", "Delete the word after", [](CommandContext& c) { erase(c, to_word_right); }},
    Command{"edit.delete_lines", "Delete lines", delete_lines},
    Command{"edit.move_lines_up", "Move lines up", [](CommandContext& c) { move_lines(c, false); }},
    Command{"edit.move_lines_down", "Move lines down", [](CommandContext& c) { move_lines(c, true); }},
    Command{"edit.copy_lines_up", "Copy lines up", [](CommandContext& c) { copy_lines(c, false); }},
    Command{"edit.copy_lines_down", "Copy lines down", [](CommandContext& c) { copy_lines(c, true); }},
    Command{"edit.insert_line_below", "Insert a line below", [](CommandContext& c) { insert_line(c, true); }},
    Command{"edit.insert_line_above", "Insert a line above", [](CommandContext& c) { insert_line(c, false); }},
    Command{"edit.cut", "Cut", cut},
    Command{"edit.copy", "Copy", copy},
    Command{"edit.paste", "Paste", paste},
    Command{"edit.undo", "Undo", undo},
    Command{"edit.redo", "Redo", redo},
};

} // namespace

std::span<const Command> commands() {
    return COMMANDS;
}

const Command* find_command(std::string_view name) {
    const auto found = std::find_if(COMMANDS.begin(), COMMANDS.end(), [&](const Command& c) { return c.name == name; });
    return found == COMMANDS.end() ? nullptr : &*found;
}

bool run_command(std::string_view name, CommandContext& context) {
    const Command* command = find_command(name);
    if (command == nullptr) return false;
    command->run(context);
    return true;
}

} // namespace fjedit
