#pragma once

// What the tokens tell of the text's shape: which brackets pair up, and
// where the text folds. Brackets inside strings and comments don't count.

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/syntax.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

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

/// The nearest `open` bracket before `at` that nothing between it and `at`
/// closes: the opener of the block `at` is in. Brackets in strings and
/// comments are passed over when `syntax` is given; nothing when there is no
/// such bracket within 10,000 lines.
[[nodiscard]] std::optional<Position> enclosing_opener(const Buffer& buffer, const Syntax* syntax, Position at,
                                                       char open);

/// Lines that fold away under the first: folded, line `first` stays and the
/// lines after it, through `last`, are hidden. For a bracket's block,
/// `last` is the line with its closer, whose text a view shows after the
/// fold's mark (`if (a) {⋯} else {`). So a range may begin on the line the
/// one before it ends on; one never begins inside another and ends past it.
struct FoldRange {
    std::size_t first{0};
    std::size_t last{0};

    bool operator==(const FoldRange&) const = default;
};

/// How a language's text folds.
enum class Folding : uint8_t {
    /// From a line where a bracket opens to the line where it closes.
    Brackets,
    /// From a line to the last of the lines after it indented deeper.
    Indentation,
};

/// Where the text folds, by `folding`, and at every run of two lines or
/// more that are all comment: in order of first line, the longest range
/// for each first line. Indentation counts a tab as reaching on to the next
/// multiple of `tab_width`.
[[nodiscard]] std::vector<FoldRange> fold_ranges(const Buffer& buffer, const Syntax& syntax, Folding folding,
                                                 std::size_t tab_width = 4);

/// Whether byte `byte` of line `line` is inside a string or a comment.
[[nodiscard]] bool in_string_or_comment(const Syntax& syntax, std::size_t line, std::size_t byte);

} // namespace fjedit
