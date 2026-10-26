#include "fjedit/core/language.hpp"

#include "lex.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fjedit;

static const Lexer& json = language_for("x.json").lexer;

TEST_CASE("JSON and fjell's files written in it", "[lex][json]") {
    for (const char* file : {"package.json", "toon.fjmat", "Inter.fjfont", "sand.fjsurface", "Player.fjp",
                             "rolling_swell.fjwater", "neutral.fjlut", "AnimSet_01.fjanimset", "gameplay.fjinput",
                             "default_day.fjday", "clear.fjweather", "breath.fjvfx", "editor_preview.fjgraph",
                             "project.fjell", "scenes/harbour.fjell"}) {
        REQUIRE(language_for(file).name == "JSON");
    }
}

TEST_CASE("JSON: keys are members, values coloured by what they are", "[lex][json]") {
    REQUIRE(colours(json, R"("shader": "shaders/tint.fjsl", "strength": 0.3,)") ==
            Colours{{"\"shader\"", K::Member}, {":", K::Punctuation}, {"\"shaders/tint.fjsl\"", K::String},
                    {",", K::Punctuation}, {"\"strength\"", K::Member}, {":", K::Punctuation}, {"0.3", K::Number},
                    {",", K::Punctuation}});
}

TEST_CASE("JSON: literals, negative numbers and exponents", "[lex][json]") {
    REQUIRE(colours(json, R"([true, false, null, -1.5e-3])") ==
            Colours{{"[", K::Punctuation}, {"true", K::Keyword}, {",", K::Punctuation}, {"false", K::Keyword},
                    {",", K::Punctuation}, {"null", K::Keyword}, {",", K::Punctuation}, {"-1.5e-3", K::Number},
                    {"]", K::Punctuation}});
}

TEST_CASE("JSON: a key with a space before its colon, and an escaped quote", "[lex][json]") {
    // Outside the REQUIRE: MSVC's preprocessor can't stringize a raw string holding \".
    const std::string line = R"("a\"b" : "c")";
    const std::string key = R"("a\"b")";
    REQUIRE(colours(json, line) == Colours{{key, K::Member}, {":", K::Punctuation}, {"\"c\"", K::String}});
}

TEST_CASE("JSON: comments, as .fjgraph has them", "[lex][json]") {
    REQUIRE(colours(json, "// Lightweight pipeline for editor previews") ==
            Colours{{"// Lightweight pipeline for editor previews", K::Comment}});
    LineState state;
    REQUIRE(colours(json, "{ /* one", {}, &state) == Colours{{"{", K::Punctuation}, {"/* one", K::Comment}});
    REQUIRE(colours(json, "two */ }", state, &state) == Colours{{"two */", K::Comment}, {"}", K::Punctuation}});
    REQUIRE(state.mode == 0);
}
