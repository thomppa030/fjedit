#include "fjedit/core/document.hpp"

#include "print.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace fjedit;

static Edit insert(Position at, std::string text) {
    return {{{.span = {at, at}, .text = std::move(text)}}};
}

TEST_CASE("A document's colours follow its edits", "[document]") {
    Document document("a();\nb();", language_for("x.cpp"));
    REQUIRE(document.syntax().tokens(1).front().kind == TokenKind::Function);
    (void)document.apply(insert({0, 0}, "/* "));
    REQUIRE(document.syntax().tokens(1).front().kind == TokenKind::Comment);
}

TEST_CASE("Undo and redo through the document keep the colours in step", "[document]") {
    Document document("a();\nb();", language_for("x.cpp"));
    const Selections before;
    const Edit made = insert({0, 0}, "/* ");
    Edit undo = document.apply(made);
    document.history().record({.made = made, .undo = std::move(undo), .before = before, .after = before,
                               .kind = EditKind::Other});

    REQUIRE(document.undo().has_value());
    REQUIRE(document.buffer().text() == "a();\nb();");
    REQUIRE(document.syntax().tokens(1).front().kind == TokenKind::Function);

    REQUIRE(document.redo().has_value());
    REQUIRE(document.syntax().tokens(1).front().kind == TokenKind::Comment);
    REQUIRE_FALSE(document.redo().has_value());
}

TEST_CASE("The edits since a version, in order", "[document]") {
    Document document("x", language_for("x.txt"));
    const uint64_t start = document.version();
    REQUIRE(document.edits_since(start)->empty());

    (void)document.apply(insert({0, 1}, "y"));
    (void)document.apply(insert({0, 2}, "z"));
    const std::optional<std::span<const Edit>> edits = document.edits_since(start);
    REQUIRE(edits->size() == 2);
    REQUIRE((*edits)[1] == insert({0, 2}, "z"));
    REQUIRE(document.edits_since(start + 1)->size() == 1);
    REQUIRE_FALSE(document.edits_since(document.version() + 1).has_value());
}

TEST_CASE("Edits too far back are no longer told", "[document]") {
    Document document("", language_for("x.txt"));
    const uint64_t start = document.version();
    for (std::size_t i = 0; i < 2 * Document::JOURNAL; ++i) (void)document.apply(insert({0, 0}, "a"));
    REQUIRE_FALSE(document.edits_since(start).has_value());
    REQUIRE(document.edits_since(document.version() - Document::JOURNAL)->size() == Document::JOURNAL);
}

TEST_CASE("A second view's selections follow edits made through the first", "[document]") {
    Document document("one\ntwo", language_for("x.txt"));
    Selections other(Selection::at({1, 1}));
    const uint64_t seen = document.version();

    (void)document.apply(insert({0, 0}, "zero\n"));
    const auto edits = document.edits_since(seen);
    REQUIRE(edits.has_value());
    for (const Edit& edit : *edits) other.map(edit);
    REQUIRE(other.primary() == Selection::at({2, 1}));
}

TEST_CASE("Folds are worked out again after an edit", "[document]") {
    Document document("f() {\n}", language_for("x.cpp"));
    REQUIRE(document.folds() == std::vector<FoldRange>{{0, 1}});
    (void)document.apply(insert({1, 0}, "  x();\n"));
    REQUIRE(document.folds() == std::vector<FoldRange>{{0, 2}});
}

TEST_CASE("A document's indent is the one its text shows", "[document]") {
    const Document tabs("a {\n\tb;\n}", language_for("x.cpp"));
    REQUIRE(tabs.indent().tabs);
    const Document plain("a\nb", language_for("x.cpp"), {.tabs = false, .width = 3});
    REQUIRE(plain.indent().width == 3);
}
