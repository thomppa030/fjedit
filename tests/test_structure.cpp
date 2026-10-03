#include "fjedit/core/language.hpp"
#include "fjedit/core/structure.hpp"

#include "print.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fjedit;

namespace {

// A text, and its syntax in the language of `file`.
struct Text {
    Buffer buffer;
    Syntax syntax;

    Text(std::string_view text, std::string_view file = "x.cpp")
        : buffer(text), syntax(language_for(file).lexer, buffer) {}

    [[nodiscard]] std::optional<BracketPair> bracket(Position at) const {
        return matching_bracket(buffer, syntax, at);
    }
};

} // namespace

TEST_CASE("A bracket after the caret finds its pair", "[structure]") {
    const Text text("f(a, g(b))");
    REQUIRE(text.bracket({0, 1}) == BracketPair{{0, 1}, {0, 9}});
    REQUIRE(text.bracket({0, 6}) == BracketPair{{0, 6}, {0, 8}});
}

TEST_CASE("Failing that, the bracket before the caret", "[structure]") {
    const Text text("f(a, g(b))");
    REQUIRE(text.bracket({0, 10}) == BracketPair{{0, 1}, {0, 9}});
    REQUIRE(text.bracket({0, 3}) == std::nullopt);
}

TEST_CASE("Pairs are found across lines, either way", "[structure]") {
    const Text text("void tick() {\n  if (a) {\n    b();\n  }\n}");
    REQUIRE(text.bracket({0, 12}) == BracketPair{{0, 12}, {4, 0}});
    REQUIRE(text.bracket({4, 0}) == BracketPair{{0, 12}, {4, 0}});
    REQUIRE(text.bracket({3, 3}) == BracketPair{{1, 9}, {3, 2}});
}

TEST_CASE("Brackets in strings and comments don't count", "[structure]") {
    const Text text("f(\")\", /* ) */ x) // (");
    REQUIRE(text.bracket({0, 1}) == BracketPair{{0, 1}, {0, 16}});
    REQUIRE(text.bracket({0, 3}) == std::nullopt);
    REQUIRE(text.bracket({0, 21}) == std::nullopt);
}

TEST_CASE("A bracket without its pair pairs with nothing", "[structure]") {
    const Text text("f(a, (b)");
    REQUIRE(text.bracket({0, 1}) == std::nullopt);
    REQUIRE(text.bracket({0, 5}) == BracketPair{{0, 5}, {0, 7}});
}

TEST_CASE("A position knows whether it is in a string or comment", "[structure]") {
    const Text text("a = \"s\"; // c");
    REQUIRE_FALSE(in_string_or_comment(text.syntax, 0, 0));
    REQUIRE(in_string_or_comment(text.syntax, 0, 5));
    REQUIRE_FALSE(in_string_or_comment(text.syntax, 0, 7));
    REQUIRE(in_string_or_comment(text.syntax, 0, 12));
}

TEST_CASE("Brackets fold from the line they open on to the line they close on", "[structure][fold]") {
    const Text text("void tick() {\n  if (a) {\n    b();\n  }\n  c(d,\n    e);\n}");
    REQUIRE(fold_ranges(text.buffer, text.syntax, Folding::Brackets) ==
            std::vector<FoldRange>{{0, 6}, {1, 3}, {4, 5}});
}

TEST_CASE("Brackets that close on their own line, or in strings, fold nothing", "[structure][fold]") {
    const Text text("f(a); s = \"{\";\nx = {1, 2};\n// {\ny();");
    REQUIRE(fold_ranges(text.buffer, text.syntax, Folding::Brackets).empty());
}

TEST_CASE("A line with several openers folds as far as the furthest closes", "[structure][fold]") {
    const Text text("call(a, [&] {\n  b();\n});");
    REQUIRE(fold_ranges(text.buffer, text.syntax, Folding::Brackets) == std::vector<FoldRange>{{0, 2}});
}

TEST_CASE("Runs of comment lines fold, single ones don't", "[structure][fold]") {
    const Text text("// one\n// two\nx();\n// alone\nz();\n/* a\n   b\n*/\ny();");
    REQUIRE(fold_ranges(text.buffer, text.syntax, Folding::Brackets) == std::vector<FoldRange>{{0, 1}, {5, 7}});
}

TEST_CASE("Markup folds by its indentation", "[structure][fold]") {
    const Text text("<widget>\n    <div>\n        <text/>\n    </div>\n\n    <div/>\n</widget>", "x.fjwidget");
    REQUIRE(language_for("x.fjwidget").folding == Folding::Indentation);
    REQUIRE(language_for("x.cpp").folding == Folding::Brackets);
    REQUIRE(fold_ranges(text.buffer, text.syntax, Folding::Indentation) == std::vector<FoldRange>{{0, 5}, {1, 2}});
}

TEST_CASE("Indentation counts tabs to the next stop", "[structure][fold]") {
    const Text text("a\n\tb\n        c\nd", "x.txt");
    REQUIRE(fold_ranges(text.buffer, text.syntax, Folding::Indentation, 4) == std::vector<FoldRange>{{0, 2}, {1, 2}});
    REQUIRE(fold_ranges(text.buffer, text.syntax, Folding::Indentation, 8) == std::vector<FoldRange>{{0, 2}});
}

TEST_CASE("A block may fold from the line the one before it closes on", "[structure][fold]") {
    const Text text("if (a) {\n  b();\n} else {\n  c();\n}");
    REQUIRE(fold_ranges(text.buffer, text.syntax, Folding::Brackets) == std::vector<FoldRange>{{0, 2}, {2, 4}});
}
