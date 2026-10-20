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

// What follows a command's name, read as the command reads it, apart from
// running it: the engine runs what these read, and the `:` line's words
// say what they read before Enter.

/// A pattern typed up to the first `delimiter` neither escaped nor in a
/// collection, and what follows that one: a search's offset, or the rest of
/// :s or :g. `ended` when the delimiter was typed.
struct Delimited {
    std::string_view pattern;
    std::string_view rest;
    bool ended{false};
};
[[nodiscard]] Delimited split_pattern(std::string_view text, char delimiter);

/// Whether `c` may stand around :s's and :g's pattern: anything but a
/// letter, a digit, a blank, a backslash, ", | and &.
[[nodiscard]] bool pattern_delimiter(char c);

/// A count after a command's name or flags (`:d 3`), which makes its lines
/// that many from its range's last: none when nothing but blanks follows.
/// An error worded as Vim's for anything else, or for a count of 0.
[[nodiscard]] ftk::Result<std::optional<std::size_t>> read_count(std::string_view argument);

/// :d's and :y's argument: a register's name, then a count.
struct RegisterCount {
    char reg{'"'};
    std::optional<std::size_t> count;
};
[[nodiscard]] ftk::Result<RegisterCount> read_register_count(std::string_view argument);

/// :>'s and :<'s argument: as many more of `shift` as levels after the
/// first, then a count.
struct Shift {
    std::size_t levels{1};
    std::optional<std::size_t> count;
};
[[nodiscard]] ftk::Result<Shift> read_shift(char shift, std::string_view argument);

/// :m's, :t's and :co's argument: the line the lines go below, as one
/// address. "E14: Invalid address" for anything else.
[[nodiscard]] ftk::Result<ExAddress> read_target(std::string_view argument);

/// :s's argument: `/pattern/replacement/flags count`, any delimiter
/// pattern_delimiter() takes in place of the /.
struct SubstituteArgument {
    /// What it looks for (empty for the last search's) and puts in its
    /// place; none for :s alone or with flags alone, which make the last :s
    /// again.
    struct Written {
        std::string pattern;
        std::string with;
    };
    std::optional<Written> written;
    /// Whether every match on a line is replaced: g says yes, & as the last
    /// :s did; the later of them written wins. Unsaid, the first.
    enum class Every : uint8_t { Unsaid, Yes, AsLast };
    Every every{Every::Unsaid};
    /// i and I: the case let go or kept, whatever the settings say.
    std::optional<bool> match_case;
    /// e: nothing said when nothing matches.
    bool quiet{false};
    std::optional<std::size_t> count;
};
[[nodiscard]] ftk::Result<SubstituteArgument> read_substitute(std::string_view argument);

/// :g's and :v's argument: the pattern (empty for the last search's), then
/// the command run at each line it marks.
struct GlobalArgument {
    std::string pattern;
    std::string command;
};
[[nodiscard]] ftk::Result<GlobalArgument> read_global(std::string_view argument);

/// The settings :set has here.
enum class SetOption : uint8_t {
    Number,
    RelativeNumber,
    IgnoreCase,
    SmartCase,
    HlSearch,
    ExpandTab,
    ShiftWidth,
    TabStop,
    ScrollOff,
};

/// One item of :set's argument ("rnu", "noic", "hls!", "so?", "sw=2"): the
/// setting by its whole name, whether it is on or off rather than a number,
/// and what to do with it.
struct SetItem {
    SetOption option{SetOption::Number};
    std::string_view name;
    bool on_off{true};
    enum class Action : uint8_t { On, Off, Toggle, Ask, Value };
    Action action{Action::On};
    /// Action::Value: the number after =.
    std::size_t value{0};
};
/// An error worded as Vim's for a setting it has none of ("E518: Unknown
/// option: foo") or what the setting can't take ("E521: Number required
/// after =: so=x").
[[nodiscard]] ftk::Result<SetItem> read_set_item(std::string_view item);

/// :sort's argument: i lets the case go, n sorts by each line's first
/// number, u keeps each line once.
struct SortArgument {
    bool fold_case{false};
    bool by_number{false};
    bool unique{false};
};
[[nodiscard]] ftk::Result<SortArgument> read_sort(std::string_view argument);

} // namespace fjedit::vim
