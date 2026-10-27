#include "fjedit/editor/code_area.hpp"
#include "fjedit/editor/icons.hpp"

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
    a.h.step();
    for (int i = 0; i < 400 && a.code->find_in_files().searching(); ++i) a.h.step();
    REQUIRE(a.code->find_in_files().hits().size() == 1);
    CHECK(a.found_opened == 0);
    a.h.press(Key::Enter);
    a.h.step(2);
    CHECK_FALSE(a.code->find_in_files().is_open());
    CHECK(a.code->front() == a.at("a.cpp"));
    // Said once, for a host to bring the area's tab forward.
    CHECK(a.found_opened == 1);
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

TEST_CASE("Closing every tab lets every file go, unsaved changes and all, and leaves one empty group",
          "[editor][area]") {
    Area a({{"a.cpp", "int a;\n"}, {"b.cpp", "int b;\n"}});
    a.open("a.cpp");
    a.h.type("x");
    a.h.step();
    REQUIRE(a.files->unsaved(a.at("a.cpp")));
    a.open("b.cpp");
    REQUIRE(a.code->run("view.split"));
    a.h.step(2);
    REQUIRE(a.code->groups() == 2);

    a.code->close_all();
    a.h.step(2);
    CHECK(a.code->shown() == std::vector<GroupFiles>{{.files = {}, .front = 0}});
    CHECK_FALSE(a.code->front().has_value());
    CHECK(a.h.find("a.cpp", Role::Tab) == nullptr);
    CHECK(a.files->workspace().files().empty());
    CHECK(a.folder.read("a.cpp") == "int a;\n");

    // Opened again, it is the file on the disk, alone in its tab.
    a.open("a.cpp");
    CHECK(a.code->shown() == std::vector<GroupFiles>{{.files = {a.at("a.cpp")}, .front = 0}});
    CHECK(a.files->workspace().find(a.at("a.cpp"))->buffer().text() == "int a;\n");
}

TEST_CASE("What the groups show is said, file by file and the one in front of each", "[editor][area]") {
    Area a({{"a.cpp", "int a;\n"}, {"b.cpp", "int b;\n"}, {"c.cpp", "int c;\n"}});
    CHECK(a.code->shown() == std::vector<GroupFiles>{{.files = {}, .front = 0}});
    a.open("a.cpp");
    a.open("b.cpp");
    // b.cpp again in a group beside, which c.cpp then opens in.
    REQUIRE(a.code->run("view.split"));
    a.h.step(2);
    a.open("c.cpp");
    CHECK(a.code->shown() == std::vector<GroupFiles>{{.files = {a.at("a.cpp"), a.at("b.cpp")}, .front = 1},
                                                     {.files = {a.at("b.cpp"), a.at("c.cpp")}, .front = 1}});
}

TEST_CASE("The tab row's Split button opens the file in front again beside it", "[editor][area]") {
    Area a({{"a.cpp", "int a;\n"}});
    a.open("a.cpp");
    // Pressed while its group is drawn, the button splits once every group
    // is: a split moves the list of groups the drawing walks.
    const auto* split = a.h.find(icon::split, Role::Button);
    REQUIRE(split != nullptr);
    a.h.click_at(split->rect.centre());
    a.h.step(2);
    REQUIRE(a.code->groups() == 2);
    CHECK(a.code->shown() == std::vector<GroupFiles>{{.files = {a.at("a.cpp")}, .front = 0},
                                                     {.files = {a.at("a.cpp")}, .front = 0}});
    CHECK(a.files->workspace().files().size() == 1);

    // The group beside has the keyboard.
    a.h.type("x");
    a.h.step();
    CHECK(a.files->workspace().find(a.at("a.cpp"))->buffer().text() == "xint a;\n");
}

