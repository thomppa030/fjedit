#include "fjedit/editor/code_area.hpp"

#include "temp_folder.hpp"

#include "ftk/test/kit_harness.hpp"
#include "ftk/ui/kit/viewport_toolbar.hpp"
#include "ftk/ui/kit/window.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace fjedit;
using ftk::ui::Key;
using ftk::ui::core::Role;

namespace {

constexpr ftk::ui::Modifiers CTRL_SHIFT{.ctrl = true, .shift = true};

// A code area on a folder of the test's own, alone in a window as a tab of
// fjell's holds it: no program of its own around it, one command of the
// program it is in ("game.play", on F5), and find in files floating over the
// whole window.
struct Area {
    TempFolder folder;
    Preferences preferences;
    std::unique_ptr<CodeFiles> files;
    std::unique_ptr<CodeArea> code;
    ftk::test::KitHarness h{{1280.0f, 800.0f}};
    // What the area asked of the program it is in.
    std::vector<std::string> ran;
    int left{0};

    explicit Area(std::initializer_list<std::pair<const char*, const char*>> written,
                  const std::function<void(const TempFolder&)>& before = {}) {
        for (const auto& [name, bytes] : written) folder.write(name, bytes);
        if (before) before(folder);
        files = std::make_unique<CodeFiles>(
            folder.path, preferences,
            CodeFilesOptions{.recovery = folder.path / ".recovery", .services = std::vector<ServiceMaker>{}});
        code = std::make_unique<CodeArea>(
            *files, preferences,
            CodeAreaOptions{.keys = folder.path / ".config" / "keys.vim",
                            .commands = {{.name = "game.play", .title = "Play", .key = Chord{.key = Key::F5}}},
                            .run = [this](std::string_view command) {
                                ran.emplace_back(command);
                                return true;
                            },
                            .leave = [this] { ++left; },
                            .program_name = "fjell"});
        h.set_ui([this] {
            code->take_keys();
            ftk::ui::next_window_place({0.0f, 0.0f}, {1280.0f, 800.0f});
            (void)ftk::ui::begin_host_window("##code", {.scrollbar = false, .to_front = false});
            code->draw();
            code->draw_find_in_files({.min = {0.0f, 0.0f}, .max = {1280.0f, 800.0f}});
            ftk::ui::end_host_window();
            code->draw_prompts();
        });
        h.step(3);
    }

    [[nodiscard]] std::filesystem::path at(std::string_view name) const {
        return files->workspace().name(folder.path / name);
    }

    // Vim's `:` line, typed and entered.
    void ex(std::string_view line) {
        h.type(std::format(":{}", line));
        h.press(Key::Enter);
        h.step(2);
    }
};

} // namespace

TEST_CASE("The code area opens a file in a tab in front, and goes to a place in it", "[editor][area]") {
    Area a({{"a.cpp", "int a;\nint speed;\n"}});
    CHECK_FALSE(a.code->front().has_value());
    a.code->open(a.folder.path / "a.cpp");
    a.h.step(2);
    REQUIRE(a.code->front() == a.at("a.cpp"));
    CHECK(a.h.find("a.cpp", Role::Tab) != nullptr);

    a.code->go_to(a.folder.path / "a.cpp", {{1, 4}, {1, 9}});
    a.h.step(2);
    REQUIRE(a.code->front_view() != nullptr);
    const Selection& selected = a.code->front_view()->selections.primary();
    CHECK(selected.anchor == Position{1, 4});
    CHECK(selected.caret == Position{1, 9});
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
    a.code->open(a.folder.path / "a.txt");
    a.h.step(2);
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
    a.code->open(a.folder.path / "a.cpp");
    a.h.step(2);
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
