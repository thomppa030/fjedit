#pragma once

// The text's colours: each line cut into tokens of a kind, by a lexer for
// its language, one line at a time. A line begins in the state the line
// before it ended in (inside a block comment, a raw string, a tag), so a
// Syntax keeps every line's tokens and end state, and after an edit lexes
// again only from the first line changed until a line ends as it did before.
//
//     fjedit::Syntax syntax(language.lexer, buffer);
//     const fjedit::Edit undo = buffer.apply(edit);
//     syntax.edited(buffer, edit);
//     for (const fjedit::Token& token : syntax.tokens(line)) paint(token);

#include "fjedit/core/buffer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit {

/// What a stretch of text is, for its colour.
enum class TokenKind : uint8_t {
    /// Names and anything else plain.
    Text,
    Keyword,
    /// A type, or a namespace or class a name is looked up in.
    Type,
    /// A function called or defined.
    Function,
    String,
    Number,
    Comment,
    /// What speaks to the compiler rather than the program: #include and
    /// the preprocessor, attributes, FJSL's hints, a stylesheet's at-rules.
    Meta,
    Punctuation,
    /// A field: a struct's member, a JSON key, a markup attribute, a
    /// stylesheet property.
    Member,
    /// A shader's uniform. Only a language service knows these.
    Uniform,
    /// A name the engine gives a shader (ALBEDO, NdotL). Only a language
    /// service knows these.
    Builtin,
    /// A template's or a cast's angle bracket, which only a language
    /// service tells from a less-than; coloured as punctuation.
    Bracket,
};

/// A stretch of one line: bytes `begin` up to `end`. A lexer leaves out
/// plain text, so the gaps between tokens are TokenKind::Text.
struct Token {
    std::size_t begin{0};
    std::size_t end{0};
    TokenKind kind{TokenKind::Text};

    bool operator==(const Token&) const = default;
};

/// Where a lexer is at the end of a line, which the next line begins in:
/// what it is inside of, and any text that needs (a raw string's end
/// marker). Each lexer gives `mode` its own meanings; zero is outside
/// everything.
struct LineState {
    uint8_t mode{0};
    std::string data{};

    bool operator==(const LineState&) const = default;
};

class Lexer {
public:
    virtual ~Lexer() = default;

    /// Appends the tokens of `line`, which begins in `state`, in order and
    /// apart, and returns the state it ends in.
    [[nodiscard]] virtual LineState lex(std::string_view line, const LineState& state,
                                        std::vector<Token>& tokens) const = 0;
};

class Syntax {
public:
    /// Lexes all of `buffer` with `lexer`, which must outlive the Syntax.
    Syntax(const Lexer& lexer, const Buffer& buffer);

    /// Brings the tokens up to date with `buffer`, in which `edit` was just
    /// made.
    void edited(const Buffer& buffer, const Edit& edit);

    /// Lexes all of `buffer` again: for a text replaced as a whole.
    void reset(const Buffer& buffer);

    /// The tokens of line `index`, in order.
    [[nodiscard]] std::span<const Token> tokens(std::size_t index) const { return tokens_[index]; }

    /// The state line `index` ends in.
    [[nodiscard]] const LineState& end_state(std::size_t index) const { return ends_[index]; }

    [[nodiscard]] std::size_t line_count() const { return tokens_.size(); }

private:
    // Lexes line `index` from the state the line before it ended in.
    void lex(const Buffer& buffer, std::size_t index);

    const Lexer* lexer_;
    std::vector<std::vector<Token>> tokens_;
    std::vector<LineState> ends_;
};

/// A line's tokens with `over` laid on `under`: where an `over` token is,
/// its kind, and `under`'s elsewhere, a token of `under` cut where one of
/// `over` begins or ends in it. Both in order, neither overlapping itself:
/// what a language service knows of a line over what its lexer saw.
[[nodiscard]] std::vector<Token> overlaid(std::span<const Token> under, std::span<const Token> over);

} // namespace fjedit