TEST_CASE("An area shown what was kept shows the same groups, their files and the ones in front", "[editor][area]") {
    Area a({{"a.cpp", "int a;\n"}, {"b.cpp", "int b;\n"}, {"c.cpp", "int c;\n"}});
    const std::vector<GroupFiles> kept{{.files = {a.at("a.cpp"), a.at("b.cpp")}, .front = 1},
                                       {.files = {a.at("b.cpp"), a.at("c.cpp")}, .front = 0}};
    a.code->show(kept);
    a.h.step(2);
    CHECK(a.code->shown() == kept);
    CHECK(a.h.find("c.cpp", Role::Tab) != nullptr);
    // One buffer for b.cpp in both groups.
    CHECK(a.files->workspace().files().size() == 3);
    CHECK_FALSE(a.files->unsaved(a.at("b.cpp")));
}

TEST_CASE("Files gone since are left out of what is shown again, and a group left with none goes", "[editor][area]") {
    Area a({{"a.cpp", "int a;\n"}});
    a.code->show(std::vector<GroupFiles>{{.files = {a.folder.path / "gone.cpp"}, .front = 0},
                                         {.files = {a.folder.path / "gone.cpp", a.folder.path / "a.cpp"}, .front = 0}});
    a.h.step(2);
    // Its front gone, the first file left is in front.
    CHECK(a.code->shown() == std::vector<GroupFiles>{{.files = {a.at("a.cpp")}, .front = 0}});
    CHECK(a.files->note().empty());

    // None left at all: the one group with no file stays.
    Area b({});
    b.code->show(std::vector<GroupFiles>{{.files = {b.folder.path / "gone.cpp"}, .front = 0}});
    b.h.step(2);
    CHECK(b.code->shown() == std::vector<GroupFiles>{{.files = {}, .front = 0}});
}

TEST_CASE("An area that shows files keeps them when shown others", "[editor][area]") {
    Area a({{"a.cpp", "int a;\n"}, {"b.cpp", "int b;\n"}});
    a.open("a.cpp");
    a.code->show(std::vector<GroupFiles>{{.files = {a.folder.path / "b.cpp"}, .front = 0}});
    a.h.step(2);
    CHECK(a.code->shown() == std::vector<GroupFiles>{{.files = {a.at("a.cpp")}, .front = 0}});
    CHECK(a.files->workspace().find(a.at("b.cpp")) == nullptr);
}

TEST_CASE("The code takes the keys it binds from its host, and leaves it the others", "[editor][area]") {
    Area a({{"a.cpp", "int a;\n"}});
    a.open("a.cpp");
    const auto ctrl = [](Key key, bool shift = false) {
        return Chord{.key = key, .modifiers = {.ctrl = true, .shift = shift}};
    };
    // The area's keys, the view's, and those of the program it is in.
    CHECK(a.code->binds(ctrl(Key::S)));
    CHECK(a.code->binds(ctrl(Key::Z)));
    CHECK(a.code->binds(ctrl(Key::I)));
    CHECK(a.code->binds(Chord{.key = Key::F5}));
    // The rest are the host's: its file tree, its settings, play, a capture.
    CHECK_FALSE(a.code->binds(ctrl(Key::Space)));
    CHECK_FALSE(a.code->binds(ctrl(Key::Comma)));
    CHECK_FALSE(a.code->binds(ctrl(Key::P)));
    CHECK_FALSE(a.code->binds(Chord{.key = Key::F9}));

    // With Vim on, Vim reads Ctrl with a letter as well, but not the F keys,
    // Ctrl with Shift, or Ctrl with a key that is no letter.
    a.preferences.vim = true;
    a.h.step(2);
    CHECK(a.code->binds(ctrl(Key::P)));
    CHECK(a.code->binds(ctrl(Key::S)));
    CHECK(a.code->binds(Chord{.key = Key::J}));
    CHECK_FALSE(a.code->binds(ctrl(Key::Space)));
    CHECK_FALSE(a.code->binds(ctrl(Key::S, true)));
    CHECK_FALSE(a.code->binds(Chord{.key = Key::F9}));
}

TEST_CASE("The area says while a view of its has the keyboard", "[editor][area]") {
    Area a({{"a.cpp", "int a;\n"}});
    CHECK_FALSE(a.code->has_keyboard());
    a.open("a.cpp");
    CHECK(a.code->has_keyboard());
    // Taken away, as a press in another of the host's panels takes it.
    a.h.ui().set_focus(0);
    a.h.step(2);
    CHECK_FALSE(a.code->has_keyboard());
}
