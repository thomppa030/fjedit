#include "fjedit/core/vim/ex.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace fjedit::vim;

namespace {

using Kind = ExAddress::Kind;

ExAddress at(Kind kind, long offset = 0) {
    return {.kind = kind, .number = 0, .mark = 0, .pattern = {}, .offset = offset};
}

ExAddress line(std::size_t number, long offset = 0) {
    return {.kind = Kind::Number, .number = number, .mark = 0, .pattern = {}, .offset = offset};
}

ExAddress mark(char name) {
    return {.kind = Kind::Mark, .number = 0, .mark = name, .pattern = {}, .offset = 0};
}

} // namespace

TEST_CASE("A range alone, or the whole text, and a name short or long", "[vim][ex]") {
    const auto twelve = parse_ex("12");
    REQUIRE(twelve);
    CHECK(twelve->range.from == line(12));
    CHECK(twelve->name.empty());

    const auto all = parse_ex("%d");
    REQUIRE(all);
    CHECK(all->range.whole);
    CHECK(all->name == "delete");
    CHECK(parse_ex("de")->name == "delete");
    CHECK(parse_ex("delete")->name == "delete");
    CHECK(parse_ex("co")->name == "copy");
    CHECK(parse_ex("sor")->name == "sort");
    CHECK(parse_ex("noh")->name == "nohlsearch");
    CHECK(parse_ex("vs")->name == "vsplit");
    CHECK(parse_ex(":::wq")->name == "wq");
}

TEST_CASE("Ranges: marks, offsets, patterns, and a semicolon", "[vim][ex]") {
    const auto visual = parse_ex("'<,'>s/a/b/g");
    REQUIRE(visual);
    CHECK(visual->range.from == mark('<'));
    CHECK(visual->range.to == mark('>'));
    CHECK(visual->name == "substitute");
    CHECK(visual->argument == "/a/b/g");

    const auto yank = parse_ex(".,$-2y x");
    REQUIRE(yank);
    CHECK(yank->range.from == at(Kind::Current));
    CHECK(yank->range.to == at(Kind::Last, -2));
    CHECK(yank->argument == "x");

    const auto found = parse_ex("/foo\\/bar/;+3d");
    REQUIRE(found);
    CHECK(found->range.from->kind == Kind::Forward);
    CHECK(found->range.from->pattern == "foo\\/bar");
    CHECK(found->range.from_first);
    CHECK(found->range.to == at(Kind::Current, 3));

    const auto back = parse_ex("?x?-");
    REQUIRE(back);
    CHECK(back->range.from->kind == Kind::Backward);
    CHECK(back->range.from->offset == -1);
    // A comma with nothing after it is the caret's line.
    CHECK(parse_ex("3,d")->range.to == at(Kind::Current));
}

TEST_CASE("What follows a name, a bang, and shifts by their length", "[vim][ex]") {
    CHECK(parse_ex("t.")->name == "t");
    CHECK(parse_ex("t.")->argument == ".");
    CHECK(parse_ex("m0")->argument == "0");
    const auto global = parse_ex("g!/x/d");
    REQUIRE(global);
    CHECK(global->name == "global");
    CHECK(global->bang);
    CHECK(global->argument == "/x/d");
    const auto shift = parse_ex("2,3>> 4");
    REQUIRE(shift);
    CHECK(shift->name == ">");
    CHECK(shift->argument == ">4");
}

TEST_CASE("A name the line doesn't know is refused as Vim words it", "[vim][ex]") {
    CHECK(parse_ex("frob").error() == "E492: Not an editor command: frob");
    // Too short to name one.
    CHECK_FALSE(parse_ex("c"));
    CHECK_FALSE(parse_ex("so"));
    CHECK_FALSE(parse_ex("nor"));
    CHECK_FALSE(parse_ex("#"));
}
