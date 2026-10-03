#include "fjedit/core/selections.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace fjedit;

static std::vector<Selection> all(const Selections& selections) {
    return {selections.all().begin(), selections.all().end()};
}

static Selection sel(Position anchor, Position caret) {
    return {.anchor = anchor, .caret = caret};
}

TEST_CASE("A view starts with one caret at the start", "[selections]") {
    const Selections selections;
    REQUIRE(all(selections) == std::vector<Selection>{Selection::at({0, 0})});
    REQUIRE(selections.primary() == Selection::at({0, 0}));
}

TEST_CASE("A selection knows its span whichever way it was made", "[selections]") {
    REQUIRE(sel({1, 4}, {1, 9}).span() == Span{{1, 4}, {1, 9}});
    REQUIRE(sel({1, 9}, {1, 4}).span() == Span{{1, 4}, {1, 9}});
    REQUIRE(sel({1, 9}, {1, 4}).reversed());
    REQUIRE(Selection::at({2, 2}).empty());
}

TEST_CASE("Added selections are kept in order and the newest is the primary", "[selections]") {
    Selections selections(Selection::at({5, 0}));
    selections.add(Selection::at({1, 3}));
    selections.add(Selection::at({9, 1}));
    REQUIRE(all(selections) == std::vector<Selection>{Selection::at({1, 3}), Selection::at({5, 0}), Selection::at({9, 1})});
    REQUIRE(selections.primary_index() == 2);

    selections.add(Selection::at({3, 0}));
    REQUIRE(selections.primary() == Selection::at({3, 0}));
    REQUIRE(selections.primary_index() == 1);
}

TEST_CASE("Overlapping selections become one", "[selections]") {
    Selections selections(sel({0, 2}, {0, 8}));
    selections.add(sel({0, 5}, {0, 12}));
    REQUIRE(all(selections) == std::vector<Selection>{sel({0, 2}, {0, 12})});
    REQUIRE(selections.primary_index() == 0);
}

TEST_CASE("Carets in one place, or on a selection, become one", "[selections]") {
    Selections carets(Selection::at({4, 4}));
    carets.add(Selection::at({4, 4}));
    REQUIRE(carets.size() == 1);

    Selections on_it(sel({0, 0}, {0, 6}));
    on_it.add(Selection::at({0, 6}));
    REQUIRE(all(on_it) == std::vector<Selection>{sel({0, 0}, {0, 6})});
}

TEST_CASE("Selections that only touch stay apart", "[selections]") {
    Selections selections(sel({0, 0}, {0, 3}));
    selections.add(sel({0, 3}, {0, 6}));
    REQUIRE(selections.size() == 2);
}

TEST_CASE("A merge keeps the primary's direction", "[selections]") {
    Selections selections(sel({0, 0}, {0, 6}));
    selections.add(sel({0, 9}, {0, 4}));
    REQUIRE(all(selections) == std::vector<Selection>{sel({0, 9}, {0, 0})});
}

TEST_CASE("Setting selections follows the primary through the sort", "[selections]") {
    Selections selections;
    selections.set({Selection::at({8, 0}), Selection::at({2, 0}), Selection::at({5, 0})}, 0);
    REQUIRE(selections.primary() == Selection::at({8, 0}));
    REQUIRE(selections.primary_index() == 2);
}

TEST_CASE("Escape leaves the primary alone", "[selections]") {
    Selections selections(Selection::at({1, 0}));
    selections.add(Selection::at({6, 0}));
    selections.add(Selection::at({3, 0}));
    selections.keep_primary();
    REQUIRE(all(selections) == std::vector<Selection>{Selection::at({3, 0})});
}

TEST_CASE("Selections are carried through an edit made elsewhere", "[selections]") {
    Selections selections(Selection::at({0, 4}));
    selections.add(sel({1, 2}, {1, 6}));
    selections.add(Selection::at({3, 0}));

    // Another view types "xy" where the first caret is, and a line above the last.
    selections.map({{{.span = {{0, 4}, {0, 4}}, .text = "xy"}, {.span = {{2, 0}, {2, 0}}, .text = "new\n"}}});
    REQUIRE(all(selections) == std::vector<Selection>{Selection::at({0, 4}), sel({1, 2}, {1, 6}), Selection::at({4, 0})});
}

TEST_CASE("A selection doesn't grow when text goes in at its ends", "[selections]") {
    Selections selections(sel({0, 2}, {0, 5}));
    selections.map({{{.span = {{0, 2}, {0, 2}}, .text = "AA"}, {.span = {{0, 5}, {0, 5}}, .text = "BB"}}});
    REQUIRE(all(selections) == std::vector<Selection>{sel({0, 4}, {0, 7})});
}

TEST_CASE("A selection whose text is replaced shrinks to a caret before it", "[selections]") {
    Selections selections(sel({0, 6}, {0, 2}));
    selections.map({{{.span = {{0, 1}, {0, 8}}, .text = "x"}}});
    REQUIRE(all(selections) == std::vector<Selection>{Selection::at({0, 1})});
}

TEST_CASE("Selections can be put back into a text that shrank", "[selections]") {
    Selections selections(sel({0, 1}, {4, 3}));
    selections.clamp(Buffer("ab\ncd"));
    REQUIRE(all(selections) == std::vector<Selection>{sel({0, 1}, {1, 2})});
}
