#include "fjedit/program/program.hpp"

#include "print.hpp"
#include "temp_folder.hpp"

#include "ftk/test/kit_harness.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace fjedit;
using ftk::ui::Key;
using ftk::ui::core::Role;

namespace {

constexpr ftk::ui::Modifiers CTRL{.ctrl = true};

// The program on a folder of a test's own, in the kit's harness.
struct Running {
    TempFolder folder;
    std::unique_ptr<Program> program;
    ftk::test::KitHarness h{{1280.0f, 800.0f}};

    explicit Running(std::initializer_list<std::pair<const char*, const char*>> files) {
        for (const auto& [name, bytes] : files) folder.write(name, bytes);
        program = std::make_unique<Program>(folder.path);
        h.set_ui([this] { program->draw(); });
        h.step(3);
    }

    // The names of the tree's rows as the last frame drew them, in order.
    [[nodiscard]] std::vector<std::string> rows() {
        std::vector<std::string> out;
        for (const auto& node : h.ui().semantics()) {
            if (node.role == Role::TreeItem) out.emplace_back(node.label);
        }
        return out;
    }

    // A press on the tree's row for `name`.
    void press_row(std::string_view name) {
        const auto* row = h.find(name, Role::TreeItem);
        REQUIRE(row != nullptr);
        h.click_at(row->rect.centre());
        h.step(2);
    }

    [[nodiscard]] const Document& document(std::string_view name) const {
        const Document* found = program->workspace().find(folder.path / name);
        REQUIRE(found != nullptr);
        return *found;
    }
};

} // namespace

TEST_CASE("The tree shows the folder: folders first, each by name, hidden ones left out", "[program]") {
    Running run({{"b.cpp", ""}, {"A.txt", ""}, {".hidden", ""}, {"scripts/coin.cpp", ""}});
    const std::vector<std::string> rows = run.rows();
    REQUIRE(rows.size() == 4);
    REQUIRE(rows[0] == run.folder.path.filename().string());
    REQUIRE(rows[1] == "scripts");
    REQUIRE(rows[2] == "A.txt");
    REQUIRE(rows[3] == "b.cpp");

    // A folder opens to show what is in it.
    run.press_row("scripts");
    run.h.step();
    REQUIRE(run.rows().size() == 5);
}

TEST_CASE("A file pressed in the tree opens in a tab in front, with the keyboard", "[program]") {
    Running run({{"coin.cpp", "int a;\n"}, {"toon.fjsl", "void fragment() {}\n"}});
    run.press_row("coin.cpp");
    REQUIRE(run.program->front() == run.program->workspace().name(run.folder.path / "coin.cpp"));
    REQUIRE(run.h.find("coin.cpp", Role::Tab) != nullptr);
    run.h.type("x");
    REQUIRE(run.document("coin.cpp").buffer().text() == "xint a;\n");
    REQUIRE(run.program->workspace().unsaved(run.folder.path / "coin.cpp"));

    // A second file, then back to the first by its tab.
    run.press_row("toon.fjsl");
    REQUIRE(run.program->front() == run.program->workspace().name(run.folder.path / "toon.fjsl"));
    run.h.click_at(run.h.find("coin.cpp", Role::Tab)->rect.centre());
    run.h.step();
    REQUIRE(run.program->front() == run.program->workspace().name(run.folder.path / "coin.cpp"));
}

TEST_CASE("Ctrl S saves the file in front", "[program]") {
    Running run({{"coin.cpp", "int a;\n"}});
    run.press_row("coin.cpp");
    run.h.type("x");
    run.h.press(Key::S, CTRL);
    REQUIRE(run.folder.read("coin.cpp") == "xint a;\n");
    REQUIRE_FALSE(run.program->workspace().unsaved(run.folder.path / "coin.cpp"));
}

TEST_CASE("Closing a tab with unsaved changes asks first", "[program]") {
    Running run({{"coin.cpp", "int a;\n"}});
    run.press_row("coin.cpp");
    run.h.type("x");
    run.h.press(Key::W, CTRL);
    run.h.step(2);
    // Still open while it asks.
    REQUIRE(run.program->front().has_value());
    const auto* dont_save = run.h.find("Don't save", Role::Button);
    REQUIRE(dont_save != nullptr);
    run.h.click_at(dont_save->rect.centre());
    run.h.step(2);
    REQUIRE_FALSE(run.program->front().has_value());
    REQUIRE(run.program->workspace().files().empty());
    REQUIRE(run.folder.read("coin.cpp") == "int a;\n");
}

TEST_CASE("Closing a saved tab asks nothing", "[program]") {
    Running run({{"coin.cpp", "int a;\n"}});
    run.press_row("coin.cpp");
    run.h.press(Key::W, CTRL);
    run.h.step();
    REQUIRE_FALSE(run.program->front().has_value());
}

TEST_CASE("A file that isn't text opens nothing", "[program]") {
    Running run({{"blob.bin", "a"}});
    run.folder.write("blob.bin", std::string("a\0b", 3));
    run.press_row("blob.bin");
    REQUIRE_FALSE(run.program->front().has_value());
    REQUIRE(run.program->workspace().files().empty());
}

TEST_CASE("The menus' entries run on the view in front", "[program]") {
    Running run({{"coin.cpp", "int a;\n"}});
    run.press_row("coin.cpp");
    run.h.type("x");
    REQUIRE(run.program->run("edit.undo"));
    run.h.step();
    REQUIRE(run.document("coin.cpp").buffer().text() == "int a;\n");
    REQUIRE_FALSE(run.program->run("no.such_command"));
}
