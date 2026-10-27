#pragma once

// Lexing in tests: a line's tokens as their text and kind, plain text left
// out, so a check reads as what it colours.
//
//     REQUIRE(colours(lexer, "int a;") == Colours{{"int", K::Keyword}, {";", K::Punctuation}});

#include "fjedit/core/syntax.hpp"

#include <catch2/catch_tostring.hpp>

#include <string>
#include <utility>
#include <vector>

using K = fjedit::TokenKind;
using Colours = std::vector<std::pair<std::string, K>>;

/// The tokens of `line`, begun in `state`, as their text; `end` gets the
/// state it ends in.
inline Colours colours(const fjedit::Lexer& lexer, std::string_view line, const fjedit::LineState& state = {},
                       fjedit::LineState* end = nullptr) {
    std::vector<fjedit::Token> tokens;
    const fjedit::LineState after = lexer.lex(line, state, tokens);
    if (end != nullptr) *end = after;
    Colours out;
    for (const fjedit::Token& token : tokens) out.emplace_back(std::string(line.substr(token.begin, token.end - token.begin)), token.kind);
    return out;
}

template <>
struct Catch::StringMaker<fjedit::TokenKind> {
    static std::string convert(fjedit::TokenKind kind) {
        constexpr const char* NAMES[] = {"Text",    "Keyword", "Type",        "Function", "String",  "Number",
                                         "Comment", "Meta",    "Punctuation", "Member",   "Uniform", "Builtin",
                                         "Bracket"};
        return NAMES[static_cast<std::size_t>(kind)];
    }
};
