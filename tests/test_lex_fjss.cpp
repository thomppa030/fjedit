#include "fjedit/core/language.hpp"

#include "lex.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fjedit;

static const Lexer& fjss = language_for("hud.fjss").lexer;

// The tokens of `lines` lexed in order, each beginning where the last ended.
static Colours sheet(std::initializer_list<std::string_view> lines) {
    Colours out;
    LineState state;
    for (const std::string_view line : lines) {
        const Colours more = colours(fjss, line, state, &state);
        out.insert(out.end(), more.begin(), more.end());
    }
    return out;
}

TEST_CASE("Stylesheets are FJSS", "[lex][fjss]") {
    REQUIRE(language_for("assets/ui/theme.fjss").name == "FJSS");
}

TEST_CASE("FJSS: selectors are tags, classes, ids and states", "[lex][fjss]") {
    REQUIRE(sheet({"panel.slot:hover > #hud, text {"}) ==
            Colours{{"panel", K::Keyword}, {".slot", K::Type}, {":hover", K::Meta}, {">", K::Punctuation},
                    {"#hud", K::Type}, {",", K::Punctuation}, {"text", K::Keyword}, {"{", K::Punctuation}});
}

TEST_CASE("FJSS: properties are members, their values coloured by what they are", "[lex][fjss]") {
    REQUIRE(sheet({".slot {", "    border: 2 solid #555;", "    width: 56; gap: 1vw;", "}"}) ==
            Colours{{".slot", K::Type}, {"{", K::Punctuation},
                    {"border", K::Member}, {":", K::Punctuation}, {"2", K::Number}, {"solid", K::Keyword},
                    {"#555", K::Number}, {";", K::Punctuation},
                    {"width", K::Member}, {":", K::Punctuation}, {"56", K::Number}, {";", K::Punctuation},
                    {"gap", K::Member}, {":", K::Punctuation}, {"1vw", K::Number}, {";", K::Punctuation},
                    {"}", K::Punctuation}});
}

TEST_CASE("FJSS: functions, custom properties and percentages in values", "[lex][fjss]") {
    REQUIRE(sheet({"a { background-color: rgba(34, 34, 34, 0.8); color: var(--accent); left: -50%; }"}) ==
            Colours{{"a", K::Keyword}, {"{", K::Punctuation},
                    {"background-color", K::Member}, {":", K::Punctuation}, {"rgba", K::Function},
                    {"(", K::Punctuation}, {"34", K::Number}, {",", K::Punctuation}, {"34", K::Number},
                    {",", K::Punctuation}, {"34", K::Number}, {",", K::Punctuation}, {"0.8", K::Number},
                    {");", K::Punctuation},
                    {"color", K::Member}, {":", K::Punctuation}, {"var", K::Function}, {"(", K::Punctuation},
                    {"--accent", K::Member}, {");", K::Punctuation},
                    {"left", K::Member}, {":", K::Punctuation}, {"-50%", K::Number}, {";", K::Punctuation},
                    {"}", K::Punctuation}});
}

TEST_CASE("FJSS: custom properties are declared as members", "[lex][fjss]") {
    REQUIRE(sheet({":root { --accent: #d4a054; }"}) ==
            Colours{{":root", K::Meta}, {"{", K::Punctuation}, {"--accent", K::Member}, {":", K::Punctuation},
                    {"#d4a054", K::Number}, {";", K::Punctuation}, {"}", K::Punctuation}});
}

TEST_CASE("FJSS: at-rules are meta, and @media's block holds selectors", "[lex][fjss]") {
    REQUIRE(sheet({"@import \"theme.fjss\";"}) ==
            Colours{{"@import", K::Meta}, {"\"theme.fjss\"", K::String}, {";", K::Punctuation}});
    REQUIRE(sheet({"@media (max-aspect-ratio: 4/3) {", "    .bottom-panel { flex-direction: column; }", "}"}) ==
            Colours{{"@media", K::Meta}, {"(", K::Punctuation}, {"max-aspect-ratio", K::Member},
                    {":", K::Punctuation}, {"4", K::Number}, {"/", K::Punctuation}, {"3", K::Number},
                    {")", K::Punctuation}, {"{", K::Punctuation},
                    {".bottom-panel", K::Type}, {"{", K::Punctuation}, {"flex-direction", K::Member},
                    {":", K::Punctuation}, {"column", K::Keyword}, {";", K::Punctuation}, {"}", K::Punctuation},
                    {"}", K::Punctuation}});
}

TEST_CASE("FJSS: a comment across lines ends where it was", "[lex][fjss]") {
    REQUIRE(sheet({".a { width: 1; /* the", "   gap */ height: 2; }"}) ==
            Colours{{".a", K::Type}, {"{", K::Punctuation}, {"width", K::Member}, {":", K::Punctuation},
                    {"1", K::Number}, {";", K::Punctuation}, {"/* the", K::Comment}, {"   gap */", K::Comment},
                    {"height", K::Member}, {":", K::Punctuation}, {"2", K::Number}, {";", K::Punctuation},
                    {"}", K::Punctuation}});
}

TEST_CASE("FJSS: a transition's timing", "[lex][fjss]") {
    REQUIRE(sheet({"b { transition: all 0.15s ease-in-out; }"}) ==
            Colours{{"b", K::Keyword}, {"{", K::Punctuation}, {"transition", K::Member}, {":", K::Punctuation},
                    {"all", K::Keyword}, {"0.15s", K::Number}, {"ease-in-out", K::Keyword}, {";", K::Punctuation},
                    {"}", K::Punctuation}});
}
