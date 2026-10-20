#include "fjedit/core/jump_list.hpp"

#include "print.hpp"
#include "temp_folder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

using namespace fjedit;

namespace {

// A place at the start of line `line` of the file `file`, its text as it
// will be when the file next opens.
JumpList::Place place(std::string_view file, std::size_t line) {
    return {.path = std::filesystem::path(file), .at = {line, 0}, .version = {}};
}

Edit insert(Position at, std::string text) {
    return {{{.span = {at, at}, .text = std::move(text)}}};
}

} // namespace

TEST_CASE("Ctrl-O goes back through the places jumped from and Ctrl-I forward; the first step back keeps its start",
          "[jumps][vim]") {
    JumpList jumps;
    jumps.jumped(place("a", 1));
    jumps.jumped(place("a", 5));
    jumps.jumped(place("b", 2));
    // Nothing is forward of the end.
    CHECK_FALSE(jumps.step(1, place("b", 9)));
    CHECK(jumps.step(-1, place("b", 9)) == place("b", 2));
    CHECK(jumps.places().size() == 4);
    CHECK(jumps.step(-2, place("b", 2)) == place("a", 1));
    CHECK_FALSE(jumps.step(-1, place("a", 1)));
    CHECK(jumps.step(3, place("a", 1)) == place("b", 9));
    CHECK_FALSE(jumps.step(1, place("b", 9)));
}

TEST_CASE("A jump goes on the end, even from the middle; a line comes back once; the oldest of a hundred goes",
          "[jumps][vim]") {
    JumpList jumps;
    jumps.jumped(place("a", 1));
    jumps.jumped(place("a", 3));
    jumps.jumped(place("a", 1));
    CHECK(jumps.step(-1, place("a", 7)) == place("a", 1));
    CHECK(jumps.places().size() == 3);
    CHECK(jumps.step(-1, place("a", 1)) == place("a", 3));
    jumps.jumped(place("a", 9));
    CHECK(jumps.places().size() == 4);
    CHECK(jumps.at() == 4);
    CHECK(jumps.step(-1, place("a", 2)) == place("a", 9));

    JumpList full;
    for (std::size_t line = 0; line <= JumpList::SIZE; ++line) full.jumped(place("a", line));
    CHECK(full.places().size() == JumpList::SIZE);
    CHECK(full.places().front() == place("a", 1));
}

TEST_CASE("The places catch up with their file's edits, closed and opened again", "[jumps][vim]") {
    const TempFolder folder;
    const std::filesystem::path file = folder.write("a.txt", "one\ntwo\nthree\n");
    Workspace workspace(folder.path);
    const ftk::Result<Document*> opened = workspace.open(file);
    REQUIRE(opened);
    Document& document = **opened;
    const std::filesystem::path path = workspace.name(file);

    JumpList jumps;
    jumps.jumped({.path = path, .at = {2, 1}, .version = document.version()});
    (void)document.apply(insert({0, 0}, "zero\n"));
    jumps.catch_up(workspace);
    CHECK(jumps.places().front().at == Position{3, 1});

    // Let go, it stays as the text last was; opened again, it catches up
    // from there.
    (void)document.apply(insert({0, 0}, "minus\n"));
    jumps.closed(path, document);
    CHECK(jumps.places().front().at == Position{4, 1});
    CHECK_FALSE(jumps.places().front().version);
    jumps.catch_up(workspace);
    CHECK(jumps.places().front().at == Position{4, 1});
    jumps.opened(path, document.version());
    (void)document.apply(insert({0, 0}, "more\n"));
    jumps.catch_up(workspace);
    CHECK(jumps.places().front().at == Position{5, 1});
}
