#pragma once

// Where a piece of text occurs in a buffer. Find and replace, the next match
// and every match all search with these.
//
//     for (const fjedit::Span& match : fjedit::find_all(buffer, "spin_speed", {.whole_word = true})) mark(match);

#include "fjedit/core/buffer.hpp"

#include <optional>
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
};

/// Every place `needle` occurs, in order and apart: after a match, the
/// search goes on from its end. Lines in `needle` break at \n. An empty
/// needle occurs nowhere.
[[nodiscard]] std::vector<Span> find_all(const Buffer& buffer, std::string_view needle, FindOptions options = {});

/// The first place `needle` occurs at or after `from`, or failing that, the
/// first from the start of the text; nothing when it occurs nowhere.
[[nodiscard]] std::optional<Span> find_next(const Buffer& buffer, std::string_view needle, Position from,
                                            FindOptions options = {});

/// The word at `at`: the run of word characters it is in or at either end
/// of, preferring the one after it; empty when there is none.
[[nodiscard]] Span word_at(const Buffer& buffer, Position at);

} // namespace fjedit
