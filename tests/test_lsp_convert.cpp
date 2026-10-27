#include "fjedit/lsp/convert.hpp"

#include "print.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

using namespace fjedit;
using namespace fjedit::lsp;
namespace fs = std::filesystem;

namespace {

// Positions in UTF-8, which need no text.
const Positions UTF8;

Json parsed(std::string_view text) {
    return Json::parse(text);
}

} // namespace

TEST_CASE("Positions go to and from the protocol counted as the server counts", "[lsp][convert]") {
    // An e with an accent is two bytes and one unit; a coin, four bytes and
    // two units.
    const std::string line = "a\xC3\xA9\xF0\x9F\xAA\x99z";
    const Positions utf16(ftk::jsonrpc::Encoding::Utf16, [&](std::size_t) { return std::string_view(line); });
    REQUIRE(utf16.to_json(Position{0, 7}) == parsed(R"({"line":0,"character":4})"));
    REQUIRE(utf16.position(parsed(R"({"line":0,"character":4})")) == Position{0, 7});
}

TEST_CASE("An edit's changes go to the server last first, each in the text the one before left", "[lsp][convert]") {
    const Edit edit{.changes = {{.span = {{0, 1}, {0, 2}}, .text = "X"}, {.span = {{2, 0}, {2, 0}}, .text = "Y\n"}}};
    REQUIRE(content_changes(edit) == parsed(R"([
        {"range":{"start":{"line":2,"character":0},"end":{"line":2,"character":0}},"text":"Y\n"},
        {"range":{"start":{"line":0,"character":1},"end":{"line":0,"character":2}},"text":"X"}])"));
}

TEST_CASE("Diagnostics are read with their severity, source and code", "[lsp][convert]") {
    const std::vector<Diagnostic> read = diagnostics_from(parsed(R"([
        {"range":{"start":{"line":3,"character":4},"end":{"line":3,"character":9}},"severity":2,
         "message":"unused variable 'x'","source":"clang","code":"-Wunused-variable"},
        {"range":{"start":{"line":1,"character":0},"end":{"line":1,"character":1}},"message":"no severity","code":42}])"),
                                                          UTF8);
    REQUIRE(read.size() == 2);
    REQUIRE(read[0] == Diagnostic{.span = {{3, 4}, {3, 9}},
                                  .severity = Severity::Warning,
                                  .message = "unused variable 'x'",
                                  .source = "clang",
                                  .code = "-Wunused-variable"});
    REQUIRE(read[1].severity == Severity::Error);
    REQUIRE(read[1].code == "42");
}

TEST_CASE("Completions are read from a list, with their edits, or replace the word typed", "[lsp][convert]") {
    bool incomplete = false;
    const std::vector<Completion> read = completions_from(parsed(R"json({"isIncomplete":true,"items":[
        {"label":"rotate","kind":3,"detail":"qua<T, Q>","labelDetails":{"detail":"(qua<T, Q> const &q)"},
         "documentation":{"kind":"markdown","value":"Rotates a quaternion."},
         "textEdit":{"range":{"start":{"line":4,"character":8},"end":{"line":4,"character":10}},"newText":"rotate"},
         "additionalTextEdits":[{"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":0}},
                                 "newText":"#include <glm/ext/quaternion_transform.hpp>\n"}],
         "filterText":"rotate","sortText":"0001"},
        {"label":"roll","kind":2}]})json"),
                                                          Position{4, 10}, "  glm::ro", language_for("x.cpp"), UTF8,
                                                          &incomplete);
    REQUIRE(incomplete);
    REQUIRE(read.size() == 2);
    REQUIRE(read[0].label == "rotate");
    REQUIRE(read[0].kind == CompletionKind::Function);
    REQUIRE(read[0].parameters == "(qua<T, Q> const &q)");
    REQUIRE(read[0].detail == "qua<T, Q>");
    REQUIRE(read[0].documentation == "Rotates a quaternion.");
    REQUIRE(read[0].change == Change{.span = {{4, 8}, {4, 10}}, .text = "rotate"});
    REQUIRE(read[0].more.size() == 1);
    REQUIRE(read[0].sort == "0001");
    // No edit of its own: the word before the caret, "ro", goes.
    REQUIRE(read[1].change == Change{.span = {{4, 7}, {4, 10}}, .text = "roll"});
    REQUIRE(read[1].kind == CompletionKind::Method);
}

TEST_CASE("A hover is read from Markdown or marked strings, and an empty one is nothing", "[lsp][convert]") {
    const std::optional<Hover> markup = hover_from(parsed(R"({"contents":{"kind":"markdown","value":"### function `spin`"},
        "range":{"start":{"line":2,"character":1},"end":{"line":2,"character":5}}})"),
                                                   UTF8);
    REQUIRE(markup == Hover{.markdown = "### function `spin`", .span = Span{{2, 1}, {2, 5}}});
    const std::optional<Hover> marked =
        hover_from(parsed(R"({"contents":["A coin.",{"language":"cpp","value":"int spin;"}]})"), UTF8);
    REQUIRE(marked.has_value());
    REQUIRE(marked->markdown == "A coin.\n\n```cpp\nint spin;\n```");
    REQUIRE_FALSE(hover_from(parsed(R"({"contents":""})"), UTF8).has_value());
    REQUIRE_FALSE(hover_from(Json(), UTF8).has_value());
}

