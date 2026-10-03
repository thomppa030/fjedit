#include "fjedit/core/language.hpp"

#include "lex.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fjedit;

static const Lexer& glsl = language_for("light.frag").lexer;
static const Lexer& fjsl = language_for("toon.fjsl").lexer;

TEST_CASE("Shader files are GLSL, and .fjsl is FJSL", "[lex][glsl]") {
    REQUIRE(language_for("ddgi.comp").name == "GLSL");
    REQUIRE(language_for("include/fjell_water_waves.glsl").name == "GLSL");
    REQUIRE(language_for("shaders/toon.fjsl").name == "FJSL");
}

TEST_CASE("GLSL: types, built-in calls and numbers", "[lex][glsl]") {
    REQUIRE(colours(glsl, "vec3 H = normalize(VIEW_DIR + LIGHT_DIR);") ==
            Colours{{"vec3", K::Type}, {"=", K::Punctuation}, {"normalize", K::Function}, {"(", K::Punctuation},
                    {"+", K::Punctuation}, {");", K::Punctuation}});
    REQUIRE(colours(glsl, "if (NdotL > 0.001) diffuse /= NdotL;").front() == std::pair<std::string, K>{"if", K::Keyword});
}

TEST_CASE("GLSL: a type built with a call is still a type", "[lex][glsl]") {
    REQUIRE(colours(glsl, "vec3(0.931, 0.7, 0.5)").front() == std::pair<std::string, K>{"vec3", K::Type});
    REQUIRE(colours(glsl, "mat3(TBN)").front() == std::pair<std::string, K>{"mat3", K::Type});
}

TEST_CASE("GLSL: the preprocessor and comments", "[lex][glsl]") {
    REQUIRE(colours(glsl, "#include \"fjell_lighting.glsl\"") ==
            Colours{{"#include", K::Meta}, {"\"fjell_lighting.glsl\"", K::String}});
    REQUIRE(colours(glsl, "#version 460") == Colours{{"#version", K::Meta}, {"460", K::Number}});
    REQUIRE(colours(glsl, "x; // banded") == Colours{{";", K::Punctuation}, {"// banded", K::Comment}});
}

TEST_CASE("GLSL knows none of FJSL's own words", "[lex][glsl]") {
    REQUIRE(colours(glsl, "pass") == Colours{});
    REQUIRE(colours(fjsl, "pass") == Colours{{"pass", K::Keyword}});
}

TEST_CASE("FJSL: the shader's type is a keyword", "[lex][fjsl]") {
    REQUIRE(colours(fjsl, "shader_type spatial;") ==
            Colours{{"shader_type", K::Keyword}, {"spatial", K::Keyword}, {";", K::Punctuation}});
}

TEST_CASE("FJSL: render modes up to the semicolon are keywords", "[lex][fjsl]") {
    REQUIRE(colours(fjsl, "render_mode unlit, cutout; float x;") ==
            Colours{{"render_mode", K::Keyword}, {"unlit", K::Keyword}, {",", K::Punctuation},
                    {"cutout", K::Keyword}, {";", K::Punctuation}, {"float", K::Keyword}, {";", K::Punctuation}});
}

TEST_CASE("FJSL: hints are meta, with their arguments", "[lex][fjsl]") {
    REQUIRE(colours(fjsl, "uniform float band_count : hint_range(2.0, 8.0, 1.0) = 3.0;") ==
            Colours{{"uniform", K::Keyword}, {"float", K::Keyword}, {":", K::Punctuation},
                    {"hint_range", K::Meta}, {"(", K::Punctuation}, {"2.0", K::Number}, {",", K::Punctuation},
                    {"8.0", K::Number}, {",", K::Punctuation}, {"1.0", K::Number}, {")", K::Punctuation},
                    {"=", K::Punctuation}, {"3.0", K::Number}, {";", K::Punctuation}});
    REQUIRE(colours(fjsl, "uniform vec3 tint : hint_color, hint_albedo;") ==
            Colours{{"uniform", K::Keyword}, {"vec3", K::Type}, {":", K::Punctuation}, {"hint_color", K::Meta},
                    {",", K::Punctuation}, {"hint_albedo", K::Meta}, {";", K::Punctuation}});
    // "mode" is plain text and left out: the hint is the third token.
    REQUIRE(colours(fjsl, "int mode : hint_enum(\"A,B\");")[2] == std::pair<std::string, K>{"hint_enum", K::Meta});
}

TEST_CASE("FJSL: instance uniforms and hooks", "[lex][fjsl]") {
    REQUIRE(colours(fjsl, "instance uniform float autumn;") ==
            Colours{{"instance", K::Keyword}, {"uniform", K::Keyword}, {"float", K::Keyword}, {";", K::Punctuation}});
    REQUIRE(colours(fjsl, "void light() {") ==
            Colours{{"void", K::Keyword}, {"light", K::Function}, {"()", K::Punctuation}, {"{", K::Punctuation}});
}
