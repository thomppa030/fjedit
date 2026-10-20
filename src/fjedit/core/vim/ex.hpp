#pragma once

// Vim's command line read: an ex command's range, its name as Vim spells it
// short or long, a ! after it, and what follows. The engine resolves the
// range against the text and runs the command.
//
//     const ftk::Result<fjedit::vim::ExCommand> read = fjedit::vim::parse_ex("'<,'>s/spin/rotor/g");
//     // read->range: '< to '>; read->name: "substitute"; read->argument: "/spin/rotor/g"

#include "ftk/base/result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace fjedit::vim {

/// One end of a range: a line by number, the caret's, the last, a mark's, or
/// the one a pattern finds after or before the caret's; then lines on or
/// back from it.
struct ExAddress {
    enum class Kind : uint8_t { Number, Current, Last, Mark, Forward, Backward };
    Kind kind{Kind::Current};
    /// Kind::Number: the line counted from 1, 0 for above the first.
    std::size_t number{0};
    /// Kind::Mark: `<`, `>` or a letter.
    char mark{0};
    /// Kind::Forward and Kind::Backward: the pattern, empty for the last.
    std::string pattern;
    /// The lines `+` and `-` after it add.
    long offset{0};

    bool operator==(const ExAddress&) const = default;
};

/// The lines a command acts on, as written. Neither end for the command's
/// own (most take the caret's line); `%` for the whole text; with
/// `from_first` (a `;` between), the caret is on the first end when the
/// second is read.
struct ExRange {
    std::optional<ExAddress> from;
    std::optional<ExAddress> to;
    bool whole{false};
    bool from_first{false};

    bool operator==(const ExRange&) const = default;
};

struct ExCommand {
    ExRange range;
    /// The command's whole name ("delete"); empty for a range alone (`:12`).
    std::string name;
    bool bang{false};
    /// What follows the name, without the blanks before it.
    std::string argument;
};

/// `line`, typed on Vim's command line, read. An error worded as Vim's for
/// a name it knows none by ("E492: Not an editor command: frob") or a range
/// that doesn't read ("E14: Invalid address").
[[nodiscard]] ftk::Result<ExCommand> parse_ex(std::string_view line);

/// The whole names of the commands Vim's line takes here, in the order a
/// list of them shows.
[[nodiscard]] std::span<const std::string_view> ex_commands();

} // namespace fjedit::vim
