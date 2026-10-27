#include "fjedit/core/structure.hpp"

#include "fjedit/core/layout.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <string_view>

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

// Which of ( [ { < a bracket is and whether it opens, for pairing every kind
// in one depth.
struct Side {
    uint8_t kind;
    bool opens;
};

// The kind Side gives angle brackets, which only a language service names.
constexpr uint8_t ANGLE = 3;

std::optional<Side> side_of(char c) {
    switch (c) {
        case '(': return Side{0, true};
        case ')': return Side{0, false};
        case '[': return Side{1, true};
        case ']': return Side{1, false};
        case '{': return Side{2, true};
        case '}': return Side{2, false};
        case '<': return Side{ANGLE, true};
        case '>': return Side{ANGLE, false};
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

// How far back the head of a block is read for a function's name, and how
// many blocks out from a line the function around it is looked for.
constexpr std::size_t HEAD_LINES = 50;
constexpr std::size_t BLOCKS_OUT = 64;

// Words a bracket follows that don't name a function.
constexpr std::string_view NOT_FUNCTIONS[] = {
    "if", "for", "while", "switch", "catch", "return", "sizeof", "alignof", "decltype", "noexcept", "requires",
    "static_assert", "typeid",
};

bool name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '~';
}

// Where the head of the block opened at `opener` begins: after the code
// before it that ends a statement or a block, or after a preprocessor line.
Position head_start(const Buffer& buffer, const Syntax& syntax, Position opener) {
    std::size_t line = opener.line;
    std::size_t byte = opener.byte;
    for (std::size_t looked = 0; looked <= HEAD_LINES; ++looked) {
        const std::string_view text = buffer.line(line);
        const std::size_t first = text.find_first_not_of(" \t");
        if (line != opener.line && first != std::string_view::npos && text[first] == '#') return {line + 1, 0};
        while (byte-- > 0) {
            const char c = text[byte];
            if ((c == ';' || c == '{' || c == '}') && code_at(&syntax, line, byte)) return {line, byte + 1};
        }
        if (line == 0) return {0, 0};
        --line;
        byte = buffer.line(line).size();
    }
    return {line, 0};
}

// The name of the function whose body the brace at `opener` opens: the
// name before the head's first bracket, when that bracket pairs before the
// brace. Nothing when the brace opens something else: a class, a branch, a
// lambda inside a call.
std::optional<std::string> function_opened(const Buffer& buffer, const Syntax& syntax, Position opener) {
    for (Position at = head_start(buffer, syntax, opener); at < opener;) {
        const std::string_view text = buffer.line(at.line);
        if (at.byte >= text.size()) {
            at = {at.line + 1, 0};
            continue;
        }
        if (text[at.byte] != '(' || !code_at(&syntax, at.line, at.byte)) {
            ++at.byte;
            continue;
        }
        std::size_t end = at.byte;
        while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
        std::size_t begin = end;
        while (begin > 0 && name_char(text[begin - 1])) --begin;
        const std::string_view name = text.substr(begin, end - begin);
        if (name.empty() || (name[0] >= '0' && name[0] <= '9') || std::ranges::contains(NOT_FUNCTIONS, name)) {
            return std::nullopt;
        }
        const std::optional<Position> close = pair_of(buffer, syntax, at, Kind{'(', ')'});
        if (!close || !(*close < opener)) return std::nullopt;
        return std::string(name) + "()";
    }
    return std::nullopt;
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

Indent detect_indent(const Buffer& buffer, Indent fallback) {
    std::size_t tab_lines = 0;
    std::size_t space_lines = 0;
    // How often each step from 2 to 8 spaces was taken between lines.
    std::array<std::size_t, 9> steps{};
    std::size_t previous = 0;
    for (std::size_t index = 0; index < buffer.line_count(); ++index) {
        const std::string_view line = buffer.line(index);
        if (blank(line)) continue;
        if (line[0] == '\t') {
            ++tab_lines;
            continue;
        }
        const std::size_t spaces = line.find_first_not_of(' ');
        if (spaces > 0) ++space_lines;
        if (line[spaces] == '\t') continue; // spaces then tabs: no step to read
        const std::size_t step = spaces > previous ? spaces - previous : previous - spaces;
        if (step >= 2 && step < steps.size()) ++steps[step];
        previous = spaces;
    }

    if (tab_lines == 0 && space_lines == 0) return fallback;
    if (tab_lines > space_lines) return {.tabs = true, .width = fallback.width};

    // The step taken most; on a tie, the more usual width.
    std::size_t width = 0;
    for (const std::size_t step : {4, 2, 8, 3, 6, 5, 7}) {
        if (steps[step] > (width == 0 ? 0 : steps[width])) width = step;
    }
    return {.tabs = false, .width = width == 0 ? fallback.width : width};
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

std::span<const LeveledBracket> BracketLevels::on_line(std::size_t line) const {
    if (line + 1 >= line_starts.size()) return {};
    return std::span(brackets).subspan(line_starts[line], line_starts[line + 1] - line_starts[line]);
}

BracketLevels bracket_levels(const Buffer& buffer, const Syntax& syntax, std::span<const std::vector<Token>> named) {
    constexpr std::string_view BRACKETS = "()[]{}<>";
    constexpr std::size_t NONE = std::numeric_limits<std::size_t>::max();
    BracketLevels levels;
    levels.line_starts.reserve(buffer.line_count() + 1);
    // For each bracket, by its place in `levels.brackets`: its side, and
    // the bracket it pairs with.
    std::vector<Side> sides;
    std::vector<std::size_t> mates;
    // The openers still open, the innermost last.
    std::vector<std::size_t> open;
    for (std::size_t line = 0; line < buffer.line_count(); ++line) {
        levels.line_starts.push_back(levels.brackets.size());
        const std::string_view text = buffer.line(line);
        const std::span<const Token> lexed = syntax.tokens(line);
        const std::span<const Token> service = line < named.size() ? std::span<const Token>(named[line]) : std::span<const Token>();
        std::size_t lexed_at = 0;
        std::size_t service_at = 0;
        for (std::size_t byte = text.find_first_of(BRACKETS); byte != std::string_view::npos;
             byte = text.find_first_of(BRACKETS, byte + 1)) {
            const Side side = *side_of(text[byte]);
            if (side.kind == ANGLE) {
                while (service_at < service.size() && service[service_at].end <= byte) ++service_at;
                if (service_at == service.size() || service[service_at].begin > byte ||
                    service[service_at].kind != TokenKind::Bracket) {
                    continue;
                }
            } else {
                while (lexed_at < lexed.size() && lexed[lexed_at].end <= byte) ++lexed_at;
                if (lexed_at < lexed.size() && lexed[lexed_at].begin <= byte &&
                    (lexed[lexed_at].kind == TokenKind::String || lexed[lexed_at].kind == TokenKind::Comment)) {
                    continue;
                }
            }
            const std::size_t index = levels.brackets.size();
            levels.brackets.push_back({.at = {line, byte}, .level = std::nullopt});
            sides.push_back(side);
            mates.push_back(NONE);
            if (side.opens) {
                open.push_back(index);
                continue;
            }
            const auto opener = std::find_if(open.rbegin(), open.rend(), [&](std::size_t o) { return sides[o].kind == side.kind; });
            if (opener == open.rend()) continue;
            mates[*opener] = index;
            mates[index] = *opener;
            open.erase(std::next(opener).base(), open.end());
        }
    }
    levels.line_starts.push_back(levels.brackets.size());

    // Paired brackets nest, so the pairs open as one goes give each its level.
    uint32_t depth = 0;
    for (std::size_t index = 0; index < levels.brackets.size(); ++index) {
        if (mates[index] == NONE) continue;
        levels.brackets[index].level = sides[index].opens ? depth++ : --depth;
    }
    return levels;
}

std::optional<std::string> enclosing_function(const Buffer& buffer, const Syntax& syntax, std::size_t line) {
    // From the line's end, unless it begins by closing a block: then it is
    // still that block's.
    const std::string_view text = buffer.line(line);
    const std::size_t first = text.find_first_not_of(" \t");
    Position at{line, first != std::string_view::npos && text[first] == '}' ? first : text.size()};
    for (std::size_t out = 0; out < BLOCKS_OUT; ++out) {
        const std::optional<Position> opener = enclosing_opener(buffer, &syntax, at, '{');
        if (!opener) return std::nullopt;
        if (std::optional<std::string> name = function_opened(buffer, syntax, *opener)) return name;
        at = *opener;
    }
    return std::nullopt;
}

} // namespace fjedit
