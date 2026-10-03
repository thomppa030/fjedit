#include "fjedit/core/commands.hpp"

#include "fjedit/core/motion.hpp"

#include <algorithm>
#include <array>
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
