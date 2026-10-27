#pragma once

// Where a piece of text occurs in a buffer. Find and replace, the next match
// and every match all search with these.
//
//     for (const fjedit::Span& match : fjedit::find_all(buffer, "spin_speed", {.whole_word = true})) mark(match);

#include "fjedit/core/buffer.hpp"

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit {

struct FindOptions {
    /// When false, letters match whichever their case. Only A to Z have
    /// a case here: other letters match as they are.
    bool match_case{true};
    /// When true, a match that begins or ends with a word character
    /// (fjedit/core/motion.hpp) must not continue a word on that side.
    bool whole_word{false};
    /// When true, the needle is a regular expression, as JavaScript reads
    /// one, matched within a line. Matches that take no text don't count,
    /// and an expression that doesn't parse (regex_valid()) finds nothing.
    bool regex{false};
    /// For a regular expression: the group whose text is the match, 0 for
    /// all the expression takes. A match whose group takes no text doesn't
    /// count, unless `empty`.
    std::size_t group{0};
    /// For a regular expression: matches that take no text count too, as
    /// Vim's `:s/^/# /` needs.
    bool empty{false};
};

/// Every place `needle` occurs, in order and apart: after a match, the
/// search goes on from its end. Lines in `needle` break at \n, unless it is
/// a regular expression. An empty needle occurs nowhere.
[[nodiscard]] std::vector<Span> find_all(const Buffer& buffer, std::string_view needle, FindOptions options = {});

/// The same, only the places that begin on lines `first_line` up to, not
/// including, `end_line`: what a view has in sight.
[[nodiscard]] std::vector<Span> find_in_lines(const Buffer& buffer, std::string_view needle, std::size_t first_line,
                                              std::size_t end_line, FindOptions options = {});

/// A place find_in_text() found, with the line it begins on, without its
/// break.
struct TextMatch {
    Span span;
    std::string_view line;

    bool operator==(const TextMatch&) const = default;
};

/// The places find_all() finds in `text` read into a Buffer, the first
/// `limit` of them, each with its line viewed in `text`: a file's bytes
/// searched as they are, without its lines being built.
[[nodiscard]] std::vector<TextMatch> find_in_text(std::string_view text, std::string_view needle,
                                                  FindOptions options = {},
                                                  std::size_t limit = std::numeric_limits<std::size_t>::max());

/// The first place `needle` occurs at or after `from`, or failing that, the
/// first from the start of the text; nothing when it occurs nowhere.
[[nodiscard]] std::optional<Span> find_next(const Buffer& buffer, std::string_view needle, Position from,
                                            FindOptions options = {});

/// Whether `pattern` parses as a regular expression.
[[nodiscard]] bool regex_valid(std::string_view pattern);

/// What replaces `match`, a place find_all() found for `needle`: `with` as
/// it is, or for a regular expression with $1 and the like (`$&` for the
/// whole match) put in from what the match captured.
[[nodiscard]] std::string replacement(const Buffer& buffer, Span match, std::string_view needle,
                                      std::string_view with, FindOptions options = {});

/// What each group of the regular expression `needle` took where it found
/// `match` (find_all()): all the expression took first, then group by
/// group, empty for one that took nothing. Nothing when `needle` finds no
/// match there.
[[nodiscard]] std::optional<std::vector<std::string>> captured(const Buffer& buffer, Span match, std::string_view needle,
                                                               FindOptions options = {});

/// The word at `at`: the run of word characters it is in or at either end
/// of, preferring the one after it; empty when there is none.
[[nodiscard]] Span word_at(const Buffer& buffer, Position at);

} // namespace fjedit
