#pragma once

// What the tokens tell of the text's shape: which brackets pair up. Brackets
// inside strings and comments don't count.

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/syntax.hpp"

#include <optional>

namespace fjedit {

/// A bracket and the one it pairs with: `open` is the ( [ or {, `close`
/// its ) ] or }, each the position before the character.
struct BracketPair {
    Position open;
    Position close;

    bool operator==(const BracketPair&) const = default;
};

/// The bracket just after `at`, or failing that the one just before it,
/// and the bracket it pairs with; nothing when neither is a bracket, the
/// bracket is in a string or comment, or it pairs with none. Brackets of
/// one kind count against each other; the search gives up past 10,000
/// lines.
[[nodiscard]] std::optional<BracketPair> matching_bracket(const Buffer& buffer, const Syntax& syntax, Position at);

/// Whether byte `byte` of line `line` is inside a string or a comment.
[[nodiscard]] bool in_string_or_comment(const Syntax& syntax, std::size_t line, std::size_t byte);

} // namespace fjedit
