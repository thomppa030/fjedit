#pragma once

// What Vim's operators and its small changes do to the text at every caret:
// the range a motion or a text object gives an operator, and the edits
// made over the ranges, each one step of the history. The engine reads the
// keys; these make the changes.
//
//     const fjedit::vim::Range range = fjedit::vim::motion_range(buffer, caret, to, kind);
//     fjedit::vim::operate(fjedit::vim::Operator::Delete, ranges, '"', context, registers);

#include "fjedit/core/commands.hpp"
#include "fjedit/core/vim/motions.hpp"
#include "fjedit/core/vim/registers.hpp"
#include "fjedit/core/vim/text_objects.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit::vim {

/// The text an operator acts on at one caret.
struct Range {
    /// From the first character to past the last. For whole lines, the
    /// lines from `span.from`'s to `span.to`'s, `span.from` where the motion
    /// began or ended first, which a yank leaves the caret at.
    Span span;
    bool linewise{false};

    bool operator==(const Range&) const = default;
};

/// The range a motion from `from` to `to` gives an operator, as Vim makes
/// it: a linewise motion takes the lines between; an inclusive one the
/// character it ends on too; an exclusive one that ends at a line's start,
/// over more than one line, ends at the end of the line before instead, and
/// takes whole lines when it began in its first line's indent (`d}` from a
/// line's start).
[[nodiscard]] Range motion_range(const Buffer& buffer, Position from, Position to, MotionKind kind);

/// The range what a text object took gives an operator, by the same rules
/// as an exclusive motion's (`ci{` over lines changes whole lines).
[[nodiscard]] Range object_range(const Buffer& buffer, const Taken& taken);

enum class Operator : uint8_t {
    /// `d`: the text into a register, and gone.
    Delete,
    /// `c`: deleted, insert mode where it was; whole lines leave one line
    /// with the first one's indent.
    Change,
    /// `y`: the text into a register.
    Yank,
    /// `>` and `<`: the lines a level of indent further in, or out.
    ShiftRight,
    ShiftLeft,
    /// `=`: the lines indented as their brackets say.
    Indent,
    /// `g~`, `gu` and `gU`: the letters' case turned over, made lower, made
    /// upper.
    ToggleCase,
    Lower,
    Upper,
};

/// Does `op` over `ranges`, one for each selection of `context` in their
/// order (none for one whose motion went nowhere, which stays), as one step
/// of the history; a change's step goes on through the insert after it.
/// Deleted and yanked text goes to `registers` under `name` (`"` for none),
/// a piece for each range; `numbered` deletes go to register 1 whatever
/// their size. `shifts` is how many levels `>` and `<` move. Each caret
/// goes where Vim leaves it: at the range's start, on a line's first text
/// after whole lines are deleted or indented.
void operate(Operator op, std::span<const std::optional<Range>> ranges, char name, CommandContext& context,
             Registers& registers, std::size_t shifts = 1, bool numbered = false);

/// `p` (`after`) and `P`: what `yanked` holds put `count` times at each
/// caret, after or before its character; whole lines on lines of their own
/// below or above its line. Each caret puts its own piece when there are as
/// many as carets, or a line each of one piece that has as many lines;
/// otherwise each puts the whole. A caret ends on the last character put,
/// or on the first line's text for whole lines, or at the start of what
/// spans several lines.
void put(const Yanked& yanked, bool after, std::size_t count, CommandContext& context);

/// `J`: `count` lines (two at least) at each caret joined into one, each
/// joined line's indent taken away and a space put between unless the line
/// before ends in a blank or the joined one starts with `)`. The caret goes
/// where the last two met. Nothing when the caret's line is the last.
void join_lines(std::size_t count, CommandContext& context);

/// `r`: the `count` characters from each caret replaced by `with`, one
/// each; a line break replaces them all with one. A caret whose line hasn't
/// that many is left as it is. False when no caret had them.
bool replace_characters(std::string_view with, std::size_t count, CommandContext& context);

/// Replace mode's typing: `character` written over the character at each
/// caret, or past a line's end put in, the caret after it. What each caret
/// wrote over goes on its stack in `overwritten` ("" for nothing), for
/// take_back_overtyped() to put back.
void overtype(std::string_view character, CommandContext& context,
              std::vector<std::vector<std::string>>& overwritten);

/// Replace mode's Backspace: each caret a character back, what it wrote over
/// there put back off its stack in `overwritten`; past where its replacing
/// began, it only moves.
void take_back_overtyped(CommandContext& context, std::vector<std::vector<std::string>>& overwritten);

/// `r` in visual mode: every character `ranges` take, but line breaks,
/// replaced by `with`, as one step; each caret at its range's start.
void replace_in(std::span<const std::optional<Range>> ranges, std::string_view with, CommandContext& context);

/// `~`: the case of `count` letters from each caret along its line turned
/// over, the caret going past them.
void toggle_case_characters(std::size_t count, CommandContext& context);

/// `Ctrl-A` and `Ctrl-X`: `amount` added to the number at or after each
/// caret on its line (decimal, with a `-` before it, or hexadecimal after
/// `0x`), the caret on its last digit. False when no caret's line has one.
bool add_to_number(long long amount, CommandContext& context);

} // namespace fjedit::vim
