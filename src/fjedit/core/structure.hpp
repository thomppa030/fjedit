#pragma once

// What the text's shape tells: which brackets pair up, where the text folds,
// how its lines are indented. Brackets inside strings and comments don't
// count.

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/syntax.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
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

/// A bracket and how deep it stands: how many pairs hold it, or nothing
/// when it pairs with none.
struct LeveledBracket {
    Position at;
    std::optional<uint32_t> level;

    bool operator==(const LeveledBracket&) const = default;
};

/// Every bracket of a text in order, with its level (bracket_levels()).
struct BracketLevels {
    std::vector<LeveledBracket> brackets;
    /// Where each line's brackets begin in `brackets`, and after the last
    /// line, where they end.
    std::vector<std::size_t> line_starts;

    /// The brackets of line `line`, in order; none for a line past the text
    /// they were counted in.
    [[nodiscard]] std::span<const LeveledBracket> on_line(std::size_t line) const;
};

/// The brackets of `buffer` paired as a parser pairs them, and given levels
/// as Rainbow Brackets counts them: ( [ { outside strings and comments, and
/// the angle brackets `named` gives TokenKind::Bracket (a language service's
/// tokens for each line), every kind in one depth. A closer pairs with the
/// nearest opener of its kind still open; the openers opened after that one
/// are left without a pair, as is a closer with none open. A paired
/// bracket's level is how many pairs hold it.
[[nodiscard]] BracketLevels bracket_levels(const Buffer& buffer, const Syntax& syntax,
                                           std::span<const std::vector<Token>> named = {});

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

/// How lines are indented: with a tab or with spaces, and how many cells a
/// level of indent and a tab take up.
struct Indent {
    bool tabs{false};
    std::size_t width{4};
};

/// How `buffer`'s lines are indented: with tabs when more of its indented
/// lines begin with a tab than with a space; with spaces otherwise, a level
/// as wide as the step most often taken from one line's indent to the next
/// (steps of one, a comment's " * ", don't count). `fallback` where the text
/// doesn't say, and its width for tabs.
[[nodiscard]] Indent detect_indent(const Buffer& buffer, Indent fallback);

/// The function whose body holds line `line`, as C-like code writes one:
/// its name then a bracket ("begin_play()"), read from the head of the
/// block it opens. The blocks of branches, loops and lambdas are looked
/// through to the function around them; a line that begins by closing a
/// block is still in it. Nothing outside a function. It reads only the
/// text; a language service that knows the file's symbols answers better.
[[nodiscard]] std::optional<std::string> enclosing_function(const Buffer& buffer, const Syntax& syntax,
                                                            std::size_t line);

/// Whether byte `byte` of line `line` is inside a string or a comment.
[[nodiscard]] bool in_string_or_comment(const Syntax& syntax, std::size_t line, std::size_t byte);

} // namespace fjedit