TEST_CASE("Definitions are read from a location, a list, or links", "[lsp][convert]") {
    const fs::path file = fs::path("/src/coin.hpp").make_preferred();
    const std::string uri = ftk::jsonrpc::uri_of(file);
    const PositionsFor positions = [](const fs::path&) { return Positions(); };
    const std::string range = R"({"start":{"line":5,"character":2},"end":{"line":5,"character":6}})";
    REQUIRE(locations_from(parsed(R"({"uri":")" + uri + R"(","range":)" + range + "}"), positions) ==
            std::vector<Location>{{file, {{5, 2}, {5, 6}}}});
    REQUIRE(locations_from(parsed(R"([{"targetUri":")" + uri +
                                  R"(","targetRange":{"start":{"line":4,"character":0},"end":{"line":9,"character":1}},
                                     "targetSelectionRange":)" + range + "}]"),
                           positions) == std::vector<Location>{{file, {{5, 2}, {5, 6}}}});
    REQUIRE(locations_from(Json(), positions).empty());
}

TEST_CASE("Where a symbol stands is read from its highlights, in order", "[lsp][convert]") {
    const Positions positions;
    REQUIRE(occurrences_from(parsed(R"([
        {"range":{"start":{"line":3,"character":4},"end":{"line":3,"character":9}},"kind":2},
        {"range":{"start":{"line":0,"character":4},"end":{"line":0,"character":9}},"kind":3}])"),
                             positions) == std::vector<Span>{{{0, 4}, {0, 9}}, {{3, 4}, {3, 9}}});
    REQUIRE(occurrences_from(Json(), positions).empty());
}

TEST_CASE("Text edits become one edit in order, and a workspace edit one per file", "[lsp][convert]") {
    const Edit edit = edit_from(parsed(R"([
        {"range":{"start":{"line":3,"character":0},"end":{"line":3,"character":1}},"newText":"b"},
        {"range":{"start":{"line":1,"character":0},"end":{"line":1,"character":1}},"newText":"a"}])"),
                                UTF8);
    REQUIRE(edit.changes.size() == 2);
    REQUIRE(edit.changes[0].text == "a");

    const fs::path a = fs::path("/src/a.cpp").make_preferred();
    const fs::path b = fs::path("/src/b.cpp").make_preferred();
    const PositionsFor positions = [](const fs::path&) { return Positions(); };
    const std::string one = R"([{"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":3}},"newText":"won"}])";
    const WorkspaceEdit by_changes =
        workspace_edit_from(parsed(R"({"changes":{")" + ftk::jsonrpc::uri_of(a) + R"(":)" + one + "}}"), positions);
    REQUIRE(by_changes.size() == 1);
    REQUIRE_FALSE(by_changes.at(a).version.has_value());
    const WorkspaceEdit by_documents = workspace_edit_from(
        parsed(R"({"documentChanges":[{"textDocument":{"uri":")" + ftk::jsonrpc::uri_of(b) +
               R"(","version":7},"edits":)" + one + R"(},{"kind":"create","uri":"file:///src/new.cpp"}]})"),
        positions);
    REQUIRE(by_documents.size() == 1);
    REQUIRE(by_documents.at(b).version == 7);
    REQUIRE(by_documents.at(b).edit.changes.front().text == "won");
}

TEST_CASE("Semantic tokens are read from their numbers by the server's legend", "[lsp][convert]") {
    const std::vector<TokenKind> kinds = token_kinds(parsed(R"(["variable","function","class","property","macro"])"));
    REQUIRE(kinds == std::vector<TokenKind>{TokenKind::Text, TokenKind::Function, TokenKind::Type, TokenKind::Member,
                                            TokenKind::Meta});
    // A class at 0:6, a function four on, a variable on line 2, and one past
    // the text, which is dropped.
    const SemanticTokens tokens = tokens_from(parsed("[0,6,4,2,0, 0,10,4,1,0, 2,4,3,0,0, 9,0,1,1,0]"), kinds, 3, UTF8);
    REQUIRE(tokens.lines.size() == 3);
    REQUIRE(tokens.lines[0].size() == 2);
    REQUIRE(tokens.lines[0][0].begin == 6);
    REQUIRE(tokens.lines[0][0].kind == TokenKind::Type);
    REQUIRE(tokens.lines[0][1].begin == 16);
    REQUIRE(tokens.lines[0][1].end == 20);
    REQUIRE(tokens.lines[2].size() == 1);
    REQUIRE(tokens.lines[2][0].kind == TokenKind::Text);
}

