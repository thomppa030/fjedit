#pragma once

// The lexer every C-like language shares: comments, strings, numbers, the
// preprocessor, names and punctuation, told apart by the language's rules.
// A name the language knows as a type is one, even called (GLSL's vec3(...));
// otherwise a name before `(` is a function, one before `::` or written
// LikeThis a type. Inside fjedit-core only.

#include "fjedit/core/syntax.hpp"

#include <span>
#include <string_view>

namespace fjedit::lex {

struct CLikeRules {
    /// The language's keywords, in order.
    std::span<const std::string_view> keywords;
    /// Names that are types in any program, in order (GLSL's vec3).
    std::span<const std::string_view> types{};
    /// Names beginning with this are meta (FJSL's hint_range).
    std::string_view meta_prefix{};
    /// The next name after one of these is a keyword (FJSL's shader_type
    /// spatial), in order.
    std::span<const std::string_view> keyword_next{};
    /// The names after one of these, up to the next `;`, are keywords
    /// (FJSL's render_mode unlit, cutout), in order.
    std::span<const std::string_view> keywords_to_semicolon{};
    /// C++'s strings: raw strings, and the prefixes u8, u, U and L.
    bool cpp_strings{false};
    /// C++'s attributes, [[nodiscard]].
    bool attributes{false};
};

class CLikeLexer final : public Lexer {
public:
    explicit CLikeLexer(CLikeRules rules) : rules_(rules) {}

    [[nodiscard]] LineState lex(std::string_view line, const LineState& state,
                                std::vector<Token>& tokens) const override;

private:
    CLikeRules rules_;
};

} // namespace fjedit::lex
