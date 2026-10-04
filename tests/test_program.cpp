#include "fjedit/program/program.hpp"

#include "print.hpp"
#include "temp_folder.hpp"

#include "ftk/test/kit_harness.hpp"
#include "ftk/ui/kit/icons.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <functional>
#include <fstream>
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

    explicit Running(std::initializer_list<std::pair<const char*, const char*>> files,
                     const std::function<void(const TempFolder&)>& before = {}) {
        for (const auto& [name, bytes] : files) folder.write(name, bytes);
        if (before) before(folder);
        program = std::make_unique<Program>(folder.path, ProgramOptions{.recovery = recovery_folder()});
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

    // Where the program keeps unsaved work: in the test's own folder, where
    // the tree doesn't show it.
    [[nodiscard]] std::filesystem::path recovery_folder() const { return folder.path / ".recovery"; }
    [[nodiscard]] std::vector<Recovery::Kept> kept() const { return Recovery(recovery_folder()).kept(); }

    // A press on the button called `label`, as the last frame drew it.
    void press(std::string_view label) {
        h.step();
        const auto* button = h.find(label, Role::Button);
        REQUIRE(button != nullptr);
        h.click_at(button->rect.centre());
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

namespace {

// Writes `bytes` over `file` and dates it a little later, as another
// program writing it a moment after would, whatever the file system's clock
// steps.
void write_later(const std::filesystem::path& file, std::string_view bytes) {
    const auto before = std::filesystem::last_write_time(file);
    std::ofstream(file, std::ios::binary | std::ios::trunc).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    std::filesystem::last_write_time(file, before + std::chrono::seconds(2));
}

// Long enough for the program to look at the disk again.
constexpr int DISK_FRAMES = 150;

} // namespace

TEST_CASE("A file changed elsewhere with nothing unsaved here comes in as it is", "[program][disk]") {
    Running run({{"coin.cpp", "int a;\n"}});
    run.press_row("coin.cpp");
    write_later(run.folder.path / "coin.cpp", "int b;\n");
    run.h.step(DISK_FRAMES);
    REQUIRE(run.document("coin.cpp").buffer().text() == "int b;\n");
    REQUIRE_FALSE(run.program->workspace().unsaved(run.folder.path / "coin.cpp"));
}

TEST_CASE("A file changed elsewhere with unsaved changes here asks, and Load from disk takes it", "[program][disk]") {
    Running run({{"coin.cpp", "int a;\n"}});
    run.press_row("coin.cpp");
    run.h.type("x");
    write_later(run.folder.path / "coin.cpp", "int b;\n");
    run.h.step(DISK_FRAMES);
    REQUIRE(run.document("coin.cpp").buffer().text() == "xint a;\n");
    const auto* load = run.h.find("Load from disk", Role::Button);
    REQUIRE(load != nullptr);
    run.h.click_at(load->rect.centre());
    run.h.step();
    REQUIRE(run.document("coin.cpp").buffer().text() == "int b;\n");
    REQUIRE(run.h.find("Load from disk", Role::Button) == nullptr);
}

TEST_CASE("Keep mine leaves the changes here as they are", "[program][disk]") {
    Running run({{"coin.cpp", "int a;\n"}});
    run.press_row("coin.cpp");
    run.h.type("x");
    write_later(run.folder.path / "coin.cpp", "int b;\n");
    run.h.step(DISK_FRAMES);
    run.h.click_at(run.h.find("Keep mine", Role::Button)->rect.centre());
    run.h.step(DISK_FRAMES);
    REQUIRE(run.document("coin.cpp").buffer().text() == "xint a;\n");
    REQUIRE(run.h.find("Keep mine", Role::Button) == nullptr);
}

TEST_CASE("A file made elsewhere shows in the tree", "[program][disk]") {
    Running run({{"coin.cpp", ""}});
    run.folder.write("rotator.cpp", "");
    run.h.step(DISK_FRAMES);
    REQUIRE(run.h.find("rotator.cpp", Role::TreeItem) != nullptr);
}

TEST_CASE("Unsaved work is kept every few seconds, and forgotten once saved", "[program][recovery]") {
    Running run({{"coin.cpp", "int a;\n"}});
    run.press_row("coin.cpp");
    run.h.type("x");
    run.h.step(DISK_FRAMES);
    const std::vector<Recovery::Kept> kept = run.kept();
    REQUIRE(kept.size() == 1);
    REQUIRE(kept[0].bytes == "xint a;\n");
    REQUIRE(kept[0].path == run.program->workspace().name(run.folder.path / "coin.cpp"));

    run.h.press(Key::S, CTRL);
    run.h.step();
    REQUIRE(run.kept().empty());
}

TEST_CASE("Work kept from before is offered at the start and comes back unsaved", "[program][recovery]") {
    Running run({{"coin.cpp", "int a;\n"}}, [](const TempFolder& folder) {
        Workspace named(folder.path);
        REQUIRE(Recovery(folder.path / ".recovery").keep(named.name("coin.cpp"), "int kept;\n").has_value());
    });
    run.press("Recover 1 file");
    REQUIRE(run.program->front() == run.program->workspace().name(run.folder.path / "coin.cpp"));
    REQUIRE(run.document("coin.cpp").buffer().text() == "int kept;\n");
    REQUIRE(run.program->workspace().unsaved(run.folder.path / "coin.cpp"));
    REQUIRE(run.folder.read("coin.cpp") == "int a;\n");
}

TEST_CASE("Discard lets the work kept from before go", "[program][recovery]") {
    Running run({{"coin.cpp", "int a;\n"}}, [](const TempFolder& folder) {
        Workspace named(folder.path);
        REQUIRE(Recovery(folder.path / ".recovery").keep(named.name("coin.cpp"), "int kept;\n").has_value());
    });
    run.press("Discard");
    REQUIRE(run.kept().empty());
    REQUIRE_FALSE(run.program->front().has_value());
}

TEST_CASE("Quitting with nothing unsaved goes at once", "[program][quit]") {
    Running run({{"coin.cpp", "int a;\n"}});
    run.press_row("coin.cpp");
    REQUIRE(run.program->ask_to_quit());
    REQUIRE(run.program->quitting());
}

TEST_CASE("Quitting with unsaved files asks: Cancel stays, Don't save goes without them", "[program][quit]") {
    Running run({{"coin.cpp", "int a;\n"}});
    run.press_row("coin.cpp");
    run.h.type("x");
    run.h.step(DISK_FRAMES);
    REQUIRE_FALSE(run.program->ask_to_quit());
    run.press("Cancel");
    REQUIRE_FALSE(run.program->quitting());

    REQUIRE_FALSE(run.program->ask_to_quit());
    run.press("Don't save");
    REQUIRE(run.program->quitting());
    REQUIRE(run.folder.read("coin.cpp") == "int a;\n");
    REQUIRE(run.kept().empty());
}

TEST_CASE("Save and quit saves the files that stay ticked", "[program][quit]") {
    Running run({{"coin.cpp", "int a;\n"}});
    run.press_row("coin.cpp");
    run.h.type("x");
    REQUIRE_FALSE(run.program->ask_to_quit());
    // The save button leads with its icon.
    run.press(std::string(ftk::ui::icon::save) + "  Save 1 and quit");
    REQUIRE(run.program->quitting());
    REQUIRE(run.folder.read("coin.cpp") == "xint a;\n");
}

TEST_CASE("Splitting opens the file in front again beside it, one document between them", "[program][split]") {
    Running run({{"coin.cpp", "int a;\n"}});
    run.press_row("coin.cpp");
    run.h.press(Key::Backslash, CTRL);
    run.h.step(2);
    REQUIRE(run.program->groups() == 2);
    std::size_t tabs = 0;
    for (const auto& node : run.h.ui().semantics()) tabs += node.role == Role::Tab && node.label == "coin.cpp";
    REQUIRE(tabs == 2);

    // The new group has the keyboard; what is typed there is the file's.
    run.h.type("x");
    REQUIRE(run.document("coin.cpp").buffer().text() == "xint a;\n");
    REQUIRE(run.program->workspace().files().size() == 1);

    // Closing the split's only tab closes the split, and asks nothing: the
    // other tab still shows the changes.
    run.h.press(Key::W, CTRL);
    run.h.step(2);
    REQUIRE(run.program->groups() == 1);
    REQUIRE(run.program->workspace().unsaved(run.folder.path / "coin.cpp"));
}

namespace {

constexpr ftk::ui::Modifiers CTRL_SHIFT{.ctrl = true, .shift = true};

// Steps until find in files has its matches, or long enough that it won't.
void wait_for_hits(Running& run) {
    for (int i = 0; i < 400 && run.program->find_in_files().hits().empty(); ++i) run.h.step();
}

} // namespace

TEST_CASE("Find in files finds in every file, and Enter opens the first at its match", "[program][find_in_files]") {
    Running run({{"a.cpp", "float spin_speed;\n"}, {"b.cpp", "x = spin_speed;\n"}, {"c.txt", "nothing\n"}});
    run.h.press(Key::F, CTRL_SHIFT);
    REQUIRE(run.program->find_in_files().is_open());
    run.h.type("spin_speed");
    wait_for_hits(run);
    REQUIRE(run.program->find_in_files().hits().size() == 2);
    REQUIRE(run.h.find("float spin_speed;", Role::TreeItem) != nullptr);

    run.h.press(Key::Enter);
    run.h.step(2);
    REQUIRE_FALSE(run.program->find_in_files().is_open());
    REQUIRE(run.program->front() == run.program->workspace().name(run.folder.path / "a.cpp"));
}

TEST_CASE("Find in files searches open files with their unsaved changes", "[program][find_in_files]") {
    Running run({{"a.cpp", "int a;\n"}});
    run.press_row("a.cpp");
    run.h.type("spin_speed ");
    run.h.press(Key::F, CTRL_SHIFT);
    run.h.type("spin_speed");
    wait_for_hits(run);
    REQUIRE(run.program->find_in_files().hits().size() == 1);
}

TEST_CASE("Replace in every file changes open files in place and writes the rest", "[program][find_in_files]") {
    Running run({{"a.cpp", "float spin_speed;\n"}, {"b.cpp", "x = spin_speed;\n"}});
    run.press_row("a.cpp");
    run.h.press(Key::F, CTRL_SHIFT);
    run.h.type("spin_speed");
    wait_for_hits(run);
    // On to the replacement, and in every file.
    run.h.press(Key::Tab);
    run.h.type("rpm");
    run.press(ftk::ui::icon::replace);
    REQUIRE(run.document("a.cpp").buffer().text() == "float rpm;\n");
    REQUIRE(run.program->workspace().unsaved(run.folder.path / "a.cpp"));
    REQUIRE(run.folder.read("b.cpp") == "x = rpm;\n");
}

TEST_CASE("Escape closes find in files, letting go of the files it only showed", "[program][find_in_files]") {
    Running run({{"a.cpp", "float spin_speed;\n"}});
    run.h.press(Key::F, CTRL_SHIFT);
    run.h.type("spin_speed");
    wait_for_hits(run);
    REQUIRE(run.program->workspace().files().size() == 1);
    run.h.press(Key::Escape);
    run.h.step();
    REQUIRE_FALSE(run.program->find_in_files().is_open());
    REQUIRE(run.program->workspace().files().empty());
}
