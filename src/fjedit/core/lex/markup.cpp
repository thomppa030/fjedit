// Widget markup (.fjwidget, .fjui), written as HTML is: tag names are
// keywords, attributes (class, on:click, class:active) members and their
// values strings, the {bindings} in values and text meta, comments as
// comments. A tag, a value and a comment may each run across lines.

#include "fjedit/core/lex/lexers.hpp"
#include "fjedit/core/lex/scan.hpp"

namespace fjedit::lex {

namespace {

enum Mode : uint8_t {
    TEXT = 0,
    // Inside a tag, after its name.
    TAG = 1,
    // Inside an attribute's value in double or single quotes.
    DOUBLE_QUOTED = 2,
    SINGLE_QUOTED = 3,
    COMMENT = 4,
};

bool markup_name_char(char c) {
    return name_char(c) || c == '-' || c == ':' || c == '.';
}

std::size_t markup_name_end(std::string_view line, std::size_t at) {
    while (at < line.size() && markup_name_char(line[at])) ++at;
    return at;
}

// Text from `at` up to `end`, its {bindings} meta and the rest `kind`.
void with_bindings(std::string_view line, std::size_t at, std::size_t end, TokenKind kind, std::vector<Token>& tokens) {
    while (at < end) {
        const std::size_t open = line.find('{', at);
        if (open == std::string_view::npos || open >= end) {
            add(tokens, at, end, kind);
            return;
        }
        add(tokens, at, open, kind);
        const std::size_t close = line.find('}', open + 1);
        const std::size_t stop = close == std::string_view::npos || close >= end ? end : close + 1;
        add(tokens, open, stop, TokenKind::Meta);
        at = stop;
    }
}

// A quoted value's text from `at`, its closing `quote` looked for from
// `search` on; returns where it ends, and sets `closed` to whether it closed
// on this line.
std::size_t value(std::string_view line, std::size_t at, std::size_t search, char quote, std::vector<Token>& tokens,
                  bool& closed) {
    const std::size_t close = line.find(quote, search);
    closed = close != std::string_view::npos;
    const std::size_t end = closed ? close + 1 : line.size();
    with_bindings(line, at, end, TokenKind::String, tokens);
    return end;
}

class MarkupLexer final : public Lexer {
public:
    [[nodiscard]] LineState lex(std::string_view line, const LineState& state, std::vector<Token>& tokens) const override {
        uint8_t mode = state.mode;
        std::size_t i = 0;
        while (i < line.size()) {
            if (mode == COMMENT) {
                const std::size_t end = line.find("-->", i);
                if (end == std::string_view::npos) {
                    add(tokens, i, line.size(), TokenKind::Comment);
                    return {.mode = COMMENT};
                }
                add(tokens, i, end + 3, TokenKind::Comment);
                i = end + 3;
                mode = TEXT;
            } else if (mode == DOUBLE_QUOTED || mode == SINGLE_QUOTED) {
                bool closed = false;
                i = value(line, i, i, mode == DOUBLE_QUOTED ? '"' : '\'', tokens, closed);
                if (!closed) return {.mode = mode};
                mode = TAG;
            } else if (mode == TAG) {
                i = tag(line, i, mode, tokens);
            } else {
                i = text(line, i, mode, tokens);
            }
        }
        return {.mode = mode};
    }

private:
    // Between tags: a tag or comment beginning, or text with its bindings.
    static std::size_t text(std::string_view line, std::size_t i, uint8_t& mode, std::vector<Token>& tokens) {
        if (line.substr(i, 4) == "<!--") {
            add(tokens, i, i + 4, TokenKind::Comment);
            mode = COMMENT;
            return i + 4;
        }
        if (line[i] == '<') {
            std::size_t name = i + 1;
            if (name < line.size() && line[name] == '/') ++name;
            add(tokens, i, name, TokenKind::Punctuation);
            const std::size_t end = markup_name_end(line, name);
            add(tokens, name, end, TokenKind::Keyword);
            mode = TAG;
            return end;
        }
        const std::size_t next = line.find('<', i);
        const std::size_t end = next == std::string_view::npos ? line.size() : next;
        with_bindings(line, i, end, TokenKind::Text, tokens);
        return end;
    }

    // Inside a tag: attributes and their values, up to its end.
    static std::size_t tag(std::string_view line, std::size_t i, uint8_t& mode, std::vector<Token>& tokens) {
        const char c = line[i];
        if (c == ' ' || c == '\t') return i + 1;
        if (c == '>' || line.substr(i, 2) == "/>") {
            const std::size_t end = c == '>' ? i + 1 : i + 2;
            add(tokens, i, end, TokenKind::Punctuation);
            mode = TEXT;
            return end;
        }
        if (c == '"' || c == '\'') {
            bool closed = false;
            const std::size_t end = value(line, i, i + 1, c, tokens, closed);
            if (!closed) mode = c == '"' ? DOUBLE_QUOTED : SINGLE_QUOTED;
            return end;
        }
        if (markup_name_char(c)) {
            const std::size_t end = markup_name_end(line, i);
            add(tokens, i, end, TokenKind::Member);
            return end;
        }
        add(tokens, i, i + 1, TokenKind::Punctuation);
        return i + 1;
    }
};

} // namespace

const Lexer& markup() {
    static const MarkupLexer lexer;
    return lexer;
}

} // namespace fjedit::lex
