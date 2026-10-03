#include "fjedit/core/structure.hpp"

#include <algorithm>
#include <cstddef>

namespace fjedit {

namespace {

// How far a bracket's pair is looked for.
constexpr std::size_t SEARCH_LINES = 10'000;

// The bracket `c` pairs with, and whether `c` opens; nothing for others.
struct Kind {
    char open;
    char close;
};

std::optional<Kind> bracket_kind(char c) {
    switch (c) {
        case '(': case ')': return Kind{'(', ')'};
        case '[': case ']': return Kind{'[', ']'};
        case '{': case '}': return Kind{'{', '}'};
        default: return std::nullopt;
    }
}

// The bracket at `at` that counts: one outside strings and comments.
std::optional<Kind> bracket_at(const Buffer& buffer, const Syntax& syntax, Position at) {
    const std::string_view line = buffer.line(at.line);
    if (at.byte >= line.size()) return std::nullopt;
    const std::optional<Kind> kind = bracket_kind(line[at.byte]);
    if (!kind || in_string_or_comment(syntax, at.line, at.byte)) return std::nullopt;
    return kind;
}

// The bracket that pairs with the one at `from`: forwards from an opener,
// backwards from a closer, counting brackets of its kind on the way.
std::optional<Position> pair_of(const Buffer& buffer, const Syntax& syntax, Position from, Kind kind) {
    const bool forward = buffer.line(from.line)[from.byte] == kind.open;
    const char same = forward ? kind.open : kind.close;
    const char other = forward ? kind.close : kind.open;
    std::size_t depth = 0;

    std::size_t line = from.line;
    std::size_t searched = 0;
    while (searched++ < SEARCH_LINES) {
        const std::string_view text = buffer.line(line);
        auto visit = [&](std::size_t byte) -> bool {
            const char c = text[byte];
            if ((c != same && c != other) || in_string_or_comment(syntax, line, byte)) return false;
            if (c == same) {
                ++depth;
                return false;
            }
            return --depth == 0;
        };
        if (forward) {
            for (std::size_t byte = line == from.line ? from.byte : 0; byte < text.size(); ++byte) {
                if (visit(byte)) return Position{line, byte};
            }
            if (++line >= buffer.line_count()) return std::nullopt;
        } else {
            for (std::size_t byte = line == from.line ? from.byte + 1 : text.size(); byte-- > 0;) {
                if (visit(byte)) return Position{line, byte};
            }
            if (line-- == 0) return std::nullopt;
        }
    }
    return std::nullopt;
}

} // namespace

bool in_string_or_comment(const Syntax& syntax, std::size_t line, std::size_t byte) {
    const std::span<const Token> tokens = syntax.tokens(line);
    const auto after = std::upper_bound(tokens.begin(), tokens.end(), byte,
                                        [](std::size_t b, const Token& token) { return b < token.begin; });
    if (after == tokens.begin()) return false;
    const Token& token = *(after - 1);
    return byte < token.end && (token.kind == TokenKind::String || token.kind == TokenKind::Comment);
}

std::optional<BracketPair> matching_bracket(const Buffer& buffer, const Syntax& syntax, Position at) {
    Position bracket = at;
    std::optional<Kind> kind = bracket_at(buffer, syntax, at);
    if (!kind && at.byte > 0) {
        bracket = {at.line, at.byte - 1};
        kind = bracket_at(buffer, syntax, bracket);
    }
    if (!kind) return std::nullopt;

    const std::optional<Position> pair = pair_of(buffer, syntax, bracket, *kind);
    if (!pair) return std::nullopt;
    return bracket < *pair ? BracketPair{bracket, *pair} : BracketPair{*pair, bracket};
}

} // namespace fjedit
