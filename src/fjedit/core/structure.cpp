#include "fjedit/core/structure.hpp"

#include "fjedit/core/layout.hpp"

#include <algorithm>
#include <cstddef>
#include <map>

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

// Whether the character at `byte` of `line` counts as code: outside strings
// and comments, as far as `syntax` (when there is one) tells.
bool code_at(const Syntax* syntax, std::size_t line, std::size_t byte) {
    return syntax == nullptr || !in_string_or_comment(*syntax, line, byte);
}

// Whether `line` holds nothing but spaces and tabs.
bool blank(std::string_view line) {
    return line.find_first_not_of(" \t") == std::string_view::npos;
}

// Whether line `index` is all comment: what isn't space is one comment
// token running to the line's end.
bool comment_line(const Buffer& buffer, const Syntax& syntax, std::size_t index) {
    const std::string_view line = buffer.line(index);
    const std::size_t first = line.find_first_not_of(" \t");
    if (first == std::string_view::npos) return false;
    for (const Token& token : syntax.tokens(index)) {
        if (token.begin <= first && first < token.end) return token.kind == TokenKind::Comment && token.end == line.size();
    }
    return false;
}

// Each line a bracket opens on, and the furthest line one of its brackets
// closes on, past strings and comments.
void bracket_folds(const Buffer& buffer, const Syntax& syntax, std::map<std::size_t, std::size_t>& folds) {
    struct Open {
        char close;
        std::size_t line;
    };
    std::vector<Open> open;
    for (std::size_t line = 0; line < buffer.line_count(); ++line) {
        const std::string_view text = buffer.line(line);
        for (std::size_t byte = 0; byte < text.size(); ++byte) {
            const std::optional<Kind> kind = bracket_kind(text[byte]);
            if (!kind || in_string_or_comment(syntax, line, byte)) continue;
            if (text[byte] == kind->open) {
                open.push_back({.close = kind->close, .line = line});
                continue;
            }
            // A closer pairs with the nearest opener of its kind; openers
            // left unclosed above that one are dropped with it.
            const auto pair = std::find_if(open.rbegin(), open.rend(), [&](const Open& o) { return o.close == text[byte]; });
            if (pair == open.rend()) continue;
            const std::size_t first = pair->line;
            open.erase(std::prev(pair.base()), open.end());
            if (first < line) folds[first] = std::max(folds[first], line);
        }
    }
}

// Each line followed by lines indented deeper, and the last of those
// (blank lines between are part of the fold, blank lines after it not).
void indentation_folds(const Buffer& buffer, std::size_t tab_width, std::map<std::size_t, std::size_t>& folds) {
    struct Open {
        std::size_t indent;
        std::size_t line;
    };
    std::vector<Open> open;
    std::size_t last_text = 0;
    auto close_to = [&](std::size_t indent) {
        while (!open.empty() && indent <= open.back().indent) {
            if (last_text > open.back().line) folds[open.back().line] = last_text;
            open.pop_back();
        }
    };
    for (std::size_t line = 0; line < buffer.line_count(); ++line) {
        const std::string_view text = buffer.line(line);
        if (blank(text)) continue;
        const std::size_t indent = column_of(text, text.find_first_not_of(" \t"), tab_width);
        close_to(indent);
        open.push_back({.indent = indent, .line = line});
        last_text = line;
    }
    close_to(0);
}

// Every run of two comment lines or more.
void comment_folds(const Buffer& buffer, const Syntax& syntax, std::map<std::size_t, std::size_t>& folds) {
    std::size_t line = 0;
    while (line < buffer.line_count()) {
        if (!comment_line(buffer, syntax, line)) {
            ++line;
            continue;
        }
        const std::size_t first = line;
        while (line + 1 < buffer.line_count() && comment_line(buffer, syntax, line + 1)) ++line;
        if (line > first) folds[first] = std::max(folds[first], line);
        ++line;
    }
}

} // namespace

std::vector<FoldRange> fold_ranges(const Buffer& buffer, const Syntax& syntax, Folding folding, std::size_t tab_width) {
    std::map<std::size_t, std::size_t> folds;
    if (folding == Folding::Brackets) {
        bracket_folds(buffer, syntax, folds);
    } else {
        indentation_folds(buffer, tab_width, folds);
    }
    comment_folds(buffer, syntax, folds);

    std::vector<FoldRange> ranges;
    ranges.reserve(folds.size());
    for (const auto& [first, last] : folds) ranges.push_back({.first = first, .last = last});
    return ranges;
}

std::optional<Position> enclosing_opener(const Buffer& buffer, const Syntax* syntax, Position at, char open) {
    const std::optional<Kind> kind = bracket_kind(open);
    if (!kind || open != kind->open) return std::nullopt;
    std::size_t depth = 0;
    std::size_t line = at.line;
    for (std::size_t searched = 0; searched < SEARCH_LINES; ++searched) {
        const std::string_view text = buffer.line(line);
        for (std::size_t byte = line == at.line ? std::min(at.byte, text.size()) : text.size(); byte-- > 0;) {
            const char c = text[byte];
            if ((c != kind->open && c != kind->close) || !code_at(syntax, line, byte)) continue;
            if (c == kind->close) {
                ++depth;
            } else if (depth == 0) {
                return Position{line, byte};
            } else {
                --depth;
            }
        }
        if (line-- == 0) break;
    }
    return std::nullopt;
}

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
