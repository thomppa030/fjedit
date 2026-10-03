#pragma once

// Where a caret goes: from one position to the next character, word, line or
// page. Each motion is a plain function of the text and a position, so the
// commands that move or extend selections use them, and so can a modal
// keymap's motions.
//
// Words are runs of one kind of character: letters, digits and underscores
// (and anything past ASCII), or punctuation. Spaces and tabs separate them.

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/layout.hpp"

#include <cstddef>
#include <cstdint>

namespace fjedit {

/// What kind of character `c` is, for where words begin and end.
enum class CharKind : uint8_t {
    /// A space or a tab.
    Space,
    /// A letter, digit or underscore, and every character past ASCII.
    Word,
    /// Any other ASCII character.
    Punctuation,
};

[[nodiscard]] CharKind char_kind(uint32_t c);

/// The position one character on, or the start of the next line from the
/// end of one. The end of the text stays where it is.
[[nodiscard]] Position char_right(const Buffer& buffer, Position at);

/// The position one character back, or the end of the line before from the
/// start of one. The start of the text stays where it is.
[[nodiscard]] Position char_left(const Buffer& buffer, Position at);

/// The end of the word or run of punctuation after `at`, past any spaces
/// first; the start of the next line from the end of one.
[[nodiscard]] Position word_right(const Buffer& buffer, Position at);

/// The start of the word or run of punctuation before `at`, past any spaces
/// first; the end of the line before from the start of one.
[[nodiscard]] Position word_left(const Buffer& buffer, Position at);

/// The first character of the line that isn't a space or a tab; from there,
/// the line's very start.
[[nodiscard]] Position line_start(const Buffer& buffer, Position at);

[[nodiscard]] Position line_end(const Buffer& buffer, Position at);

/// The position `lines` lines up from `at` nearest `x` along it; the start
/// of the text when there are not that many lines above.
[[nodiscard]] Position lines_up(const Buffer& buffer, const Layout& layout, Position at, float x, std::size_t lines);

/// The position `lines` lines down from `at` nearest `x` along it; the end
/// of the text when there are not that many lines below.
[[nodiscard]] Position lines_down(const Buffer& buffer, const Layout& layout, Position at, float x, std::size_t lines);

} // namespace fjedit
