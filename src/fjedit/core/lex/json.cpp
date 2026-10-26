// JSON, and fjell's files written in it: keys are members, other strings
// strings, numbers numbers, true, false and null keywords. Comments are
// taken too, as .fjgraph files have them.

#include "fjedit/core/lex/lexers.hpp"
#include "fjedit/core/lex/scan.hpp"

namespace fjedit::lex {

namespace {

enum Mode : uint8_t {
    NORMAL = 0,
    BLOCK_COMMENT = 1,
};

class JsonLexer final : public Lexer {
public:
    [[nodiscard]] LineState lex(std::string_view line, const LineState& state, std::vector<Token>& tokens) const override {
        std::size_t i = 0;
        if (state.mode == BLOCK_COMMENT) {
            const std::size_t end = line.find("*/");
            if (end == std::string_view::npos) {
                add(tokens, 0, line.size(), TokenKind::Comment);
                return state;
            }
            add(tokens, 0, end + 2, TokenKind::Comment);
            i = end + 2;
        }

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
            } else if (c == '"') {
                bool closed = false;
                const std::size_t end = quoted_end(line, i, closed);
                const std::size_t next = skip_space(line, end);
                const bool key = next < line.size() && line[next] == ':';
                add(tokens, i, end, key ? TokenKind::Member : TokenKind::String);
                i = end;
            } else if (digit(c) || (c == '-' && i + 1 < line.size() && digit(line[i + 1]))) {
                const std::size_t end = number_end(line, i);
                add(tokens, i, end, TokenKind::Number);
                i = end;
            } else if (name_start(c)) {
                const std::size_t end = name_end(line, i);
                const std::string_view word = line.substr(i, end - i);
                const bool literal = word == "true" || word == "false" || word == "null";
                add(tokens, i, end, literal ? TokenKind::Keyword : TokenKind::Text);
                i = end;
            } else {
                add(tokens, i, i + 1, TokenKind::Punctuation);
                ++i;
            }
        }
        return {};
    }
};

} // namespace

const Lexer& json() {
    static const JsonLexer lexer;
    return lexer;
}

} // namespace fjedit::lex
