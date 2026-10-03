#include "fjedit/core/lex/c_like.hpp"

#include "fjedit/core/lex/scan.hpp"

#include <algorithm>
#include <array>

namespace fjedit::lex {

namespace {

// Prefixes a C++ string or character may have.
constexpr auto STRING_PREFIXES = std::to_array<std::string_view>({"L", "U", "u", "u8"});
static_assert(std::ranges::is_sorted(STRING_PREFIXES));

enum Mode : uint8_t {
    NORMAL = 0,
    BLOCK_COMMENT = 1,
    // Inside a raw string; its delimiter is in the state's data.
    RAW_STRING = 2,
};

// Written LikeThis: a capital, then a small letter somewhere.
bool pascal_case(std::string_view name) {
    return !name.empty() && name[0] >= 'A' && name[0] <= 'Z' &&
           std::any_of(name.begin(), name.end(), [](char c) { return c >= 'a' && c <= 'z'; });
}

// A preprocessor line's directive (#include and the rest), and an include's
// file; returns where the line's own lexing goes on.
std::size_t directive(std::string_view line, std::vector<Token>& tokens) {
    const std::size_t hash = skip_space(line, 0);
    if (hash >= line.size() || line[hash] != '#') return 0;
    const std::size_t word = skip_space(line, hash + 1);
    const std::size_t end = name_end(line, word);
    add(tokens, hash, end, TokenKind::Meta);
    if (line.substr(word, end - word) != "include") return end;

    const std::size_t file = skip_space(line, end);
    if (file < line.size() && (line[file] == '<' || line[file] == '"')) {
        const std::size_t close = line.find(line[file] == '<' ? '>' : '"', file + 1);
        const std::size_t stop = close == std::string_view::npos ? line.size() : close + 1;
        add(tokens, file, stop, TokenKind::String);
        return stop;
    }
    return end;
}

// R"delim( ... )delim", from its prefix at `at`, its quote at `quote`; a raw
// string left open sets `after` to go on in.
std::size_t raw_string(std::string_view line, std::size_t at, std::size_t quote, std::vector<Token>& tokens,
                       LineState& after) {
    const std::size_t open = line.find('(', quote + 1);
    if (open == std::string_view::npos) {
        add(tokens, at, line.size(), TokenKind::String);
        return line.size();
    }
    const std::string_view delimiter = line.substr(quote + 1, open - quote - 1);
    const std::string close = ")" + std::string(delimiter) + "\"";
    const std::size_t end = line.find(close, open + 1);
    if (end == std::string_view::npos) {
        add(tokens, at, line.size(), TokenKind::String);
        after = {.mode = RAW_STRING, .data = std::string(delimiter)};
        return line.size();
    }
    add(tokens, at, end + close.size(), TokenKind::String);
    return end + close.size();
}

// How names that follow a context word are coloured.
enum class Context : uint8_t { None, NextName, ToSemicolon };

} // namespace

LineState CLikeLexer::lex(std::string_view line, const LineState& state, std::vector<Token>& tokens) const {
    std::size_t i = 0;
    if (state.mode == BLOCK_COMMENT) {
        const std::size_t end = line.find("*/");
        if (end == std::string_view::npos) {
            add(tokens, 0, line.size(), TokenKind::Comment);
            return state;
        }
        add(tokens, 0, end + 2, TokenKind::Comment);
        i = end + 2;
    } else if (state.mode == RAW_STRING) {
        const std::string close = ")" + state.data + "\"";
        const std::size_t end = line.find(close);
        if (end == std::string_view::npos) {
            add(tokens, 0, line.size(), TokenKind::String);
            return state;
        }
        add(tokens, 0, end + close.size(), TokenKind::String);
        i = end + close.size();
    } else {
        i = directive(line, tokens);
    }

    Context context = Context::None;
    while (i < line.size()) {
        const char c = line[i];
        if (c == ' ' || c == '\t') {
            ++i;
        } else if (line.substr(i, 2) == "//") {
            add(tokens, i, line.size(), TokenKind::Comment);
            break;
        } else if (line.substr(i, 2) == "/*") {
            const std::size_t end = line.find("*/", i + 2);
            if (end == std::string_view::npos) {
                add(tokens, i, line.size(), TokenKind::Comment);
                return {.mode = BLOCK_COMMENT};
            }
            add(tokens, i, end + 2, TokenKind::Comment);
            i = end + 2;
        } else if (c == '"' || c == '\'') {
            bool closed = false;
            const std::size_t end = quoted_end(line, i, closed);
            add(tokens, i, end, TokenKind::String);
            i = end;
        } else if (digit(c) || (c == '.' && i + 1 < line.size() && digit(line[i + 1]))) {
            const std::size_t end = number_end(line, i);
            add(tokens, i, end, TokenKind::Number);
            i = end;
        } else if (name_start(c)) {
            const std::size_t end = name_end(line, i);
            const std::string_view word = line.substr(i, end - i);
            const bool quote_next = end < line.size() && (line[end] == '"' || line[end] == '\'');

            if (rules_.cpp_strings && quote_next && line[end] == '"' && word.ends_with('R') &&
                (word.size() == 1 || one_of(STRING_PREFIXES, word.substr(0, word.size() - 1)))) {
                LineState after;
                i = raw_string(line, i, end, tokens, after);
                if (after.mode != NORMAL) return after;
                continue;
            }
            if (rules_.cpp_strings && quote_next && one_of(STRING_PREFIXES, word)) {
                bool closed = false;
                const std::size_t stop = quoted_end(line, end, closed);
                add(tokens, i, stop, TokenKind::String);
                i = stop;
                continue;
            }

            TokenKind kind = TokenKind::Text;
            const std::size_t next = skip_space(line, end);
            if (context != Context::None || one_of(rules_.keywords, word)) {
                kind = TokenKind::Keyword;
            } else if (!rules_.meta_prefix.empty() && word.starts_with(rules_.meta_prefix)) {
                kind = TokenKind::Meta;
            } else if (next < line.size() && line[next] == '(') {
                kind = TokenKind::Function;
            } else if (one_of(rules_.types, word) || line.substr(next, 2) == "::" || pascal_case(word)) {
                kind = TokenKind::Type;
            }
            add(tokens, i, end, kind);

            if (context == Context::NextName) context = Context::None;
            if (one_of(rules_.keyword_next, word)) context = Context::NextName;
            if (one_of(rules_.keywords_to_semicolon, word)) context = Context::ToSemicolon;
            i = end;
        } else if (rules_.attributes && line.substr(i, 2) == "[[") {
            const std::size_t end = line.find("]]", i + 2);
            const std::size_t stop = end == std::string_view::npos ? line.size() : end + 2;
            add(tokens, i, stop, TokenKind::Meta);
            i = stop;
        } else {
            if (c == ';') context = Context::None;
            add(tokens, i, i + 1, c_punctuation(c) ? TokenKind::Punctuation : TokenKind::Text);
            ++i;
        }
    }
    return {};
}

} // namespace fjedit::lex
