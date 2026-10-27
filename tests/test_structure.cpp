#include "fjedit/core/document.hpp"
#include "fjedit/core/language.hpp"
#include "fjedit/core/structure.hpp"

#include "print.hpp"

#include <catch2/catch_test_macros.hpp>

#include <span>
#include <string>
#include <vector>

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

    /// The text with each bracket's level after it, `!` for one without a
    /// pair: "f(0x)0". `named` is a language service's tokens.
    [[nodiscard]] std::string levelled(std::span<const std::vector<Token>> named = {}) const {
        const BracketLevels levels = bracket_levels(buffer, syntax, named);
        std::string out;
        for (std::size_t line = 0; line < buffer.line_count(); ++line) {
            if (line > 0) out += '\n';
            const std::string_view text = buffer.line(line);
            const std::span<const LeveledBracket> brackets = levels.on_line(line);
            std::size_t next = 0;
            for (std::size_t byte = 0; byte < text.size(); ++byte) {
                out += text[byte];
                if (next < brackets.size() && brackets[next].at.byte == byte) {
                    out += brackets[next].level ? std::to_string(*brackets[next].level) : "!";
                    ++next;
                }
            }
        }
        return out;
    }
};

} // namespace

TEST_CASE("Brackets of every kind count in one depth", "[structure][levels]") {
    REQUIRE(Text("f(a[b]) {}").levelled() == "f(0a[1b]1)0 {0}0");
}

TEST_CASE("Levels run on across lines", "[structure][levels]") {
    REQUIRE(Text("a {\n  b(c[\n  ]);\n}").levelled() == "a {0\n  b(1c[2\n  ]2)1;\n}0");
}

TEST_CASE("Brackets in strings and comments are not counted", "[structure][levels]") {
    REQUIRE(Text("s = \"(\" + f(x); // )").levelled() == "s = \"(\" + f(0x)0; // )");
}

TEST_CASE("A bracket without a pair has no level and leaves the others theirs", "[structure][levels]") {
    REQUIRE(Text("if (a && (b) {\n}").levelled() == "if (!a && (0b)0 {0\n}0");
    REQUIRE(Text("f(x))").levelled() == "f(0x)0)!");
}

TEST_CASE("A closer pairs with the nearest opener of its kind, those opened after it with none",
          "[structure][levels]") {
    REQUIRE(Text("{ ( }").levelled() == "{0 (! }0");
}

TEST_CASE("Angle brackets count where a language service names them", "[structure][levels]") {
    const auto bracket = [](std::size_t byte) { return Token{.begin = byte, .end = byte + 1, .kind = TokenKind::Bracket}; };
    const Text text("std::vector<std::pair<int, float>> v; bool b = 1 < 2;");
    const std::vector<std::vector<Token>> named{{bracket(11), bracket(21), bracket(32), bracket(33)}};
    REQUIRE(text.levelled(named) == "std::vector<0std::pair<1int, float>1>0 v; bool b = 1 < 2;");
    REQUIRE(text.levelled() == "std::vector<std::pair<int, float>> v; bool b = 1 < 2;");
}

TEST_CASE("A line past the text has no brackets", "[structure][levels]") {
    const Text text("f()");
    REQUIRE(bracket_levels(text.buffer, text.syntax).on_line(3).empty());
}

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

static Indent detected(std::string_view text, Indent fallback = {.tabs = false, .width = 4}) {
    return detect_indent(Buffer(text), fallback);
}

TEST_CASE("A file indented with four spaces", "[structure][indent]") {
    const Indent indent = detected("a {\n    b {\n        c;\n    }\n}");
    REQUIRE_FALSE(indent.tabs);
    REQUIRE(indent.width == 4);
}

TEST_CASE("A file indented with two spaces", "[structure][indent]") {
    const Indent indent = detected("a {\n  b;\n  c {\n    d;\n  }\n}");
    REQUIRE_FALSE(indent.tabs);
    REQUIRE(indent.width == 2);
}

TEST_CASE("A file indented with tabs keeps the width it is given", "[structure][indent]") {
    const Indent indent = detected("a {\n\tb;\n\tc {\n\t\td;\n\t}\n}", {.tabs = false, .width = 8});
    REQUIRE(indent.tabs);
    REQUIRE(indent.width == 8);
}

TEST_CASE("A file without indented lines says nothing", "[structure][indent]") {
    const Indent indent = detected("one\ntwo\n\nthree", {.tabs = true, .width = 3});
    REQUIRE(indent.tabs);
    REQUIRE(indent.width == 3);
}

TEST_CASE("A comment's stars don't count as a step", "[structure][indent]") {
    const Indent indent = detected("/**\n * one\n * two\n */\nf() {\n  x;\n  y;\n}");
    REQUIRE(indent.width == 2);
}

TEST_CASE("A few tabs in a file of spaces don't make it a tab file", "[structure][indent]") {
    const Indent indent = detected("a {\n    b;\n    c;\n\td;\n    e;\n}");
    REQUIRE_FALSE(indent.tabs);
    REQUIRE(indent.width == 4);
}

TEST_CASE("The function a line is in is named from its block's head", "[structure]") {
    const Document document("#include <x>\n"                                  // 0
                            "namespace game {\n"                               // 1
                            "struct Coin {\n"                                  // 2
                            "    [[nodiscard]] int value() const {\n"          // 3
                            "        if (spun(1)) {\n"                         // 4
                            "            return 2;\n"                          // 5
                            "        } else {\n"                               // 6
                            "            auto f = [&](int a) { return a; };\n" // 7
                            "        }\n"                                      // 8
                            "    }\n"                                          // 9
                            "};\n"                                             // 10
                            "Coin::Coin() : a_(1), b_(2) {\n"                  // 11
                            "    run(std::visit([](auto v) {\n"                // 12
                            "        return v;\n"                              // 13
                            "    }, x));\n"                                    // 14
                            "}\n"                                              // 15
                            "}",                                               // 16
                            language_for("x.cpp"));
    const auto in = [&](std::size_t line) { return enclosing_function(document.buffer(), document.syntax(), line); };
    REQUIRE(in(5) == "value()");
    REQUIRE(in(6) == "value()");
    REQUIRE(in(7) == "value()");
    REQUIRE(in(9) == "value()");
    REQUIRE(in(3) == "value()");
    REQUIRE(in(13) == "Coin()");
    REQUIRE(in(15) == "Coin()");
    REQUIRE_FALSE(in(2).has_value());
    REQUIRE_FALSE(in(10).has_value());
    REQUIRE_FALSE(in(0).has_value());
}

TEST_CASE("A shader's hooks name the lines in them", "[structure]") {
    const Document document("shader_type spatial;\nuniform float band = 0.5;\nvoid fragment() {\n    ALBEDO = vec3(band);\n}",
                            language_for("x.fjsl"));
    REQUIRE(enclosing_function(document.buffer(), document.syntax(), 3) == "fragment()");
    REQUIRE_FALSE(enclosing_function(document.buffer(), document.syntax(), 1).has_value());
}
