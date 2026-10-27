#include "fjedit/editor/find_in_files.hpp"

#include "temp_folder.hpp"

#include "ftk/base/thread_pool.hpp"
#include "ftk/test/kit_harness.hpp"
#include "ftk/ui/kit/viewport_toolbar.hpp"
#include "ftk/ui/kit/window.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace fjedit;
using ftk::ui::Key;

namespace {

// Find in files over a folder of the test's own, floating over the whole of
// its window as a program that holds no code view of its own floats it.
struct Finding {
    TempFolder folder;
    Workspace workspace;
    ftk::ThreadPool pool;
    FindInFiles find{workspace, pool};
    Keymap keys = Keymap::standard();
    ftk::test::KitHarness h{{1280.0f, 800.0f}};
    // What it asked for, all frames together.
    std::optional<FileHit> went{};
    std::set<std::filesystem::path> let_go{};

    explicit Finding(std::initializer_list<std::pair<const char*, const char*>> files)
        : workspace(folder.path) {
        for (const auto& [name, bytes] : files) folder.write(name, bytes);
        h.set_ui([this] {
            ftk::ui::next_window_place({0.0f, 0.0f}, {1280.0f, 800.0f});
            (void)ftk::ui::begin_host_window("##window", {.scrollbar = false, .to_front = false});
            const FindInFilesAsk asked =
                find.draw({.min = {0.0f, 0.0f}, .max = {1280.0f, 800.0f}}, workspace.root(), keys, 13.0f);
            if (asked.go) went = asked.go;
            let_go.insert(asked.shown.begin(), asked.shown.end());
            ftk::ui::end_host_window();
        });
        h.step(2);
    }

    // Steps until it has its matches, or long enough that it won't.
    void wait_for_hits() {
        for (int i = 0; i < 400 && find.hits().empty(); ++i) h.step();
    }
};

} // namespace

TEST_CASE("Find in files opened on a word looks for it in every file, and Enter asks for the first match",
          "[editor][find_in_files]") {
    Finding f({{"a.cpp", "float spin_speed;\n"}, {"b.cpp", "x = spin_speed;\n"}, {"c.txt", "nothing\n"}});
    f.find.open("spin_speed");
    REQUIRE(f.find.is_open());
    f.wait_for_hits();
    REQUIRE(f.find.hits().size() == 2);

    f.h.press(Key::Enter);
    f.h.step(2);
    CHECK_FALSE(f.find.is_open());
    REQUIRE(f.went.has_value());
    CHECK(f.went->path == f.workspace.name(f.folder.path / "a.cpp"));
    CHECK(f.went->span.from == Position{0, 6});
}

TEST_CASE("Find in files closed hands back the files it opened only to show them", "[editor][find_in_files]") {
    Finding f({{"a.cpp", "float spin_speed;\n"}});
    f.find.open("spin_speed");
    f.wait_for_hits();
    // The first match is shown, its file opened to show it.
    REQUIRE(f.workspace.files().size() == 1);
    f.h.press(Key::Escape);
    f.h.step();
    CHECK_FALSE(f.find.is_open());
    CHECK_FALSE(f.went.has_value());
    CHECK(f.let_go == std::set<std::filesystem::path>{f.workspace.name(f.folder.path / "a.cpp")});
}

TEST_CASE("Find in files leaves out of what it hands back a file that was open before it", "[editor][find_in_files]") {
    Finding f({{"a.cpp", "float spin_speed;\n"}});
    REQUIRE(f.workspace.open(f.folder.path / "a.cpp"));
    f.find.open("spin_speed");
    f.wait_for_hits();
    f.h.press(Key::Escape);
    f.h.step();
    CHECK(f.let_go.empty());
    CHECK(f.workspace.files().size() == 1);
}
