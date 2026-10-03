#include "fjedit/core/language.hpp"

#include "lex.hpp"
#include "print.hpp"

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <string>
#include <vector>

using namespace fjedit;

TEST_CASE("A file's language is found by its extension, case aside", "[syntax]") {
    REQUIRE(language_for("scripts/coincollectible.cpp").name == "C++");
    REQUIRE(language_for("C:\\game\\Player.HPP").name == "C++");
    REQUIRE(language_for("notes.txt").name == "Plain text");
    REQUIRE(language_for("Makefile").name == "Plain text");
    REQUIRE(language_for("dir.v2/README").name == "Plain text");
}

TEST_CASE("A syntax holds each line's tokens and end state", "[syntax]") {
    const Buffer buffer("int a; /* one\ntwo */ b();");
    const Syntax syntax(language_for("x.cpp").lexer, buffer);
    REQUIRE(syntax.line_count() == 2);
    REQUIRE(syntax.end_state(0).mode != 0);
    REQUIRE(syntax.end_state(1).mode == 0);
    REQUIRE(syntax.tokens(1).front() == Token{.begin = 0, .end = 6, .kind = TokenKind::Comment});
}

TEST_CASE("Opening a comment colours the lines after it, closing it gives them back", "[syntax]") {
    Buffer buffer("a();\nb();\nc();");
    Syntax syntax(language_for("x.cpp").lexer, buffer);
    REQUIRE(syntax.tokens(2).front().kind == TokenKind::Function);

    Edit open{{{.span = {{0, 0}, {0, 0}}, .text = "/* "}}};
    (void)buffer.apply(open);
    syntax.edited(buffer, open);
    REQUIRE(syntax.tokens(2).front().kind == TokenKind::Comment);

    Edit close{{{.span = {{1, 4}, {1, 4}}, .text = " */"}}};
    (void)buffer.apply(close);
    syntax.edited(buffer, close);
    REQUIRE(syntax.tokens(1).back().kind == TokenKind::Comment);
    REQUIRE(syntax.tokens(2).front().kind == TokenKind::Function);
}

// Edits that open and close comments and strings, add and remove lines:
// the tokens kept up to date edit by edit must be those of lexing anew.
TEST_CASE("Tokens kept through random edits are those of lexing anew", "[syntax]") {
    std::mt19937 random(777);
    auto below = [&](std::size_t n) { return static_cast<std::size_t>(random() % n); };
    const std::vector<std::string> pieces = {"/*", "*/", "\"", "x", " ", "\n", "R\"(", ")\"", "//", "f(", "1.5f",
                                             "#include <a>", "\\"};
    const Lexer& lexer = language_for("x.cpp").lexer;

    for (int round = 0; round < 60; ++round) {
        Buffer buffer("int a;\n/* b */\nc();");
        Syntax syntax(lexer, buffer);
        for (int step = 0; step < 30; ++step) {
            const std::size_t line = below(buffer.line_count());
            Position from = buffer.clamp({line, below(buffer.line(line).size() + 1)});
            Position to = from;
            if (below(3) == 0) to = buffer.clamp({from.line + below(2), below(6)});
            if (to < from) std::swap(from, to);
            Edit edit{{{.span = {from, to}, .text = pieces[below(pieces.size())]}}};
            (void)buffer.apply(edit);
            syntax.edited(buffer, edit);

            const Syntax fresh(lexer, buffer);
            REQUIRE(syntax.line_count() == fresh.line_count());
            for (std::size_t i = 0; i < fresh.line_count(); ++i) {
                REQUIRE(syntax.end_state(i) == fresh.end_state(i));
                REQUIRE(std::vector<Token>(syntax.tokens(i).begin(), syntax.tokens(i).end()) ==
                        std::vector<Token>(fresh.tokens(i).begin(), fresh.tokens(i).end()));
            }
        }
    }
}
