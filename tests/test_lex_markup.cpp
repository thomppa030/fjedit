#include "fjedit/core/language.hpp"

#include "lex.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fjedit;

static const Lexer& markup = language_for("hud.fjwidget").lexer;

// The tokens of `lines` lexed in order, each beginning where the last ended.
static Colours widget(std::initializer_list<std::string_view> lines, LineState* end = nullptr) {
    Colours out;
    LineState state;
    for (const std::string_view line : lines) {
        const Colours more = colours(markup, line, state, &state);
        out.insert(out.end(), more.begin(), more.end());
    }
    if (end != nullptr) *end = state;
    return out;
}

TEST_CASE("Widgets and UI files are markup", "[lex][markup]") {
    REQUIRE(language_for("assets/ui/hud.fjwidget").name == "Widget");
    REQUIRE(language_for("assets/ui/test.fjui").name == "Widget");
}

TEST_CASE("Markup: tags are keywords, attributes members, values strings", "[lex][markup]") {
    REQUIRE(widget({"<panel id=\"hud\" class=\"overlay\">"}) ==
            Colours{{"<", K::Punctuation}, {"panel", K::Keyword}, {"id", K::Member}, {"=", K::Punctuation},
                    {"\"hud\"", K::String}, {"class", K::Member}, {"=", K::Punctuation}, {"\"overlay\"", K::String},
                    {">", K::Punctuation}});
}

TEST_CASE("Markup: an element that closes itself, and a closing tag", "[lex][markup]") {
    REQUIRE(widget({"<prop name=\"fps\"/></widget>"}) ==
            Colours{{"<", K::Punctuation}, {"prop", K::Keyword}, {"name", K::Member}, {"=", K::Punctuation},
                    {"\"fps\"", K::String}, {"/></", K::Punctuation}, {"widget", K::Keyword},
                    {">", K::Punctuation}});
}

TEST_CASE("Markup: directives and their expressions", "[lex][markup]") {
    REQUIRE(widget({"<div on:click=\"select_slot_1\" class:active=\"active_slot==1\">"}) ==
            Colours{{"<", K::Punctuation}, {"div", K::Keyword}, {"on:click", K::Member}, {"=", K::Punctuation},
                    {"\"select_slot_1\"", K::String}, {"class:active", K::Member}, {"=", K::Punctuation},
                    {"\"active_slot==1\"", K::String}, {">", K::Punctuation}});
}

TEST_CASE("Markup: bindings are meta, in values and in text", "[lex][markup]") {
    REQUIRE(widget({"<bar value=\"{health}\"/>"}) ==
            Colours{{"<", K::Punctuation}, {"bar", K::Keyword}, {"value", K::Member}, {"=", K::Punctuation},
                    {"\"", K::String}, {"{health}", K::Meta}, {"\"", K::String}, {"/>", K::Punctuation}});
    REQUIRE(widget({"<text>HP: {health} / {max_health}</text>"}) ==
            Colours{{"<", K::Punctuation}, {"text", K::Keyword}, {">", K::Punctuation}, {"{health}", K::Meta},
                    {"{max_health}", K::Meta}, {"</", K::Punctuation}, {"text", K::Keyword},
                    {">", K::Punctuation}});
}

TEST_CASE("Markup: a tag runs across lines", "[lex][markup]") {
    LineState end;
    REQUIRE(widget({"<div class=\"slot\"", "     if=\"item.count\">"}, &end) ==
            Colours{{"<", K::Punctuation}, {"div", K::Keyword}, {"class", K::Member}, {"=", K::Punctuation},
                    {"\"slot\"", K::String}, {"if", K::Member}, {"=", K::Punctuation}, {"\"item.count\"", K::String},
                    {">", K::Punctuation}});
    REQUIRE(end.mode == 0);
}

TEST_CASE("Markup: comments, on a line and across lines", "[lex][markup]") {
    REQUIRE(widget({"<!-- the hotbar --><div>"}) ==
            Colours{{"<!-- the hotbar -->", K::Comment}, {"<", K::Punctuation}, {"div", K::Keyword},
                    {">", K::Punctuation}});
    LineState end;
    REQUIRE(widget({"<!-- one", "two --> {x}"}, &end) ==
            Colours{{"<!-- one", K::Comment}, {"two -->", K::Comment}, {"{x}", K::Meta}});
    REQUIRE(end.mode == 0);
}
