#include "fjedit/editor/code_area.hpp"

#include "code_area_running.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace fjedit;
using ftk::ui::Key;
using ftk::ui::core::Role;

namespace {

constexpr ftk::ui::Modifiers CTRL_SHIFT{.ctrl = true, .shift = true};

} // namespace

TEST_CASE("The code area opens a file in a tab in front, and goes to a place in it", "[editor][area]") {
    Area a({{"a.cpp", "int a;\nint speed;\n"}});
    CHECK_FALSE(a.code->front().has_value());
    a.open("a.cpp");
    CHECK(a.h.find("a.cpp", Role::Tab) != nullptr);

    a.code->go_to(a.folder.path / "a.cpp", {{1, 4}, {1, 9}});
    a.h.step(2);
    const CodeStatus status = a.code->status();
    REQUIRE(status.place.has_value());
    CHECK(status.place->line == 2);
    CHECK(status.place->column == 10);
}

TEST_CASE("The program's commands run by their keys and by name; others are no command", "[editor][area]") {
    Area a({});
    a.h.press(Key::F5);
    a.h.step();
    CHECK(a.ran == std::vector<std::string>{"game.play"});
    CHECK(a.code->run("game.play"));
    CHECK(a.ran.size() == 2);
    // One of no one's is not handed to the program.
    CHECK_FALSE(a.code->run("game.nothing"));
    CHECK(a.ran.size() == 2);
    CHECK(a.code->shortcut("game.play") == "F5");
}

TEST_CASE("keys.vim binds the program's commands as the area's own, and names one no one has", "[editor][area]") {
    Area a({}, [](const TempFolder& folder) {
        folder.write(".config/keys.vim", "bind program <F6> game.play\nbind program <F7> game.nothing\n");
    });
    REQUIRE(a.code->keys_errors().size() == 1);
    CHECK(a.code->keys_errors().front().line == 2);
}

TEST_CASE("With Vim on, :qa asks the program to leave, and is refused over unsaved changes but with !",
          "[editor][area][vim]") {
    Area a({{"a.txt", "abc\n"}});
    a.preferences.vim = true;
    a.open("a.txt");
    a.ex("qa");
    CHECK(a.left == 1);

    a.h.type("x");
    a.h.step();
    REQUIRE(a.files->unsaved(a.at("a.txt")));
    a.ex("qa");
    CHECK(a.left == 1);
    a.ex("qa!");
    CHECK(a.left == 2);
}

TEST_CASE("Find in files floats over the room it is given, and opens what it finds in the area", "[editor][area]") {
    Area a({{"a.cpp", "float spin_speed;\n"}, {"b.cpp", "x = 1;\n"}});
    a.h.press(Key::F, CTRL_SHIFT);
    a.h.step();
    REQUIRE(a.code->find_in_files().is_open());
    a.h.type("spin_speed");
    for (int i = 0; i < 400 && a.code->find_in_files().hits().empty(); ++i) a.h.step();
    REQUIRE(a.code->find_in_files().hits().size() == 1);
    a.h.press(Key::Enter);
    a.h.step(2);
    CHECK_FALSE(a.code->find_in_files().is_open());
    CHECK(a.code->front() == a.at("a.cpp"));
}

TEST_CASE("The code area offers the work kept from before by itself, and Recover brings it back unsaved",
          "[editor][area]") {
    Area a({{"a.cpp", "int a;\n"}}, [](const TempFolder& folder) {
        REQUIRE(Recovery(folder.path / ".recovery").keep(Workspace(folder.path).name("a.cpp"), "int kept;\n"));
    });
    const auto* recover = a.h.find("Recover 1 file", Role::Button);
    REQUIRE(recover != nullptr);
    a.h.click_at(recover->rect.centre());
    a.h.step(2);
    REQUIRE(a.code->front() == a.at("a.cpp"));
    CHECK(a.files->workspace().find(a.at("a.cpp"))->buffer().text() == "int kept;\n");
    CHECK(a.files->unsaved(a.at("a.cpp")));
    CHECK(a.folder.read("a.cpp") == "int a;\n");
}

TEST_CASE("A tab closed with unsaved changes asks first, and Don't save lets its file go", "[editor][area]") {
    Area a({{"a.cpp", "int a;\n"}});
    a.open("a.cpp");
    a.h.type("x");
    a.h.step();
    REQUIRE(a.code->run("file.close"));
    a.h.step(2);
    REQUIRE(a.code->front().has_value());
    const auto* dont_save = a.h.find("Don't save", Role::Button);
    REQUIRE(dont_save != nullptr);
    a.h.click_at(dont_save->rect.centre());
    a.h.step(2);
    CHECK_FALSE(a.code->front().has_value());
    CHECK(a.files->workspace().files().empty());
    CHECK(a.folder.read("a.cpp") == "int a;\n");
}
