#pragma once

// Vim's patterns read onto fjedit's find: what `/`, `?`, `*` and `:s`
// search for, read as Vim reads it (magic, or as \v, \m, \M and \V switch
// it), turned into the regular expression find takes, matched within a
// line; and what `:s` puts in a match's place.
//
//     const ftk::Result<fjedit::vim::Pattern> read = fjedit::vim::read_pattern("\\<spin\\w\\+");
//     if (read) for (const fjedit::Span& match : fjedit::find_all(buffer, read->regex, read->options)) mark(match);

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/find.hpp"

#include "ftk/base/result.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit::vim {

/// Vim's settings that change how a pattern matches.
struct PatternSettings {
    /// `ignorecase`: letters match whichever their case.
    bool ignore_case{false};
    /// `smartcase`: with ignore_case, a pattern with an upper case letter
    /// in it matches case.
    bool smart_case{false};
};

/// A Vim pattern as fjedit's find takes it.
struct Pattern {
    /// The regular expression, in the syntax find reads.
    std::string regex;
    /// How find matches it: as a regular expression, the case as Vim's
    /// settings, \c and \C say, the match the group \zs begins when the
    /// pattern has one.
    FindOptions options;
    /// find's group for each of Vim's, \0 (the match) to the last \( \).
    std::vector<std::size_t> groups;
};

/// `pattern` read as Vim reads it, `settings` its ignorecase and smartcase.
/// An error, as Vim words one, for a pattern that doesn't read, or for what
/// find can't do: a pattern across lines (\n, \_), a position (\%l), a look
/// around (\@), \& and ~.
[[nodiscard]] ftk::Result<Pattern> read_pattern(std::string_view pattern, PatternSettings settings = {});

/// What `pattern` took where find found `match`: the match first, then each
/// of Vim's groups, empty for one that took nothing; nothing when `pattern`
/// finds no match there.
[[nodiscard]] std::optional<std::vector<std::string>> captured(const Buffer& buffer, Span match, const Pattern& pattern);

/// What `:s` puts in a match's place: `with`, a ~ in it first the previous
/// substitute's `previous`, with `&` and `\0` the match and `\1`–`\9` its
/// groups from `taken` (captured()), `\r` and `\n` a line break, `\t` a
/// tab, `\u` and `\l` the next letter's case changed, `\U` and `\L` every
/// letter's until `\E` or `\e`, and a backslash before any other character
/// that character.
[[nodiscard]] std::string expand_replacement(std::string_view with, std::span<const std::string> taken,
                                             std::string_view previous = {});

} // namespace fjedit::vim
