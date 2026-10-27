#include "fjedit/editor/file_tree.hpp"

#include "temp_folder.hpp"

#include "ftk/test/kit_harness.hpp"
#include "ftk/ui/kit/viewport_toolbar.hpp"
#include "ftk/ui/kit/window.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace fjedit;
using ftk::ui::core::Role;

namespace {

// A tree of a folder of the test's own, filling a window as a panel would.
struct Tree {
    TempFolder folder;
    FileTree tree;
    std::filesystem::path selected{};
    // The file pressed in the last frame that pressed one.
    std::optional<std::filesystem::path> pressed{};
    ftk::test::KitHarness h{{400.0f, 600.0f}};

    explicit Tree(std::initializer_list<const char*> files) : tree(folder.path) {
        for (const char* name : files) folder.write(name, "");
        h.set_ui([this] {
            ftk::ui::next_window_place({0.0f, 0.0f}, {400.0f, 600.0f});
            (void)ftk::ui::begin_host_window("##files", {.scrollbar = false, .to_front = false});
            if (const std::optional<std::filesystem::path> file = tree.draw(selected)) pressed = file;
            ftk::ui::end_host_window();
        });
        h.step(2);
    }

    // The names of its rows as the last frame drew them, in order.
    [[nodiscard]] std::vector<std::string> rows() {
        std::vector<std::string> out;
        for (const auto& node : h.ui().semantics()) {
            if (node.role == Role::TreeItem) out.emplace_back(node.label);
        }
        return out;
    }

    void press_row(std::string_view name) {
        const auto* row = h.find(name, Role::TreeItem);
        REQUIRE(row != nullptr);
        h.click_at(row->rect.centre());
        h.step(2);
    }
};

} // namespace

TEST_CASE("A folder is read folders first, each by name as people read them, hidden ones only when asked",
          "[editor][tree]") {
    const TempFolder folder;
    folder.write("b.cpp", "");
    folder.write("A.txt", "");
    folder.write(".hidden", "");
    folder.write("scripts/coin.cpp", "");
    std::vector<std::string> names;
    for (const FolderEntry& entry : read_folder(folder.path)) names.push_back(entry.name);
    CHECK(names == std::vector<std::string>{"scripts", "A.txt", "b.cpp"});

    names.clear();
    for (const FolderEntry& entry : read_folder(folder.path, true)) names.push_back(entry.name);
    CHECK(names == std::vector<std::string>{"scripts", ".hidden", "A.txt", "b.cpp"});
    // One that can't be read has nothing in it.
    CHECK(read_folder(folder.path / "gone").empty());
}

TEST_CASE("The tree shows its folder open; a folder opens on a press, and a file pressed is handed back",
          "[editor][tree]") {
    Tree t({"b.cpp", "scripts/coin.cpp"});
    CHECK(t.rows() == std::vector<std::string>{t.folder.path.filename().string(), "scripts", "b.cpp"});
    t.press_row("scripts");
    CHECK(t.rows() == std::vector<std::string>{t.folder.path.filename().string(), "scripts", "coin.cpp", "b.cpp"});
    CHECK_FALSE(t.pressed.has_value());

    t.press_row("coin.cpp");
    REQUIRE(t.pressed.has_value());
    CHECK(*t.pressed == t.folder.path / "scripts" / "coin.cpp");
}

TEST_CASE("The tree reads a folder again only once told to forget what it read", "[editor][tree]") {
    Tree t({"b.cpp"});
    t.folder.write("c.cpp", "");
    t.h.step(2);
    CHECK(t.rows().size() == 2);
    t.tree.forget_listings();
    t.h.step(2);
    CHECK(t.rows() == std::vector<std::string>{t.folder.path.filename().string(), "b.cpp", "c.cpp"});
}
