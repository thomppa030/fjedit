// FJSS stylesheets, which are written as CSS is. What a word is depends on
// where it stands, so the lexer follows that across lines:
// - in selectors, tags are keywords, .classes and #ids types, :hover meta;
// - in a rule's block, a property's name is a member;
// - after its colon, numbers with their units and #hex colours are numbers,
//   rgba( and var( functions, --custom properties members, other words
//   keywords;
// - an at-rule (@import, @media) is meta, and its own { opens selectors
//   again, where a rule's opens properties.

#include "fjedit/core/lex/lexers.hpp"
#include "fjedit/core/lex/scan.hpp"

namespace fjedit::lex {

namespace {

// Where the lexer stands, in a state's mode: one of the places, and two
// flags.
enum Mode : uint8_t {
    SELECTOR = 0,
    PROPERTY = 1,
    VALUE = 2,
    PLACE = 0x0F,
    // Inside a block comment, which ends back in the place below it.
    COMMENT = 0x10,
    // After an at-rule's name, until its { or ;.
    AT_RULE = 0x20,
};

bool css_name_start(char c) {
    return name_start(c) || c == '-';
}

bool css_name_char(char c) {
    return name_char(c) || c == '-';
}

std::size_t css_name_end(std::string_view line, std::size_t at) {
    while (at < line.size() && css_name_char(line[at])) ++at;
    return at;
}

bool hex_digit(char c) {
    return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

class FjssLexer final : public Lexer {
public:
    [[nodiscard]] LineState lex(std::string_view line, const LineState& state, std::vector<Token>& tokens) const override {
        uint8_t mode = state.mode;
        std::size_t i = 0;
        if ((mode & COMMENT) != 0) {
            const std::size_t end = line.find("*/");
            if (end == std::string_view::npos) {
                add(tokens, 0, line.size(), TokenKind::Comment);
                return state;
            }
            add(tokens, 0, end + 2, TokenKind::Comment);
            i = end + 2;
            mode = static_cast<uint8_t>(mode & ~COMMENT);
        }

        while (i < line.size()) {
            const char c = line[i];
            const uint8_t place = mode & PLACE;
            if (c == ' ' || c == '\t') {
                ++i;
            } else if (line.substr(i, 2) == "/*") {
                const std::size_t end = line.find("*/", i + 2);
                if (end == std::string_view::npos) {
                    add(tokens, i, line.size(), TokenKind::Comment);
                    return {.mode = static_cast<uint8_t>(mode | COMMENT)};
                }
                add(tokens, i, end + 2, TokenKind::Comment);
                i = end + 2;
            } else if (c == '"' || c == '\'') {
                bool closed = false;
                const std::size_t end = quoted_end(line, i, closed);
                add(tokens, i, end, TokenKind::String);
                i = end;
            } else if (c == '{') {
                add(tokens, i, i + 1, TokenKind::Punctuation);
                mode = (mode & AT_RULE) != 0 ? SELECTOR : PROPERTY;
                ++i;
            } else if (c == '}') {
                add(tokens, i, i + 1, TokenKind::Punctuation);
                mode = SELECTOR;
                ++i;
            } else if (c == ';') {
                add(tokens, i, i + 1, TokenKind::Punctuation);
                // A value ends at its ;, and so does an at-rule without a block.
                mode = place == VALUE ? static_cast<uint8_t>(PROPERTY) : place;
                ++i;
            } else if ((mode & AT_RULE) != 0 || place == SELECTOR) {
                i = selector(line, i, mode, tokens);
            } else if (place == PROPERTY) {
                i = property(line, i, mode, tokens);
            } else {
                i = value(line, i, tokens);
            }
        }
        return {.mode = mode};
    }

private:
    // A number from `at`, with its unit or a percent sign.
    static std::size_t number(std::string_view line, std::size_t at, std::vector<Token>& tokens) {
        std::size_t end = number_end(line, at);
        if (end < line.size() && line[end] == '%') ++end;
        add(tokens, at, end, TokenKind::Number);
        return end;
    }

    // In selectors, or an at-rule before its { or ;.
    static std::size_t selector(std::string_view line, std::size_t i, uint8_t& mode, std::vector<Token>& tokens) {
        const char c = line[i];
        const bool at_rule = (mode & AT_RULE) != 0;
        if (c == '@') {
            const std::size_t end = css_name_end(line, i + 1);
            add(tokens, i, end, TokenKind::Meta);
            mode = static_cast<uint8_t>(mode | AT_RULE);
            return end;
        }
        if ((c == '.' || c == '#') && i + 1 < line.size() && css_name_start(line[i + 1])) {
            const std::size_t end = css_name_end(line, i + 1);
            add(tokens, i, end, TokenKind::Type);
            return end;
        }
        if (c == ':' && !at_rule) {
            std::size_t start = i + 1;
            if (start < line.size() && line[start] == ':') ++start;
            const std::size_t end = css_name_end(line, start);
            add(tokens, i, end, end > start ? TokenKind::Meta : TokenKind::Punctuation);
            return std::max(end, i + 1);
        }
        if (digit(c) || (c == '.' && i + 1 < line.size() && digit(line[i + 1]))) return number(line, i, tokens);
        if (css_name_start(c)) {
            const std::size_t end = css_name_end(line, i);
            // In an at-rule, a name before a colon is a feature: (max-width: 800)
            const std::size_t next = skip_space(line, end);
            const bool feature = at_rule && next < line.size() && line[next] == ':';
            add(tokens, i, end, feature ? TokenKind::Member : TokenKind::Keyword);
            return end;
        }
        add(tokens, i, i + 1, TokenKind::Punctuation);
        return i + 1;
    }

    // In a rule's block, before a property's colon.
    static std::size_t property(std::string_view line, std::size_t i, uint8_t& mode, std::vector<Token>& tokens) {
        const char c = line[i];
        if (css_name_start(c)) {
            const std::size_t end = css_name_end(line, i);
            add(tokens, i, end, TokenKind::Member);
            return end;
        }
        if (c == ':') mode = VALUE;
        add(tokens, i, i + 1, TokenKind::Punctuation);
        return i + 1;
    }

    // After a property's colon, up to its ; or the block's }.
    static std::size_t value(std::string_view line, std::size_t i, std::vector<Token>& tokens) {
        const char c = line[i];
        if (c == '#' && i + 1 < line.size() && hex_digit(line[i + 1])) {
            std::size_t end = i + 1;
            while (end < line.size() && hex_digit(line[end])) ++end;
            add(tokens, i, end, TokenKind::Number);
            return end;
        }
        const bool negative = c == '-' && i + 1 < line.size() && (digit(line[i + 1]) || line[i + 1] == '.');
        if (digit(c) || negative || (c == '.' && i + 1 < line.size() && digit(line[i + 1]))) {
            return number(line, i, tokens);
        }
        if (c == '!') {
            const std::size_t end = css_name_end(line, i + 1);
            add(tokens, i, end, end > i + 1 ? TokenKind::Meta : TokenKind::Punctuation);
            return std::max(end, i + 1);
        }
        if (css_name_start(c)) {
            const std::size_t end = css_name_end(line, i);
            const std::string_view word = line.substr(i, end - i);
            TokenKind kind = TokenKind::Keyword;
            if (end < line.size() && line[end] == '(') {
                kind = TokenKind::Function;
            } else if (word.starts_with("--")) {
                kind = TokenKind::Member;
            }
            add(tokens, i, end, kind);
            return end;
        }
        add(tokens, i, i + 1, TokenKind::Punctuation);
        return i + 1;
    }
};

} // namespace

const Lexer& fjss() {
    static const FjssLexer lexer;
    return lexer;
}

} // namespace fjedit::lex
