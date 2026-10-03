#include "fjedit/core/language.hpp"

#include "lex.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fjedit;

static const Lexer& cpp = language_for("x.cpp").lexer;

TEST_CASE("C++: keywords, calls, types and punctuation", "[lex][cpp]") {
    REQUIRE(colours(cpp, "void CoinCollectible::tick(float dt) {") ==
            Colours{{"void", K::Keyword}, {"CoinCollectible", K::Type}, {"::", K::Punctuation}, {"tick", K::Function},
                    {"(", K::Punctuation}, {"float", K::Keyword}, {")", K::Punctuation}, {"{", K::Punctuation}});
}

TEST_CASE("C++: a name before :: is a type, as is one written LikeThis", "[lex][cpp]") {
    REQUIRE(colours(cpp, "glm::vec3 axis") == Colours{{"glm", K::Type}, {"::", K::Punctuation}});
    REQUIRE(colours(cpp, "Ref<Node3D> other") ==
            Colours{{"Ref", K::Type}, {"<", K::Punctuation}, {"Node3D", K::Type}, {">", K::Punctuation}});
    REQUIRE(colours(cpp, "MAX_SPEED") == Colours{});
}

TEST_CASE("C++: numbers in their forms", "[lex][cpp]") {
    for (const char* number : {"90.0f", "0x1F", "1'000'000", "1e-3", ".5", "2.0e+10L", "0b1010u"}) {
        REQUIRE(colours(cpp, number) == Colours{{number, K::Number}});
    }
}

TEST_CASE("C++: strings, characters and their prefixes", "[lex][cpp]") {
    REQUIRE(colours(cpp, R"(s = "a \" b";)") ==
            Colours{{"=", K::Punctuation}, {R"("a \" b")", K::String}, {";", K::Punctuation}});
    REQUIRE(colours(cpp, "u8\"x\" L'y'") == Colours{{"u8\"x\"", K::String}, {"L'y'", K::String}});
}

TEST_CASE("C++: a raw string runs across lines to its own end", "[lex][cpp]") {
    LineState state;
    REQUIRE(colours(cpp, R"(auto s = R"js(one)", {}, &state) ==
            Colours{{"auto", K::Keyword}, {"=", K::Punctuation}, {R"(R"js(one)", K::String}});
    REQUIRE(state.mode != 0);
    REQUIRE(colours(cpp, R"x(two)" still)x", state, &state) == Colours{{R"x(two)" still)x", K::String}});
    REQUIRE(colours(cpp, R"(three)js"; x)", state, &state) ==
            Colours{{R"(three)js")", K::String}, {";", K::Punctuation}});
    REQUIRE(state.mode == 0);
}

TEST_CASE("C++: comments, to the line's end and across lines", "[lex][cpp]") {
    REQUIRE(colours(cpp, "a; // why") == Colours{{";", K::Punctuation}, {"// why", K::Comment}});
    LineState state;
    REQUIRE(colours(cpp, "x /* one", {}, &state) == Colours{{"/* one", K::Comment}});
    REQUIRE(colours(cpp, "two */ y();", state, &state) ==
            Colours{{"two */", K::Comment}, {"y", K::Function}, {"();", K::Punctuation}});
    REQUIRE(state.mode == 0);
}

TEST_CASE("C++: the preprocessor and an include's file", "[lex][cpp]") {
    REQUIRE(colours(cpp, "#include <glm/gtc/quaternion.hpp>") ==
            Colours{{"#include", K::Meta}, {"<glm/gtc/quaternion.hpp>", K::String}});
    REQUIRE(colours(cpp, "  #pragma once") == Colours{{"#pragma", K::Meta}});
    REQUIRE(colours(cpp, "#define TWICE(x) ((x) * 2)").front() == std::pair<std::string, K>{"#define", K::Meta});
}

TEST_CASE("C++: attributes are meta", "[lex][cpp]") {
    REQUIRE(colours(cpp, "[[fj::property]] float spin_speed = 90.0f;") ==
            Colours{{"[[fj::property]]", K::Meta}, {"float", K::Keyword}, {"=", K::Punctuation},
                    {"90.0f", K::Number}, {";", K::Punctuation}});
}

TEST_CASE("C++: names in any script read whole", "[lex][cpp]") {
    REQUIRE(colours(cpp, "gr\xC3\xB6\xC3\x9F" "e()") ==
            Colours{{"gr\xC3\xB6\xC3\x9F" "e", K::Function}, {"()", K::Punctuation}});
}
