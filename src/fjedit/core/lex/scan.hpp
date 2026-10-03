#pragma once

// What the lexers share: reading names, numbers and quoted text off a line,
// and adding tokens. Inside fjedit-core only.

#include "fjedit/core/syntax.hpp"

#include <algorithm>
#include <span>
#include <string_view>
#include <vector>

namespace fjedit::lex {

[[nodiscard]] inline bool name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}

[[nodiscard]] inline bool digit(char c) {
    return c >= '0' && c <= '9';
}

[[nodiscard]] inline bool name_char(char c) {
    return name_start(c) || digit(c);
}

/// Where the name beginning at `at` ends. Bytes past ASCII belong to
/// names, so names in any script read whole.
[[nodiscard]] inline std::size_t name_end(std::string_view line, std::size_t at) {
    while (at < line.size() && name_char(line[at])) ++at;
    return at;
}

/// Where the number beginning at `at` ends: digits, a point, letters for
/// a base, a suffix or a unit ("0x1F", "90.0f", "1e-3", "0.15s", "1vw"),
/// separators ('), and a sign after an exponent.
[[nodiscard]] inline std::size_t number_end(std::string_view line, std::size_t at) {
    const bool hex = line.substr(at, 2) == "0x" || line.substr(at, 2) == "0X";
    ++at;
    while (at < line.size()) {
        const char c = line[at];
        const char before = line[at - 1];
        const bool exponent = hex ? (before == 'p' || before == 'P') : (before == 'e' || before == 'E');
        if (name_char(c) || c == '.' || c == '\'' || ((c == '+' || c == '-') && exponent)) {
            ++at;
        } else {
            break;
        }
    }
    return at;
}

/// Where the text quoted from the quote at `at` ends: past its closing
/// quote, a backslash escaping the character after it. Sets `closed` to
/// whether it closed on this line; if not, it runs to the line's end.
[[nodiscard]] inline std::size_t quoted_end(std::string_view line, std::size_t at, bool& closed) {
    const char quote = line[at];
    for (std::size_t i = at + 1; i < line.size(); ++i) {
        if (line[i] == '\\') {
            ++i;
        } else if (line[i] == quote) {
            closed = true;
            return i + 1;
        }
    }
    closed = false;
    return line.size();
}

/// The first byte from `at` on that isn't a space or a tab.
[[nodiscard]] inline std::size_t skip_space(std::string_view line, std::size_t at) {
    while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) ++at;
    return at;
}

/// Whether `word` is in `sorted`, a list in order.
[[nodiscard]] inline bool one_of(std::span<const std::string_view> sorted, std::string_view word) {
    return std::binary_search(sorted.begin(), sorted.end(), word);
}

/// Adds a token, as one with the token before when they touch and are of
/// a kind (an operator of two characters, say). Plain text is left out.
inline void add(std::vector<Token>& tokens, std::size_t begin, std::size_t end, TokenKind kind) {
    if (kind == TokenKind::Text || begin >= end) return;
    if (!tokens.empty() && tokens.back().end == begin && tokens.back().kind == kind) {
        tokens.back().end = end;
        return;
    }
    tokens.push_back({.begin = begin, .end = end, .kind = kind});
}

/// Whether `c` is punctuation of a C-like language: brackets and operators.
[[nodiscard]] inline bool c_punctuation(char c) {
    return std::string_view("{}[]();,.<>+-*/%=!&|^~?:#\\").find(c) != std::string_view::npos;
}

} // namespace fjedit::lex
