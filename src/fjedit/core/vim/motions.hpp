#pragma once

// Vim's motions: where each takes a caret, and how an operator takes the
// text it passes over (`dw` stops before the next word, `de` takes the
// word's last character, `dj` takes whole lines). They are plain functions
// of the text; the ones that need the view's rows (`j`, `k`, `H`) are the
// engine's.
//
// A position is on the character that starts there, or at a line's end,
// where its break is. Normal mode never leaves a caret at a non-empty
// line's end, but an operator reaches there: `d$` takes the line's last
// character, not its break.
//
//     const fjedit::Position to = fjedit::vim::word_start_after(buffer, caret, 3, false);

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/syntax.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace fjedit::vim {

/// How an operator takes the text a motion moves over.
enum class MotionKind : uint8_t {
    /// Up to the place moved to, not its character: `w`, `b`, `h`, `}`.
    Exclusive,
    /// Through the character moved to: `e`, `f`, `$`, `%`.
    Inclusive,
    /// The whole lines from the one moved from to the one moved to: `j`,
    /// `gg`.
    Linewise,
};

/// Where a motion went, and how an operator over it takes the text.
struct Moved {
    Position to;
    MotionKind kind{MotionKind::Exclusive};

    bool operator==(const Moved&) const = default;
};

/// `w`: the start of the `count`th word after `at`, past blanks and line
/// breaks; an empty line is a word. Words are runs of word characters or
/// of punctuation (fjedit/core/motion.hpp); `big` makes them any run that
/// isn't blank (`W`). It stops at the text's end. With `stop_at_line_end`
/// the last count stops at the end of the line it leaves rather than going
/// on, as for an operator: `dw` on a line's last word leaves the break.
[[nodiscard]] Position word_start_after(const Buffer& buffer, Position at, std::size_t count, bool big,
                                        bool stop_at_line_end = false);

/// `e`: the last character of the `count`th word ending after `at` (`E`
/// with `big`). With `stay`, a first count that starts on a word's last
/// character stays there, as `cw` does. At the text's end it stops there.
[[nodiscard]] Position word_end_after(const Buffer& buffer, Position at, std::size_t count, bool big,
                                      bool stay = false);

/// `b`: the start of the `count`th word before `at` (`B` with `big`); an
/// empty line is a word. The text's start stays where it is.
[[nodiscard]] Position word_start_before(const Buffer& buffer, Position at, std::size_t count, bool big);

/// `ge`: the last character of the `count`th word ending before `at` (`gE`
/// with `big`); an empty line is a word. The text's start stays where it
/// is.
[[nodiscard]] Position word_end_before(const Buffer& buffer, Position at, std::size_t count, bool big);

/// `^`: the first character of `line` that isn't a space or a tab; its end
/// when it has none.
[[nodiscard]] Position first_non_blank(const Buffer& buffer, std::size_t line);

/// `$` as normal mode places it: on `line`'s last character, at its start
/// when it is empty.
[[nodiscard]] Position last_character(const Buffer& buffer, std::size_t line);

/// `}`: the start of the `count`th empty line after `at`'s, past the lines
/// with text first, exclusive; with none left, the last line's last
/// character, inclusive. Nothing moves when the text ends with counts still
/// to go.
[[nodiscard]] Moved paragraph_after(const Buffer& buffer, Position at, std::size_t count);

/// `{`: the start of the `count`th empty line before `at`'s, or of the
/// text; exclusive. Nothing moves when the text starts with counts still
/// to go.
[[nodiscard]] Moved paragraph_before(const Buffer& buffer, Position at, std::size_t count);

/// `%`: the bracket paired with the first of ( ) [ ] { } at or after `at` on
/// its line, brackets in strings and comments passed over. Nothing when the
/// line has none after `at`, or the first pairs with none.
[[nodiscard]] std::optional<Position> paired_bracket(const Buffer& buffer, const Syntax& syntax, Position at);

/// What `f`, `t`, `F` and `T` look for, which `;` and `,` look for again.
struct CharSearch {
    /// The character, as UTF-8.
    std::string target;
    /// `f` and `t` look on along the line, `F` and `T` back.
    bool forward{true};
    /// `t` and `T`: beside the character, short of it.
    bool till{false};

    bool operator==(const CharSearch&) const = default;
};

/// Where `search` lands on `at`'s line at its `count`th match: on the
/// character, or beside it short of it for a till. With `again`, as `;`
/// and `,` look, a till of one whose match is right beside `at` goes on to
/// the next, so it never lands where it starts. Nothing when the line has
/// fewer matches.
[[nodiscard]] std::optional<Position> find_in_line(const Buffer& buffer, Position at, const CharSearch& search,
                                                   std::size_t count, bool again = false);

/// `h`: `count` characters back along `at`'s line, no further than its
/// start.
[[nodiscard]] Position left_in_line(const Buffer& buffer, Position at, std::size_t count);

/// `l`: `count` characters on along `at`'s line, no further than its end.
[[nodiscard]] Position right_in_line(const Buffer& buffer, Position at, std::size_t count);

} // namespace fjedit::vim
