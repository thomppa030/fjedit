#pragma once

// Vim's text objects: the text `iw`, `a(`, `i"` or `ap` takes around a
// position, as an operator (`ciw`, `da(`) or visual mode takes it. Each is
// found as Vim's own code finds it, so the blanks `aw` takes with a word,
// and the lines `i{` leaves its braces on, are the ones Vim takes.
//
//     const auto taken = fjedit::vim::text_object(buffer, syntax, caret, "i(", 1);

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/syntax.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace fjedit::vim {

/// What a text object takes.
struct Taken {
    /// From the object's first character to past its last; for whole
    /// lines, from the first line's start to the last line's end.
    Span span;
    /// It takes whole lines: `ip`, `ap`.
    bool linewise{false};

    bool operator==(const Taken&) const = default;
};

/// The text object `name` around `at`, `count` of them: `iw` `aw` and the
/// WORD forms `iW` `aW`; the blocks `i(` `a(` (also `i)` `ib`), `i{` `a{`
/// (`i}` `iB`), `i[` `a[`, `i<` `a<`; the quoted `i"` `i'` and ``i` `` with
/// their `a` forms; the paragraphs `ip` `ap`. An `i` takes what is inside, an
/// `a` the object whole: a word with the blanks after it (or before, when
/// none follow), a block with its brackets, a quote with its quotes and the
/// blanks after, a paragraph with the empty lines after it. Brackets in
/// strings and comments don't count; quotes are looked for on `at`'s line
/// only, a backslash escaping one. Nothing for a name it doesn't know, or
/// where the text has no such object, or fewer than `count`.
[[nodiscard]] std::optional<Taken> text_object(const Buffer& buffer, const Syntax& syntax, Position at,
                                               std::string_view name, std::size_t count);

} // namespace fjedit::vim