TEST_CASE("fjls's own token types colour as what only a server knows", "[lsp][convert]") {
    const std::vector<TokenKind> kinds = token_kinds(parsed(R"(["uniform","builtin","hint","operator","glow"])"));
    // An operator keeps the lexer's punctuation; a name nobody gave a colour
    // is plain.
    REQUIRE(kinds == std::vector<TokenKind>{TokenKind::Uniform, TokenKind::Builtin, TokenKind::Meta,
                                            TokenKind::Punctuation, TokenKind::Text});
}

TEST_CASE("A template's angle brackets are brackets, a less-than punctuation", "[lsp][convert]") {
    // clangd names a template's or a cast's < and > a bracket, and a less-than
    // an operator.
    const std::vector<TokenKind> kinds = token_kinds(parsed(R"(["bracket","operator"])"));
    REQUIRE(kinds == std::vector<TokenKind>{TokenKind::Bracket, TokenKind::Punctuation});
}

TEST_CASE("Why a completion was asked goes to the server as its context", "[lsp][convert]") {
    REQUIRE(completion_context({}) == parsed(R"({"triggerKind":1})"));
    REQUIRE(completion_context({.kind = CompletionTrigger::Kind::Character, .character = "@"}) ==
            parsed(R"({"triggerKind":2,"triggerCharacter":"@"})"));
    REQUIRE(completion_context({.kind = CompletionTrigger::Kind::Incomplete, .character = {}}) ==
            parsed(R"({"triggerKind":3})"));
}

TEST_CASE("An entry with no edit of its own replaces the word as the file's language has it", "[lsp][convert]") {
    const Json answer = parsed(R"json([{"label":"justify-content"}])json");
    // A sheet's word runs through its hyphens; C++'s stops at one.
    const std::vector<Completion> sheet = completions_from(answer, Position{0, 13}, "    justify-c", language_for("hud.fjss"), UTF8);
    REQUIRE(sheet.size() == 1);
    REQUIRE(sheet[0].change == Change{.span = {{0, 4}, {0, 13}}, .text = "justify-content"});
    const std::vector<Completion> code = completions_from(answer, Position{0, 13}, "    justify-c", language_for("x.cpp"), UTF8);
    REQUIRE(code[0].change == Change{.span = {{0, 12}, {0, 13}}, .text = "justify-content"});
    // A widget's runs through its colons as well.
    const Json click = parsed(R"json([{"label":"on:click"}])json");
    const std::vector<Completion> widget =
        completions_from(click, Position{0, 14}, "    <div on:cl", language_for("hud.fjwidget"), UTF8);
    REQUIRE(widget[0].change == Change{.span = {{0, 9}, {0, 14}}, .text = "on:click"});
}

TEST_CASE("Colour values and a colour's forms are read as the server counts, a colour sent as it writes one",
          "[lsp][convert]") {
    // An e with an accent is two bytes and one unit.
    const std::string line = "/* \xC3\xA9 */ .a { color: #ff8040; border: 1 solid rgba(0, 0, 0, 0.5); }";
    const Positions utf16(ftk::jsonrpc::Encoding::Utf16, [&](std::size_t) { return std::string_view(line); });
    const Json answer = parsed(R"([
        {"range":{"start":{"line":0,"character":45},"end":{"line":0,"character":63}},
         "color":{"red":0,"green":0,"blue":0,"alpha":0.5}},
        {"range":{"start":{"line":0,"character":20},"end":{"line":0,"character":27}},
         "color":{"red":1,"green":0.5,"blue":0.25}}])");
    const std::vector<ColourValue> values = colour_values_from(answer, utf16);
    // In order, each where its bytes are; a channel left out is opaque.
    REQUIRE(values.size() == 2);
    CHECK(values[0].span == Span{{0, 21}, {0, 28}});
    CHECK(values[0].rgba == std::array<float, 4>{1.0f, 0.5f, 0.25f, 1.0f});
    CHECK(line.substr(values[1].span.from.byte, values[1].span.to.byte - values[1].span.from.byte) ==
          "rgba(0, 0, 0, 0.5)");
    CHECK(values[1].rgba[3] == 0.5f);
    CHECK(colour_values_from(Json(), utf16).empty());

    CHECK(colour_json({1.0f, 0.5f, 0.25f, 1.0f}) == parsed(R"({"red":1.0,"green":0.5,"blue":0.25,"alpha":1.0})"));

    // A form with an edit of its own, then one with its label alone, written
    // over the value asked about.
    const Json forms = parsed(R"json([
        {"label":"#3fb8c4","textEdit":{"range":{"start":{"line":0,"character":20},"end":{"line":0,"character":27}},
                                       "newText":"#3fb8c4"}},
        {"label":"rgb(63, 184, 196)"}])json");
    const Span asked{{0, 21}, {0, 28}};
    CHECK(colour_forms_from(forms, asked, utf16) ==
          std::vector<Change>{{.span = asked, .text = "#3fb8c4"}, {.span = asked, .text = "rgb(63, 184, 196)"}});
    CHECK(colour_forms_from(Json(), asked, utf16).empty());
}
