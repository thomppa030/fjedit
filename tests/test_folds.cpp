#include "fjedit/core/folds.hpp"
#include "fjedit/core/language.hpp"

#include "print.hpp"
#include "view.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace fjedit;

namespace {

// Folds on lines 0..6, 1..3 and 3..5: a function, and an if and its else.
constexpr std::string_view BRANCHES = "void f() {\n"   // 0
                                      "    if (a) {\n" // 1
                                      "        x();\n" // 2
                                      "    } else {\n" // 3
                                      "        y();\n" // 4
                                      "    }\n"        // 5
                                      "}\n"            // 6
                                      "z();";          // 7

// Two blocks, folding on lines 0..2 and 3..5.
constexpr std::string_view TWO_BLOCKS = "a {\n  b\n}\nc {\n  d\n}";

Edit insert(Position at, std::string text) {
    return {.changes = {{.span = {at, at}, .text = std::move(text)}}};
}

Edit erase(Position from, Position to) {
    return {.changes = {{.span = {from, to}, .text = ""}}};
}

} // namespace

TEST_CASE("A closed fold hides the lines after its first, through its closer", "[folds]") {
    const Document document(BRANCHES, language_for("x.cpp"));
    Folds folds;
    REQUIRE(folds.close(document, 1));
    REQUIRE(folds.closed_at(1) == FoldRange{1, 3});
    REQUIRE_FALSE(folds.hidden(1));
    REQUIRE(folds.hidden(2));
    REQUIRE(folds.hidden(3));
    REQUIRE_FALSE(folds.hidden(4));
    REQUIRE(folds.row_lines(1) == FoldRange{1, 3});
    REQUIRE(folds.row_lines(4) == FoldRange{4, 4});
    REQUIRE(folds.row_count(8) == 6);
    REQUIRE(folds.row_of(4) == 2);
    REQUIRE(folds.row_of(3) == 1);
    REQUIRE(folds.line_of(2) == 4);
}

TEST_CASE("Closing needs a fold to begin there, not closed yet", "[folds]") {
    const Document document(BRANCHES, language_for("x.cpp"));
    Folds folds;
    REQUIRE_FALSE(folds.close(document, 2));
    REQUIRE(folds.close(document, 0));
    REQUIRE_FALSE(folds.close(document, 0));
    REQUIRE(folds.open(0));
    REQUIRE_FALSE(folds.open(0));
}

TEST_CASE("A fold beginning where a closed one ends folds on along its row", "[folds]") {
    const Document document(BRANCHES, language_for("x.cpp"));
    Folds folds;
    REQUIRE(folds.close(document, 1));
    REQUIRE(folds.close(document, 3));
    REQUIRE(folds.row_lines(1) == FoldRange{1, 5});
    REQUIRE(folds.row_count(8) == 4);
    REQUIRE(folds.line_of(2) == 6);

    // Opening the row opens both.
    REQUIRE(folds.open_row(1));
    REQUIRE_FALSE(folds.closed_at(1));
    REQUIRE_FALSE(folds.closed_at(3));
    REQUIRE(folds.row_count(8) == 8);
}

TEST_CASE("A fold closed inside a closed one is still closed when that opens", "[folds]") {
    const Document document(BRANCHES, language_for("x.cpp"));
    Folds folds;
    REQUIRE(folds.close(document, 1));
    REQUIRE(folds.close(document, 0));
    REQUIRE(folds.row_lines(0) == FoldRange{0, 6});
    REQUIRE(folds.row_count(8) == 2);

    REQUIRE(folds.open_row(0));
    REQUIRE(folds.row_lines(1) == FoldRange{1, 3});
}

TEST_CASE("Revealing a line opens the folds that hide it, and only those", "[folds]") {
    const Document document(BRANCHES, language_for("x.cpp"));
    Folds folds;
    REQUIRE(folds.close(document, 0));
    REQUIRE(folds.close(document, 1));
    REQUIRE(folds.reveal(5));
    REQUIRE_FALSE(folds.closed_at(0));
    REQUIRE(folds.closed_at(1));
    REQUIRE_FALSE(folds.reveal(5));

    REQUIRE(folds.close(document, 0));
    REQUIRE(folds.reveal(2));
    REQUIRE_FALSE(folds.closed_at(0));
    REQUIRE_FALSE(folds.closed_at(1));
}

TEST_CASE("Closed folds go with their lines through edits", "[folds]") {
    Document document(BRANCHES, language_for("x.cpp"));
    Folds folds;
    REQUIRE(folds.close(document, 1));

    // A line above pushes the fold down.
    (void)document.apply(insert({0, 0}, "w();\n"));
    folds.catch_up(document);
    REQUIRE(folds.closed_at(2) == FoldRange{2, 4});

    // Typing on its first line, or joining it onto a line above, keeps it.
    (void)document.apply(insert({2, 8}, " b &&"));
    (void)document.apply(insert({2, 0}, "a();\n"));
    (void)document.apply(erase({2, 4}, {3, 0}));
    folds.catch_up(document);
    REQUIRE(folds.closed_at(2) == FoldRange{2, 4});
}

TEST_CASE("A fold whose lines are edited elsewhere stays closed over them", "[folds]") {
    Document document(TWO_BLOCKS, language_for("x.cpp"));
    Folds folds;
    REQUIRE(folds.close(document, 0));
    (void)document.apply(insert({1, 0}, "  e\n"));
    folds.catch_up(document);
    REQUIRE(folds.closed_at(0) == FoldRange{0, 3});
}

