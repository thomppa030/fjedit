#include "fjedit/core/vim/ex.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

TEST_CASE("A pattern ends at its delimiter, not one escaped or in a collection", "[vim][ex]") {
    const Delimited plain = split_pattern("gust/wind/g", '/');
    CHECK(plain.pattern == "gust");
    CHECK(plain.rest == "wind/g");
    CHECK(plain.ended);
    CHECK(split_pattern("a\\/b/c", '/').pattern == "a\\/b");
    CHECK(split_pattern("[/]x/y", '/').pattern == "[/]x");
    const Delimited open = split_pattern("gu", '/');
    CHECK(open.pattern == "gu");
    CHECK_FALSE(open.ended);
    CHECK(pattern_delimiter('#'));
    CHECK_FALSE(pattern_delimiter('a'));
    CHECK_FALSE(pattern_delimiter('"'));
}

TEST_CASE("Counts, registers, shifts and targets after a name read as Vim reads them", "[vim][ex]") {
    CHECK(read_count("")->has_value() == false);
    CHECK(read_count(" 3") == std::optional<std::size_t>{3});
    CHECK(read_count("x").error() == "E488: Trailing characters: x");
    CHECK(read_count("0").error() == "E939: Positive count required");
    const auto into_a = read_register_count("a 2");
    REQUIRE(into_a);
    CHECK(into_a->reg == 'a');
    CHECK(into_a->count == std::optional<std::size_t>{2});
    CHECK(read_register_count("3")->reg == '"');
    const auto shift = read_shift('>', ">> 4");
    REQUIRE(shift);
    CHECK(shift->levels == 3);
    CHECK(shift->count == std::optional<std::size_t>{4});
    CHECK(read_target("$") == at(Kind::Last));
    CHECK(read_target("0") == line(0));
    CHECK(read_target("1,2").error() == "E14: Invalid address");
    CHECK(read_target("d").error() == "E14: Invalid address");
}

TEST_CASE(":s's argument: pattern, replacement, flags in order, and a count", "[vim][ex]") {
    const auto every = read_substitute("/gust/wind/gi 2");
    REQUIRE(every);
    REQUIRE(every->written);
    CHECK(every->written->pattern == "gust");
    CHECK(every->written->with == "wind");
    CHECK(every->every == SubstituteArgument::Every::Yes);
    CHECK(every->match_case == std::optional<bool>{false});
    CHECK(every->count == std::optional<std::size_t>{2});
    // Typed as far as the pattern: no replacement yet.
    const auto typing = read_substitute("#a#");
    REQUIRE(typing);
    CHECK(typing->written->pattern == "a");
    CHECK(typing->written->with.empty());
    // Flags alone go on with the last; the later of g and & wins.
    const auto again = read_substitute("g&");
    REQUIRE(again);
    CHECK_FALSE(again->written);
    CHECK(again->every == SubstituteArgument::Every::AsLast);
    CHECK(read_substitute("/a/b/x").error() == "E488: Trailing characters: x");
}

TEST_CASE(":g's, :set's and :sort's arguments read as Vim reads them", "[vim][ex]") {
    const auto global = read_global("/TODO/d");
    REQUIRE(global);
    CHECK(global->pattern == "TODO");
    CHECK(global->command == "d");
    CHECK(read_global("TODO").error() == "E476: Invalid command");

    const auto off = read_set_item("noic");
    REQUIRE(off);
    CHECK(off->option == SetOption::IgnoreCase);
    CHECK(off->name == "ignorecase");
    CHECK(off->action == SetItem::Action::Off);
    CHECK(read_set_item("hls!")->action == SetItem::Action::Toggle);
    CHECK(read_set_item("invrnu")->action == SetItem::Action::Toggle);
    CHECK(read_set_item("so")->action == SetItem::Action::Ask);
    const auto width = read_set_item("sw=2");
    REQUIRE(width);
    CHECK(width->action == SetItem::Action::Value);
    CHECK(width->value == 2);
    CHECK(read_set_item("so=0")->value == 0);
    CHECK(read_set_item("foo").error() == "E518: Unknown option: foo");
    CHECK(read_set_item("ic=1").error() == "E474: Invalid argument: ic=1");
    CHECK(read_set_item("nosw").error() == "E474: Invalid argument: nosw");
    CHECK(read_set_item("sw!").error() == "E474: Invalid argument: sw!");
    CHECK(read_set_item("so=x").error() == "E521: Number required after =: so=x");
    CHECK(read_set_item("sw=0").error() == "E487: Argument must be positive: sw=0");

    const auto sort = read_sort("n u");
    REQUIRE(sort);
    CHECK(sort->by_number);
    CHECK(sort->unique);
    CHECK_FALSE(sort->fold_case);
    CHECK(read_sort("/x/").error() == "Not supported yet: :sort with a pattern");
    CHECK(read_sort("q").error() == "E474: Invalid argument: q");
}

TEST_CASE("A name being typed lists the commands it can become, after any range", "[vim][ex]") {
    const auto names_of = [](const NameTyped& typed) {
        std::vector<std::string_view> out;
        for (const ExCommandName& command : typed.names) out.push_back(command.name);
        return out;
    };
    const auto w = name_typed("w");
    REQUIRE(w);
    CHECK(w->at == 0);
    CHECK(w->typed == "w");
    CHECK(names_of(*w) == std::vector<std::string_view>{"wall", "wincmd", "wq", "write"});
    const auto whole = name_typed("%s");
    REQUIRE(whole);
    CHECK(whole->at == 1);
    CHECK(names_of(*whole) == std::vector<std::string_view>{"set", "sort", "split", "substitute"});
    CHECK(name_typed("'<,'>no")->at == 5);
    // Before a letter, past the name, or a name no command begins as.
    CHECK_FALSE(name_typed(""));
    CHECK_FALSE(name_typed("12"));
    CHECK_FALSE(name_typed("w "));
    CHECK_FALSE(name_typed("w!"));
    CHECK_FALSE(name_typed("s/"));
    CHECK_FALSE(name_typed("zz"));
    // Every command has its words, and reads by its whole name.
    for (const ExCommandName& command : ex_commands()) {
        CHECK_FALSE(command.words.empty());
        CHECK(parse_ex(command.name)->name == command.name);
    }
}

TEST_CASE("A path typed after :e splits into its folder and the name begun", "[vim][ex]") {
    const auto deep = path_typed("e src/fjedit/co");
    REQUIRE(deep);
    CHECK(deep->at == 2);
    CHECK(deep->folder == "src/fjedit/");
    CHECK(deep->name == "co");
    const auto fresh = path_typed("edit! ");
    REQUIRE(fresh);
    CHECK(fresh->folder.empty());
    CHECK(fresh->name.empty());
    CHECK(path_typed("ed  .cl")->name == ".cl");
    // Not :e, or no blank after it yet.
    CHECK_FALSE(path_typed("e"));
    CHECK_FALSE(path_typed("w src/"));
    CHECK_FALSE(path_typed("ex src/"));
}
