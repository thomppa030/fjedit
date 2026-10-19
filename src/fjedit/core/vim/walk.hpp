#pragma once

// How Vim's own code walks the text: a character at a time, each line's end
// a place of its own where its break is, and characters told apart as
// blanks, punctuation and word characters. The word motions and the word
// text objects both walk this way, so `iw` takes a word as `w` and `e` find
// it. For core/vim's own use.

#include "fjedit/core/buffer.hpp"

#include <cstddef>

namespace fjedit::vim::detail {

/// What kind of place a walk is on: a space, a tab or a line's end; any
/// other character of a WORD, or punctuation; a word character.
inline constexpr int BLANK = 0;
inline constexpr int PUNCTUATION = 1;
inline constexpr int WORD = 2;

/// What a step of a walk did: along its line; onto the next line's start
/// (back: onto the line before's end); onto its line's end; nowhere, at the
/// text's edge.
inline constexpr int ALONG = 0;
inline constexpr int ONTO_NEXT_LINE = 1;
inline constexpr int ONTO_LINE_END = 2;
inline constexpr int AT_TEXT_EDGE = -1;

class Walk {
public:
    /// From `at`; `big` walks WORDs, any run of what isn't blank.
    Walk(const Buffer& buffer, Position at, bool big) : buffer_(buffer), at_(at), big_(big) {}

    [[nodiscard]] Position at() const { return at_; }

    /// BLANK, PUNCTUATION or WORD for the place it is on.
    [[nodiscard]] int kind() const;

    [[nodiscard]] bool on_empty_line() const { return at_.byte == 0 && buffer_.line(at_.line).empty(); }
    [[nodiscard]] bool on_last_line() const { return at_.line + 1 == buffer_.line_count(); }

    /// A step on (Vim's inc()): ALONG, ONTO_LINE_END, ONTO_NEXT_LINE, or
    /// AT_TEXT_EDGE at the last line's end, where it stays.
    int forward();

    /// A step back (dec()): ALONG, or ONTO_NEXT_LINE onto the end of the
    /// line before; AT_TEXT_EDGE at the text's start, where it stays.
    int back();

    /// A step on that passes over a non-empty line's end (incl()), and one
    /// back that does (decl()); each says what its last step did.
    int forward_past_line_end();
    int back_past_line_end();

    /// Steps on, or back, over places of `kind`; true when the text's edge
    /// came first.
    bool skip(int kind, bool on);

private:
    const Buffer& buffer_;
    Position at_;
    bool big_;
};

/// Vim's fwd_word(): `count` words on, the last stopping at the end of the
/// line it leaves with `stop_at_line_end`. False where Vim's fails: from
/// the text's last character.
bool forward_words(Walk& walk, std::size_t count, bool stop_at_line_end);

/// Vim's end_word(): to the end of `count` words, a first count that starts
/// on a word's end staying there with `stay`, and stopping on an empty line
/// among blanks with `stop_at_empty_line`. False where Vim's fails: at the
/// text's end.
bool end_of_words(Walk& walk, std::size_t count, bool stay, bool stop_at_empty_line);

/// Vim's bck_word(): back to the start of `count` words. False from the
/// text's start.
bool back_words(Walk& walk, std::size_t count);

/// Vim's bckend_word(): back to the end of `count` words before. False from
/// the text's start.
bool back_ends_of_words(Walk& walk, std::size_t count);

} // namespace fjedit::vim::detail