TEST_CASE("A fold whose block is deleted opens, and doesn't close the next", "[folds]") {
    Document document(TWO_BLOCKS, language_for("x.cpp"));
    Folds folds;
    REQUIRE(folds.close(document, 0));
    (void)document.apply(erase({0, 0}, {3, 0}));
    folds.catch_up(document);
    REQUIRE(document.folds() == std::vector<FoldRange>{{0, 2}});
    REQUIRE_FALSE(folds.closed_at(0));
    REQUIRE(folds.row_count(document.buffer().line_count()) == 3);
}

TEST_CASE("A fold whose closer is taken away opens", "[folds]") {
    Document document(TWO_BLOCKS, language_for("x.cpp"));
    Folds folds;
    REQUIRE(folds.close(document, 0));
    (void)document.apply(erase({2, 0}, {2, 1}));
    folds.catch_up(document);
    REQUIRE_FALSE(folds.closed_at(0));
}

TEST_CASE("Folds open when the document no longer has the edits to follow", "[folds]") {
    Document document(TWO_BLOCKS, language_for("x.cpp"));
    Folds folds;
    REQUIRE(folds.close(document, 3));
    // The journal keeps at least JOURNAL edits, trimming twice as many.
    for (std::size_t i = 0; i < 2 * Document::JOURNAL; ++i) (void)document.apply(insert({0, 0}, " "));
    folds.catch_up(document);
    REQUIRE_FALSE(folds.closed_at(3));
}

TEST_CASE("Rows and lines answer for each other however folds are closed", "[folds]") {
    // Blocks inside blocks, with an else after each: every kind of nesting
    // and folding on.
    std::string text;
    for (int outer = 0; outer < 6; ++outer) {
        text += "if (a) {\n";
        for (int inner = 0; inner < 3; ++inner) text += "  if (b) {\n    x();\n  } else {\n    y();\n  }\n";
        text += "} else {\n  z();\n}\n";
    }
    const Document document(text, language_for("x.cpp"));
    const std::vector<FoldRange>& ranges = document.folds();
    const std::size_t lines = document.buffer().line_count();

    for (std::size_t pattern = 0; pattern < 64; ++pattern) {
        Folds folds;
        for (std::size_t i = 0; i < ranges.size(); ++i) {
            if (((pattern * 2654435761U) >> (i % 29)) & 1U) (void)folds.close(document, ranges[i].first);
        }
        const std::size_t rows = folds.row_count(lines);
        std::size_t previous = 0;
        for (std::size_t row = 0; row < rows; ++row) {
            const std::size_t line = folds.line_of(row);
            REQUIRE_FALSE(folds.hidden(line));
            REQUIRE(folds.row_of(line) == row);
            if (row > 0) REQUIRE(line > previous);
            previous = line;
        }
        for (std::size_t line = 0; line < lines; ++line) {
            if (!folds.hidden(line)) continue;
            const std::size_t shown = folds.line_of(folds.row_of(line));
            REQUIRE(shown < line);
            REQUIRE(folds.row_lines(shown).last >= line);
        }
    }
}

TEST_CASE("Carets a fold closes over go to the end of its first line", "[folds]") {
    const Document document(BRANCHES, language_for("x.cpp"));
    Folds folds;
    Selections selections({.anchor = {2, 4}, .caret = {2, 8}});
    selections.add(Selection::at({7, 0}));
    REQUIRE(folds.close(document, 1));
    move_out_of_folds(selections, folds, document.buffer());
    REQUIRE(selections.all()[0] == Selection::at({1, 12}));
    REQUIRE(selections.primary() == Selection::at({7, 0}));
}

TEST_CASE("Fold commands close the innermost open fold at each caret", "[folds][commands]") {
    TestView view("void f() {\n    if (a) {\n        x|();\n    }\n}");
    view.run("fold.close");
    REQUIRE(view.folds.closed_at(1));
    REQUIRE(view.shown() == "void f() {\n    if (a) {|\n        x();\n    }\n}");

    // Again from the fold's own line: the one around it.
    view.run("fold.close");
    REQUIRE(view.folds.closed_at(0));
    REQUIRE(view.carets() == std::vector<Position>{{0, 10}});

    view.run("fold.open");
    REQUIRE_FALSE(view.folds.closed_at(0));
    REQUIRE(view.folds.closed_at(1));

    view.run("fold.toggle");
    REQUIRE(view.folds.closed_at(0));
    view.run("fold.open_all");
    REQUIRE_FALSE(view.folds.closed_at(0));
    REQUIRE_FALSE(view.folds.closed_at(1));
    view.run("fold.close_all");
    REQUIRE(view.folds.closed_at(0));
    REQUIRE(view.folds.closed_at(1));
}

TEST_CASE("Down steps over a closed fold, and a column skips its lines", "[folds][commands]") {
    TestView view("a {|\n  b\n}\nc");
    view.run("fold.close");
    view.run("caret.down");
    REQUIRE(view.carets() == std::vector<Position>{{3, 1}});
    view.run("caret.up");
    REQUIRE(view.carets() == std::vector<Position>{{0, 3}});

    const Selections column = column_selection(view.buffer(), view.layout, 0, 0.0f, 3, 1.0f);
    REQUIRE(column.size() == 2);
}
