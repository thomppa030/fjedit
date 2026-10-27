#include "fjedit/program/program.hpp"
#include "fjedit/program/preferences_window.hpp"

#include "fjedit/core/vim/notation.hpp"

#include "fake_language.hpp"
#include "print.hpp"
#include "temp_folder.hpp"

#include "ftk/app/desktop_entry.hpp"
#include "ftk/app/picture.hpp"
#include "ftk/platform/child_process.hpp"
#include "ftk/test/kit_harness.hpp"
#include "ftk/ui/theme.hpp"
#include "ftk/ui/kit/icons.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>
#include <functional>
#include <format>
#include <fstream>
#include <set>
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
    Preferences preferences;
    std::unique_ptr<Program> program;
    ftk::test::KitHarness h{{1280.0f, 800.0f}};

    // The language service made for C++ files, when the test gives one,
    // and how many have been made; and the one beside it for script headers
    // and fjell's files, when the test gives both.
    FakeLanguage* language{nullptr};
    int languages_made{0};
    FakeLanguage* fjls{nullptr};
    int fjls_made{0};
    // How many times the program asked where another folder is.
    int folders_picked{0};

    // What answers for C++: nothing, so no test runs a server it didn't ask
    // for; a fake; a fake and a second fake for headers and fjell's files, as
    // clangd and fjls; or clangd, as the program runs it.
    enum class Languages { None, Fake, Both, Clangd };

    explicit Running(std::initializer_list<std::pair<const char*, const char*>> files,
                     const std::function<void(const TempFolder&)>& before = {}, Languages languages = Languages::None,
                     const std::function<void(ProgramOptions&, const TempFolder&)>& more = {}) {
        for (const auto& [name, bytes] : files) folder.write(name, bytes);
        if (before) before(folder);
        std::vector<ServiceMaker> services;
        if (languages == Languages::Fake || languages == Languages::Both) {
            // Named as clangd, so the preferences' path for it holds.
            services.push_back({.name = "clangd",
                                .extensions = {"cpp", "hpp"},
                                .make = [this](const std::filesystem::path&) -> std::unique_ptr<LanguageService> {
                                    auto made = std::make_unique<FakeLanguage>();
                                    language = made.get();
                                    ++languages_made;
                                    return made;
                                }});
        }
        if (languages == Languages::Both) {
            // Named as fjls, so the preferences' path for it holds.
            services.push_back({.name = "fjls",
                                .extensions = {"hpp", "fjss"},
                                .make = [this](const std::filesystem::path&) -> std::unique_ptr<LanguageService> {
                                    auto made = std::make_unique<FakeLanguage>();
                                    fjls = made.get();
                                    ++fjls_made;
                                    return made;
                                }});
        }
        ProgramOptions options{.recovery = recovery_folder(),
                               .services = languages == Languages::Clangd ? std::nullopt : std::optional(std::move(services)),
                               .wake = {},
                               .keys = keys_file(),
                               .pick_folder = [this] { ++folders_picked; },
                               .brand = FJEDIT_BRAND};
        if (more) more(options, folder);
        program = std::make_unique<Program>(folder.path, preferences, std::move(options));
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

    // keys.vim, read when a test writes it: in the test's own folder, where
    // the tree doesn't show it.
    [[nodiscard]] std::filesystem::path keys_file() const { return folder.path / ".config" / "keys.vim"; }

    // Where the program keeps unsaved work: in the test's own folder, where
    // the tree doesn't show it.
    [[nodiscard]] std::filesystem::path recovery_folder() const { return folder.path / ".recovery"; }
    [[nodiscard]] std::vector<Recovery::Kept> kept() const { return Recovery(recovery_folder()).kept(); }

    // Opens the menu bar's `menu`: then the entry called `entry` in it, as the
    // last frame drew it, or nothing when there is none. The kit records a
    // menu's entries as list items, and the menus on the bar as menu items.
    [[nodiscard]] const ftk::ui::core::SemanticNode* menu_entry(std::string_view menu, std::string_view entry) {
        const auto* top = h.find(menu, Role::MenuItem);
        REQUIRE(top != nullptr);
        h.click_at(top->rect.centre());
        h.step(2);
        return h.find(entry, Role::ListItem);
    }

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

TEST_CASE("A folder in the tree is drawn in Structure's grey, open or not, never in the accent", "[program]") {
    Running run({{"scripts/coin.cpp", ""}});
    // Whether the last frame drew anything in `colour` on the row named `name`.
    const auto on_row = [&](std::string_view name, ftk::ui::Color colour) {
        const auto* row = run.h.find(name, Role::TreeItem);
        REQUIRE(row != nullptr);
        const uint32_t packed = ftk::ui::core::pack_colour(colour);
        for (const ftk::ui::core::RootOutput& root : run.h.output().roots) {
            for (const ftk::ui::core::Primitive& p : root.draw->primitives) {
                if (p.colour == packed && row->rect.contains(p.min)) return true;
            }
        }
        return false;
    };
    const ftk::ui::Color grey = ftk::theme::hue(ftk::theme::Hue::grey);
    CHECK(on_row("scripts", grey));
    CHECK_FALSE(on_row("scripts", ftk::theme::accent()));
    run.press_row("scripts");
    run.h.step();
    REQUIRE(run.rows().size() == 3);
    CHECK(on_row("scripts", grey));
    CHECK_FALSE(on_row("scripts", ftk::theme::accent()));
}

TEST_CASE("fjedit's brand files read, its mark and its tile square", "[program][brand]") {
    for (const char* name : {"fjedit-mark-dark.svg", "fjedit-tile.svg"}) {
        const ftk::Result<ftk::SvgPicture> picture = ftk::SvgPicture::load(std::filesystem::path(FJEDIT_BRAND) / name);
        REQUIRE(picture);
        CHECK(picture->size(64.0f) == glm::vec2(64.0f, 64.0f));
    }
}

TEST_CASE("A group with no file open shows fjedit's logomark where the kit's file icon was", "[program][brand]") {
    Running run({{"coin.cpp", "int a;\n"}});
    // How much the last frame drew in `colour` in the group: right of the
    // tree, from its first row down to the status bar.
    const ftk::ui::core::Rect row = run.h.find("coin.cpp", Role::TreeItem)->rect;
    const auto in_group = [&](ftk::ui::Color colour) {
        const uint32_t packed = ftk::ui::core::pack_colour(colour);
        int drawn = 0;
        for (const ftk::ui::core::RootOutput& root : run.h.output().roots) {
            for (const ftk::ui::core::Primitive& p : root.draw->primitives) {
                const glm::vec2 at = (p.min + p.max) * 0.5f;
                if (p.colour == packed && at.x > row.max.x && at.y > row.min.y &&
                    at.y < run.h.size().y - ftk::theme::STATUS_BAR) {
                    ++drawn;
                }
            }
        }
        return drawn;
    };
    // Its words, and over them no icon in the disabled colour: the mark's
    // room, which a harness with no device leaves empty.
    CHECK(in_group(ftk::theme::text_secondary()) > 0);
    CHECK(in_group(ftk::theme::text_disabled()) == 0);
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

TEST_CASE("Open folder asks the window where the folder is", "[program][folder]") {
    Running run({{"coin.cpp", "int a;\n"}});
    REQUIRE(run.program->run("file.open_folder"));
    CHECK(run.folders_picked == 1);
    CHECK_FALSE(run.program->folder_wanted());
}

TEST_CASE("Another folder with nothing unsaved here is wanted at once", "[program][folder]") {
    Running run({{"coin.cpp", "int a;\n"}});
    run.press_row("coin.cpp");
    const std::filesystem::path other = run.folder.path / "other";
    run.program->open_folder(other);
    REQUIRE(run.program->folder_wanted() == other);
    CHECK_FALSE(run.program->quitting());
}

TEST_CASE("Another folder with unsaved files here asks first: Cancel stays, Don't save leaves for it",
          "[program][folder]") {
    Running run({{"coin.cpp", "int a;\n"}});
    run.press_row("coin.cpp");
    run.h.type("x");
    run.h.step(DISK_FRAMES);
    const std::filesystem::path other = run.folder.path / "other";
    run.program->open_folder(other);
    CHECK_FALSE(run.program->folder_wanted());
    run.press("Cancel");
    CHECK_FALSE(run.program->folder_wanted());

    run.program->open_folder(other);
    run.press("Don't save");
    REQUIRE(run.program->folder_wanted() == other);
    CHECK_FALSE(run.program->quitting());
    CHECK(run.folder.read("coin.cpp") == "int a;\n");
    CHECK(run.kept().empty());
}

TEST_CASE("Leaving for another folder saves the files that stay ticked, the folder open closing", "[program][folder]") {
    Running run({{"coin.cpp", "int a;\n"}});
    run.press_row("coin.cpp");
    run.h.type("x");
    const std::filesystem::path other = run.folder.path / "other";
    run.program->open_folder(other);
    run.press(std::string(ftk::ui::icon::save) + "  Save 1 and close");
    REQUIRE(run.program->folder_wanted() == other);
    CHECK(run.folder.read("coin.cpp") == "xint a;\n");
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
    // The group left has the keyboard.
    run.h.type("y");
    REQUIRE(run.document("coin.cpp").buffer().text() == "yxint a;\n");
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

TEST_CASE("Find in files draws the matches in view, and Down brings the one picked into it", "[program][find_in_files]") {
    Running run({}, [](const TempFolder& folder) {
        for (int i = 0; i < 40; ++i) folder.write(std::format("f{:02}.txt", i), std::format("match {}\n", i));
    });
    run.h.press(Key::F, CTRL_SHIFT);
    run.h.type("match");
    wait_for_hits(run);
    REQUIRE(run.program->find_in_files().hits().size() == 40);
    REQUIRE(run.h.find("match 0", Role::TreeItem) != nullptr);
    REQUIRE(run.h.find("match 30", Role::TreeItem) == nullptr);

    for (int i = 0; i < 30; ++i) run.h.press(Key::Down);
    run.h.step(2);
    REQUIRE(run.h.find("match 30", Role::TreeItem) != nullptr);
    REQUIRE(run.h.find("match 0", Role::TreeItem) == nullptr);
}

TEST_CASE("With Vim on, Ctrl-J and Ctrl-K move through find in files' matches", "[program][find_in_files][vim]") {
    Running run({{"a.cpp", "spin\n"}, {"b.cpp", "spin\n"}});
    run.program->preferences().vim = true;
    run.h.press(Key::F, CTRL_SHIFT);
    run.h.type("spin");
    wait_for_hits(run);
    REQUIRE(run.program->find_in_files().hits().size() == 2);
    run.h.press(Key::J, CTRL);
    run.h.press(Key::K, CTRL);
    run.h.press(Key::J, CTRL);
    run.h.step();
    run.h.press(Key::Enter);
    run.h.step(2);
    CHECK(run.program->front() == run.program->workspace().name(run.folder.path / "b.cpp"));
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

namespace {

// The code view of the tab in front, as the last frame drew it.
ftk::ui::core::Rect code_rect(Running& run) {
    const auto* code = run.h.find("Code", Role::TextField);
    REQUIRE(code != nullptr);
    return code->rect;
}

} // namespace

TEST_CASE("Ctrl , opens the preferences, where the text size steps", "[program][preferences]") {
    Running run({{"a.txt", "a\n"}});
    REQUIRE_FALSE(run.program->preferences_window().is_open());
    run.h.press(Key::Comma, CTRL);
    run.h.step(2);
    REQUIRE(run.program->preferences_window().is_open());
    const int before = run.program->preferences().text_size;
    run.press(ftk::ui::icon::add);
    REQUIRE(run.program->preferences().text_size == before + 1);
}

TEST_CASE("Ctrl and the wheel over the code size every view's text, as far as the preferences go",
          "[program][preferences]") {
    Running run({{"a.txt", "a\n"}});
    run.press_row("a.txt");
    const glm::vec2 over = code_rect(run).centre();
    const int before = run.program->preferences().text_size;
    run.h.hold(CTRL);
    run.h.wheel(over, 2.0f);
    run.h.step();
    REQUIRE(run.program->preferences().text_size == before + 2);
    run.h.wheel(over, 100.0f);
    run.h.step();
    REQUIRE(run.program->preferences().text_size == TEXT_SIZE_MAX);
    run.h.wheel(over, -100.0f);
    run.h.step();
    REQUIRE(run.program->preferences().text_size == TEXT_SIZE_MIN);
    run.h.hold({});
}

TEST_CASE("The minimap's checkbox in the preferences turns it off", "[program][preferences]") {
    Running run({{"a.txt", "a\n"}});
    REQUIRE(run.program->run("edit.preferences"));
    run.h.step(2);
    std::vector<ftk::ui::core::Rect> toggles;
    for (const auto& node : run.h.ui().semantics()) {
        if (node.role == Role::Toggle) toggles.push_back(node.rect);
    }
    // The page's checkboxes, in the order drawn: Minimap's, in Text, before
    // Vim keys', in Keys.
    REQUIRE(toggles.size() == 2);
    run.h.click_at(toggles.front().centre());
    run.h.step();
    REQUIRE_FALSE(run.program->preferences().minimap);
}

TEST_CASE("Every view shows its code at the text size preferred", "[program][preferences]") {
    Running run({{"a.txt", "a\nb\nc\nd\n"}});
    run.press_row("a.txt");
    // Two and a half lines down at the size a view starts at is the third
    // line; at the largest size, the second.
    const ftk::ui::core::Rect code = code_rect(run);
    const glm::vec2 at{code.min.x + 300.0f, code.min.y + 2.5f * code_line(CODE_TEXT)};
    run.program->preferences().text_size = TEXT_SIZE_MAX;
    run.h.step(2);
    run.h.click_at(at);
    run.h.step();
    run.h.type("x");
    REQUIRE(run.document("a.txt").buffer().text() == "a\nbx\nc\nd\n");
}

TEST_CASE("Vim keys preferred give every view Vim's keys, and the status bar its mode", "[program][preferences][vim]") {
    Running run({{"a.txt", "abc\n"}});
    run.press_row("a.txt");
    run.program->preferences().vim = true;
    run.h.step(2);
    // The insert mode's pill: the toggle colour drawn on the status bar.
    const auto pill_on = [&] {
        ftk::ui::Color toggle = ftk::theme::toggle_on();
        toggle.a = 1.0f;
        const uint32_t on = ftk::ui::core::pack_colour(toggle);
        const float bar_top = run.h.size().y - ftk::theme::STATUS_BAR;
        for (const ftk::ui::core::RootOutput& root : run.h.output().roots) {
            for (const ftk::ui::core::Primitive& p : root.draw->primitives) {
                if (p.colour == on && p.min.y >= bar_top) return true;
            }
        }
        return false;
    };
    CHECK_FALSE(pill_on());
    // A, as Vim has it, appends at the line's end: without Vim it would be
    // typed.
    run.h.type("A");
    run.h.step();
    CHECK(pill_on());
    run.h.type("Z");
    run.h.press(Key::Escape);
    run.h.step();
    REQUIRE(run.document("a.txt").buffer().text() == "abcZ\n");
    CHECK_FALSE(pill_on());
}

TEST_CASE("The menus show the keys of the mode the view in front is in", "[program][vim]") {
    Running run({{"a.txt", "abc\n"}});
    run.press_row("a.txt");
    CHECK(run.program->shortcut("edit.undo") == "Ctrl Z");
    CHECK(run.program->shortcut("file.close") == "Ctrl W");
    run.program->preferences().vim = true;
    run.h.step();
    CHECK(run.program->shortcut("edit.undo") == "u");
    CHECK(run.program->shortcut("edit.redo") == "Ctrl R");
    CHECK(run.program->shortcut("edit.cut") == "\"+dd");
    // A terminal's copy and paste, which Vim keeps.
    CHECK(run.program->shortcut("edit.copy") == "Ctrl Shift C");
    CHECK(run.program->shortcut("edit.paste") == "Ctrl Shift V");
    // The program's keys and the bindings Vim keeps, and none for what Vim
    // has no keys for yet.
    CHECK(run.program->shortcut("file.save") == "Ctrl S");
    CHECK(run.program->shortcut("format.document") == "Ctrl Shift I");
    CHECK(run.program->shortcut("definition.go") == "gd");
    CHECK(run.program->shortcut("symbol.rename") == "grn");
    CHECK(run.program->shortcut("file.close") == "Ctrl W q");
    CHECK(run.program->shortcut("find.open").empty());
    run.h.type("v");
    run.h.step();
    CHECK(run.program->shortcut("edit.cut") == "\"+d");
    CHECK(run.program->shortcut("edit.copy") == "Ctrl Shift C");
    CHECK(run.program->shortcut("edit.undo").empty());
    CHECK(run.program->shortcut("file.close") == "Ctrl W q");
    run.h.press(Key::Escape);
    run.h.type("i");
    run.h.step();
    CHECK(run.program->shortcut("edit.undo") == "Ctrl O u");
    CHECK(run.program->shortcut("edit.paste") == "Ctrl Shift V");
    CHECK(run.program->shortcut("edit.copy").empty());
    CHECK(run.program->shortcut("file.close") == "Ctrl O Ctrl W q");
}

TEST_CASE("With Vim on, :set rnu and so change every view for the session, Preferences left as they are",
          "[program][vim]") {
    Running run({{"a.txt", "abc\n"}});
    run.program->preferences().vim = true;
    run.press_row("a.txt");
    run.h.step();
    run.h.type(":set rnu so=7");
    run.h.press(Key::Enter);
    run.h.step(2);
    CHECK(run.program->vim_settings().relative_numbers_shown);
    CHECK(run.program->vim_settings().around_caret_shown == 7);
    CHECK(run.program->preferences().line_numbers == LineNumbers::Absolute);
    CHECK(run.program->preferences().around_caret == 0);
}

TEST_CASE("With Vim on, Ctrl W is Vim's and closes no tab", "[program][vim]") {
    Running run({{"a.txt", "foo bar\n"}});
    run.program->preferences().vim = true;
    run.press_row("a.txt");
    run.h.step();
    run.h.type("A");
    run.h.press(Key::W, CTRL);
    run.h.step(2);
    REQUIRE(run.program->front().has_value());
    REQUIRE(run.document("a.txt").buffer().text() == "foo \n");
}

TEST_CASE("With Vim on, what is yanked in one file can be put in another", "[program][vim]") {
    Running run({{"a.txt", "spin\n"}, {"b.txt", "roll\n"}});
    run.program->preferences().vim = true;
    run.press_row("a.txt");
    run.h.step();
    run.h.type("yy");
    run.press_row("b.txt");
    run.h.step();
    run.h.type("p");
    REQUIRE(run.document("b.txt").buffer().text() == "roll\nspin\n");
}

TEST_CASE("Every view keeps the rows preferred in sight around the caret", "[program][preferences]") {
    std::string text;
    for (int i = 0; i < 100; ++i) text += "line\n";
    Running run({{"a.txt", text.c_str()}});
    run.press_row("a.txt");
    run.program->preferences().around_caret = 5;
    run.h.step();
    // The caret down to the last row wholly in sight: five more rows come
    // into sight under it, so the view's top row is the fifth or the sixth
    // line, as the view's height falls between rows.
    const ftk::ui::core::Rect code = code_rect(run);
    const float line = code_line(CODE_TEXT);
    const auto rows = static_cast<int>(code.height() / line);
    for (int i = 0; i < rows - 1; ++i) run.h.press(Key::Down);
    run.h.click_at({code.min.x + 300.0f, code.min.y + 0.5f * line});
    run.h.step();
    run.h.type("x");
    const Buffer& buffer = run.document("a.txt").buffer();
    std::size_t typed_on = 0;
    for (std::size_t l = 0; l < buffer.line_count(); ++l) {
        if (buffer.line(l) == "linex") typed_on = l;
    }
    CHECK(typed_on >= 4);
    CHECK(typed_on <= 5);
}

TEST_CASE("The folders opened lately are kept in the settings file, in order, each once", "[program][preferences]") {
    std::vector<std::string> kept{"/home/user/game", "/home/user/tools"};
    const ftk::ui::SettingsSection section = recent_folders_section(kept);
    const std::vector<ftk::ui::SettingsEntry> written = section.write();
    REQUIRE(written.size() == 1);

    std::vector<std::string> read;
    ftk::ui::SettingsSection reading = recent_folders_section(read);
    for (const std::string& line : written.front().lines) reading.read(written.front().name, line);
    REQUIRE(read == kept);
    // The file read again adds nothing; another entry's lines are not its.
    for (const std::string& line : written.front().lines) reading.read(written.front().name, line);
    reading.read("Code", "Folder=/home/user/other");
    reading.read(written.front().name, "Folder=");
    REQUIRE(read == kept);
}

TEST_CASE("A C++ file opened is told to its language service, closed and saved too", "[program][language]") {
    Running run({{"coin.cpp", "int a;\n"}, {"notes.txt", "a\n"}}, {}, Running::Languages::Fake);
    run.press_row("notes.txt");
    REQUIRE(run.language == nullptr);
    run.press_row("coin.cpp");
    REQUIRE(run.language != nullptr);
    const std::filesystem::path coin = run.program->workspace().name(run.folder.path / "coin.cpp");
    REQUIRE(run.language->opened == std::vector<std::filesystem::path>{coin});

    run.h.type("x");
    run.h.press(Key::S, CTRL);
    REQUIRE(run.language->saved_paths == std::vector<std::filesystem::path>{coin});
    run.h.press(Key::W, CTRL);
    run.h.step(2);
    REQUIRE(run.language->closed == std::vector<std::filesystem::path>{coin});
}

TEST_CASE("A definition in another file opens it there, the place selected", "[program][language]") {
    Running run({{"coin.cpp", "#include \"coin.hpp\"\nint a = speed;\n"}, {"coin.hpp", "// coin\nint speed;\n"}}, {}, Running::Languages::Fake);
    run.press_row("coin.cpp");
    run.h.press(Key::F12);
    run.h.step();
    REQUIRE(run.language->definitions.size() == 1);
    const std::filesystem::path header = run.program->workspace().name(run.folder.path / "coin.hpp");
    FakeLanguage::answer(run.language->definitions.front(), std::vector<Location>{{header, {{1, 4}, {1, 9}}}});
    run.h.step(3);
    REQUIRE(run.program->front() == header);
    REQUIRE(run.document("coin.hpp").buffer().line(1) == "int speed;");
    // The keyboard in it, the definition selected: typing replaces it.
    run.h.type("pace");
    run.h.step();
    REQUIRE(run.document("coin.hpp").buffer().line(1) == "int pace;");
}

TEST_CASE("clangd, run by the program, finds the problems in a C++ file opened", "[program][language][clangd]") {
    const std::vector<std::string> probe{"clangd", "--version"};
    if (!ftk::platform::ChildProcess::start(probe)) SKIP("clangd isn't installed");
    Running run({{"compile_flags.txt", "-std=c++20\n"}, {"coin.cpp", "int spin() {\n    return speed;\n}\n"}}, {},
                Running::Languages::Clangd);
    run.press_row("coin.cpp");
    // clangd parses the file on a thread of its own; the program draws its
    // problem when it comes: the line's tint in the error colour.
    const auto tinted = [&] {
        const uint32_t tint = ftk::ui::core::pack_colour(ftk::theme::code_error_line());
        for (const ftk::ui::core::RootOutput& root : run.h.output().roots) {
            for (const ftk::ui::core::Primitive& p : root.draw->primitives) {
                if (p.colour == tint) return true;
            }
        }
        return false;
    };
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!tinted() && std::chrono::steady_clock::now() < until) {
        run.h.step();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(tinted());
}


TEST_CASE("F2 renames in place, and the rename's edits are made in every file", "[program][language][rename]") {
    Running run({{"coin.cpp", "int speed;\nint f() { return speed; }\n"}, {"other.cpp", "extern int speed;\n"}}, {},
                Running::Languages::Fake);
    run.press_row("coin.cpp");
    for (int i = 0; i < 5; ++i) run.h.press(Key::Right);
    run.h.press(Key::F2);
    run.h.step(2);
    REQUIRE(run.language->highlights.size() == 1);
    FakeLanguage::answer(run.language->highlights.front(), std::vector<Span>{{{0, 4}, {0, 9}}, {{1, 17}, {1, 22}}});
    run.h.step(2);
    run.h.type("pace");
    run.h.step();
    REQUIRE(run.document("coin.cpp").buffer().text() == "int pace;\nint f() { return pace; }\n");
    // Enter takes the typing back and asks the service.
    run.h.press(Key::Enter);
    run.h.step(2);
    REQUIRE(run.document("coin.cpp").buffer().text() == "int speed;\nint f() { return speed; }\n");
    REQUIRE(run.language->renames.size() == 1);
    REQUIRE(run.language->new_names == std::vector<std::string>{"pace"});
    REQUIRE(run.language->renames.front().at == Position{0, 4});

    const std::filesystem::path coin = run.program->workspace().name(run.folder.path / "coin.cpp");
    const std::filesystem::path other = run.program->workspace().name(run.folder.path / "other.cpp");
    WorkspaceEdit edits;
    edits[coin] = {.version = run.document("coin.cpp").version(),
                   .edit = {.changes = {{.span = {{0, 4}, {0, 9}}, .text = "pace"}, {.span = {{1, 17}, {1, 22}}, .text = "pace"}}}};
    edits[other] = {.version = std::nullopt, .edit = {.changes = {{.span = {{0, 11}, {0, 16}}, .text = "pace"}}}};
    FakeLanguage::answer(run.language->renames.front(), ftk::Result<WorkspaceEdit>(edits));
    run.h.step(2);
    REQUIRE(run.document("coin.cpp").buffer().text() == "int pace;\nint f() { return pace; }\n");
    REQUIRE(run.program->workspace().unsaved(coin));
    REQUIRE(run.folder.read("other.cpp") == "extern int pace;\n");
}

TEST_CASE("fjls, run by the program beside clangd, finds the problems in a script header", "[program][language][fjls]") {
    const std::vector<std::string> probe{"fjls"};
    if (!ftk::platform::ChildProcess::start(probe)) SKIP("fjls isn't on the PATH");
    // Plain C++ to clangd, its macro defined and its attribute only
    // unknown; to fjreflect, an attribute it has no such name for.
    Running run({{"project.fjell", "{}\n"},
                 {"scripts/mover.hpp", "#define FJELL_COMPONENT(name)\n"
                                       "class Mover {\n"
                                       "    FJELL_COMPONENT(Mover)\n"
                                       "public:\n"
                                       "    [[fj::propery]] int typo = 0;\n"
                                       "};\n"}},
                {}, Running::Languages::Clangd);
    run.press_row("scripts");
    run.h.step();
    run.press_row("mover.hpp");
    const auto tinted = [&] {
        const uint32_t tint = ftk::ui::core::pack_colour(ftk::theme::code_error_line());
        for (const ftk::ui::core::RootOutput& root : run.h.output().roots) {
            for (const ftk::ui::core::Primitive& p : root.draw->primitives) {
                if (p.colour == tint) return true;
            }
        }
        return false;
    };
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!tinted() && std::chrono::steady_clock::now() < until) {
        run.h.step();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(tinted());
}

TEST_CASE("Escape gives a rename in place up with nothing asked", "[program][language][rename]") {
    Running run({{"coin.cpp", "int speed;\n"}}, {}, Running::Languages::Fake);
    run.press_row("coin.cpp");
    for (int i = 0; i < 5; ++i) run.h.press(Key::Right);
    run.h.press(Key::F2);
    run.h.step(2);
    REQUIRE(run.language->highlights.size() == 1);
    FakeLanguage::answer(run.language->highlights.front(), std::vector<Span>{{{0, 4}, {0, 9}}});
    run.h.step(2);
    run.h.type("pace");
    run.h.press(Key::Escape);
    run.h.step(2);
    REQUIRE(run.language->renames.empty());
    REQUIRE(run.document("coin.cpp").buffer().text() == "int speed;\n");
    // The keyboard is the code's again.
    run.h.type("x");
    run.h.step();
    REQUIRE(run.document("coin.cpp").buffer().line(0) == "int sxpeed;");
}

TEST_CASE("Cancelling the question about closing gives the keyboard back to the code", "[program]") {
    Running run({{"coin.cpp", "int a;\n"}});
    run.press_row("coin.cpp");
    run.h.type("x");
    run.h.press(Key::W, CTRL);
    run.h.step(2);
    run.h.press(Key::Escape);
    run.h.step(2);
    REQUIRE(run.program->front().has_value());
    run.h.type("y");
    run.h.step();
    REQUIRE(run.document("coin.cpp").buffer().line(0) == "xyint a;");
}

namespace {

// Whether the last frame drew anything in `colour` along the status bar.
bool on_status_bar(Running& run, ftk::ui::Color colour) {
    const uint32_t packed = ftk::ui::core::pack_colour(colour);
    const float top = run.h.size().y - ftk::theme::STATUS_BAR;
    for (const ftk::ui::core::RootOutput& root : run.h.output().roots) {
        for (const ftk::ui::core::Primitive& p : root.draw->primitives) {
            if (p.colour == packed && p.min.y >= top) return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("File, Create desktop entry writes fjedit's entry, and the status bar says it did", "[program][desktop]") {
    const std::filesystem::path program = "/opt/fjedit/fjedit";
    Running run({{"coin.cpp", "int a;\n"}}, {}, Running::Languages::None,
                [&](ProgramOptions& options, const TempFolder& folder) {
                    options.desktop_entry = Program::desktop_entry(program, FJEDIT_BRAND);
                    options.desktop_data_home = folder.path / ".share";
                });
    const std::filesystem::path share = run.folder.path / ".share";
    const auto* entry = run.menu_entry("File", "Create desktop entry");
    REQUIRE(entry != nullptr);
    run.h.click_at(entry->rect.centre());
    run.h.step(2);

    CHECK(ftk::desktop_entry_program("fjedit", share) == program);
    CHECK(std::filesystem::exists(share / "icons" / "hicolor" / "scalable" / "apps" / "fjedit.svg"));
    CHECK(on_status_bar(run, ftk::theme::success()));
    CHECK_FALSE(on_status_bar(run, ftk::theme::error()));
    // Said until the next key.
    run.h.press(Key::Down);
    run.h.step();
    CHECK_FALSE(on_status_bar(run, ftk::theme::success()));
}

TEST_CASE("A desktop entry that can't be written is said why on the status bar", "[program][desktop]") {
    // A file stands where the entries' folder would be made.
    Running run({{"coin.cpp", "int a;\n"}, {".share/applications", "not a folder"}}, {}, Running::Languages::None,
                [](ProgramOptions& options, const TempFolder& folder) {
                    options.desktop_entry = Program::desktop_entry("/opt/fjedit/fjedit", FJEDIT_BRAND);
                    options.desktop_data_home = folder.path / ".share";
                });
    const auto* entry = run.menu_entry("File", "Create desktop entry");
    REQUIRE(entry != nullptr);
    run.h.click_at(entry->rect.centre());
    run.h.step(2);
    CHECK(on_status_bar(run, ftk::theme::error()));
    CHECK_FALSE(on_status_bar(run, ftk::theme::success()));
}

TEST_CASE("With nowhere to write a desktop entry, the File menu offers none", "[program][desktop]") {
    Running run({{"coin.cpp", "int a;\n"}}, {}, Running::Languages::None, [](ProgramOptions& options, const TempFolder&) {
        options.desktop_entry = Program::desktop_entry("/opt/fjedit/fjedit", FJEDIT_BRAND);
    });
    // The menu is open, with the entries around where it would be.
    CHECK(run.menu_entry("File", "Quit") != nullptr);
    CHECK(run.h.find("Close", Role::ListItem) != nullptr);
    CHECK(run.h.find("Create desktop entry", Role::ListItem) == nullptr);
}

TEST_CASE("The status bar shows how the language service of the file in front is", "[program][language]") {
    Running run({{"coin.cpp", "int a;\n"}}, {}, Running::Languages::Fake);
    run.press_row("coin.cpp");
    run.h.step();
    REQUIRE(on_status_bar(run, ftk::theme::success()));
    run.language->state = {.state = ServiceState::Stopped, .note = "clangd: no such program"};
    run.h.step(2);
    REQUIRE(on_status_bar(run, ftk::theme::error()));
    REQUIRE_FALSE(on_status_bar(run, ftk::theme::success()));
}

TEST_CASE("A GLSL file is fjls's, started with the first one opened", "[program][language]") {
    Running run({{"shaders/sky.frag", "#version 460\nvoid main() {}\n"}}, {}, Running::Languages::Clangd);
    // A program that isn't there: its dot says so, and nothing runs.
    run.program->preferences().fjls = "fjedit-no-such-fjls";
    run.press_row("shaders");
    run.h.step();
    REQUIRE_FALSE(on_status_bar(run, ftk::theme::error()));
    run.press_row("sky.frag");
    run.h.step(2);
    REQUIRE(on_status_bar(run, ftk::theme::error()));
}

TEST_CASE("A new path for clangd in the preferences starts it again, the views joined to it", "[program][language]") {
    Running run({{"coin.cpp", "int a;\n"}}, {}, Running::Languages::Fake);
    run.press_row("coin.cpp");
    run.h.step(3);
    REQUIRE(run.languages_made == 1);
    run.program->preferences().clangd = "/opt/llvm/bin/clangd";
    run.h.step(2);
    REQUIRE(run.languages_made == 2);
    const std::filesystem::path coin = run.program->workspace().name(run.folder.path / "coin.cpp");
    REQUIRE(run.language->opened == std::vector<std::filesystem::path>{coin});
    // The view asks the new one.
    run.h.press(Key::F12);
    run.h.step();
    REQUIRE(run.language->definitions.size() == 1);
    // And it is started once, not again every frame.
    run.h.step(3);
    REQUIRE(run.languages_made == 2);
}

TEST_CASE("A script header is told to clangd and fjls both, each with its dot on the status bar", "[program][language]") {
    Running run({{"mover.hpp", "struct Mover;\n"}, {"look.fjss", ".a { color: red; }\n"}}, {}, Running::Languages::Both);
    run.press_row("mover.hpp");
    run.h.step();
    const std::filesystem::path mover = run.program->workspace().name(run.folder.path / "mover.hpp");
    REQUIRE(run.language != nullptr);
    REQUIRE(run.fjls != nullptr);
    REQUIRE(run.language->opened == std::vector<std::filesystem::path>{mover});
    REQUIRE(run.fjls->opened == std::vector<std::filesystem::path>{mover});

    // One dot each: fjls stopped, clangd running.
    REQUIRE(on_status_bar(run, ftk::theme::success()));
    REQUIRE_FALSE(on_status_bar(run, ftk::theme::error()));
    run.fjls->state = {.state = ServiceState::Stopped, .note = "fjls: no such program"};
    run.h.step(2);
    REQUIRE(on_status_bar(run, ftk::theme::error()));
    REQUIRE(on_status_bar(run, ftk::theme::success()));

    // A stylesheet is fjls's alone.
    run.press_row("look.fjss");
    run.h.step();
    const std::filesystem::path look = run.program->workspace().name(run.folder.path / "look.fjss");
    REQUIRE(run.fjls->opened == std::vector<std::filesystem::path>{mover, look});
    REQUIRE(run.language->opened == std::vector<std::filesystem::path>{mover});

    // A new path for fjls starts fjls again, and only it.
    run.program->preferences().fjls = "/opt/fjell/fjls";
    run.h.step(2);
    REQUIRE(run.fjls_made == 2);
    REQUIRE(run.languages_made == 1);
    const std::set<std::filesystem::path> told(run.fjls->opened.begin(), run.fjls->opened.end());
    REQUIRE(told == std::set<std::filesystem::path>{mover, look});
}

namespace {

// `line` typed on Vim's `:` line and run.
void ex(Running& run, std::string_view line) {
    run.h.type(std::format(":{}", line));
    run.h.press(Key::Enter);
    run.h.step(2);
}

} // namespace

TEST_CASE("With Vim on, :w saves; :q is refused over unsaved changes, asking nothing, and :q! drops them",
          "[program][vim]") {
    Running run({{"a.txt", "abc\n"}});
    run.program->preferences().vim = true;
    run.press_row("a.txt");
    run.h.step();
    run.h.type("x");
    ex(run, "w");
    CHECK(run.folder.read("a.txt") == "bc\n");
    run.h.type("x");
    REQUIRE_FALSE(on_status_bar(run, ftk::theme::error()));
    ex(run, "q");
    REQUIRE(run.program->front().has_value());
    CHECK(on_status_bar(run, ftk::theme::error()));
    CHECK(run.h.find("Don't save", Role::Button) == nullptr);
    ex(run, "q!");
    CHECK_FALSE(run.program->front().has_value());
    CHECK(run.program->workspace().files().empty());
    CHECK(run.folder.read("a.txt") == "bc\n");
}

TEST_CASE("With Vim on, :q closes a tab whose changes another tab shows, and :wq and :x write and close",
          "[program][vim]") {
    Running run({{"a.txt", "abc\n"}, {"b.txt", "b\n"}});
    run.program->preferences().vim = true;
    run.press_row("a.txt");
    run.h.step();
    run.h.type("x");
    ex(run, "sp");
    REQUIRE(run.program->groups() == 2);
    ex(run, "q");
    CHECK(run.program->groups() == 1);
    CHECK(run.program->workspace().unsaved(run.folder.path / "a.txt"));
    ex(run, "wq");
    CHECK_FALSE(run.program->front().has_value());
    CHECK(run.folder.read("a.txt") == "bc\n");
    // :x writes only a file with changes: what another program wrote stays.
    run.press_row("b.txt");
    run.h.step();
    run.folder.write("b.txt", "outside\n");
    ex(run, "x");
    CHECK_FALSE(run.program->front().has_value());
    CHECK(run.folder.read("b.txt") == "outside\n");
}

TEST_CASE("With Vim on, :qa quits; refused while a file has changes, and :qa! quits dropping them", "[program][vim]") {
    {
        Running run({{"a.txt", "abc\n"}});
        run.program->preferences().vim = true;
        run.press_row("a.txt");
        run.h.step();
        ex(run, "qa");
        CHECK(run.program->quitting());
    }
    Running run({{"a.txt", "abc\n"}, {"b.txt", "b\n"}});
    run.program->preferences().vim = true;
    run.press_row("a.txt");
    run.h.step();
    run.h.type("x");
    run.press_row("b.txt");
    run.h.step();
    ex(run, "qa");
    CHECK_FALSE(run.program->quitting());
    CHECK(on_status_bar(run, ftk::theme::error()));
    ex(run, "qa!");
    CHECK(run.program->quitting());
    CHECK(run.folder.read("a.txt") == "abc\n");
}

TEST_CASE("With Vim on, :e opens a file from the folder and reads the one in front again; :bn and :bp step the tabs",
          "[program][vim]") {
    Running run({{"a.txt", "abc\n"}, {"sub/b.txt", "b\n"}});
    run.program->preferences().vim = true;
    run.press_row("a.txt");
    run.h.step();
    const std::filesystem::path a = run.program->workspace().name(run.folder.path / "a.txt");
    const std::filesystem::path b = run.program->workspace().name(run.folder.path / "sub/b.txt");
    ex(run, "e sub/b.txt");
    CHECK(run.program->front() == b);
    ex(run, "bp");
    CHECK(run.program->front() == a);
    ex(run, "bn");
    CHECK(run.program->front() == b);
    ex(run, "bn");
    CHECK(run.program->front() == a);

    // Read again; refused over changes; with !, the changes dropped, and
    // undone, back.
    run.folder.write("a.txt", "outside\n");
    ex(run, "e");
    CHECK(run.document("a.txt").buffer().text() == "outside\n");
    run.h.type("x");
    ex(run, "e");
    CHECK(run.document("a.txt").buffer().text() == "utside\n");
    CHECK(on_status_bar(run, ftk::theme::error()));
    ex(run, "e!");
    CHECK(run.document("a.txt").buffer().text() == "outside\n");
    CHECK_FALSE(run.program->workspace().unsaved(a));
    run.h.type("u");
    run.h.step();
    CHECK(run.document("a.txt").buffer().text() == "utside\n");
}

TEST_CASE("With Vim on, Ctrl-W v and s split beside, w h and l go between the groups, q and c close",
          "[program][vim]") {
    Running run({{"a.txt", "abc\n"}, {"b.txt", "b\n"}});
    run.program->preferences().vim = true;
    run.press_row("a.txt");
    run.h.step();
    const std::filesystem::path a = run.program->workspace().name(run.folder.path / "a.txt");
    const std::filesystem::path b = run.program->workspace().name(run.folder.path / "b.txt");
    const auto window = [&](std::string_view key) {
        run.h.press(Key::W, CTRL);
        run.h.type(key);
        run.h.step(2);
    };
    window("v");
    REQUIRE(run.program->groups() == 2);
    ex(run, "e b.txt");
    CHECK(run.program->front() == b);
    window("h");
    CHECK(run.program->front() == a);
    // The keys go to the group gone to.
    run.h.type("x");
    CHECK(run.document("a.txt").buffer().text() == "bc\n");
    CHECK(run.document("b.txt").buffer().text() == "b\n");
    window("l");
    CHECK(run.program->front() == b);
    window("l");
    CHECK(run.program->front() == b);
    window("w");
    CHECK(run.program->front() == a);
    window("w");
    CHECK(run.program->front() == b);
    // None stands above or below.
    window("j");
    CHECK(run.program->front() == b);
    CHECK(run.program->groups() == 2);
    window("s");
    CHECK(run.program->groups() == 3);
    window("q");
    window("c");
    CHECK(run.program->groups() == 2);
}

TEST_CASE("With Vim on, Tab after :e completes the folder's paths as the tree lists them", "[program][vim]") {
    Running run({{"a.txt", "abc\n"}, {"sub/b.txt", "b\n"}, {".hidden/c.txt", "c\n"}});
    run.program->preferences().vim = true;
    run.press_row("a.txt");
    run.h.step();
    const auto field = [&] { return run.h.find("Command line", Role::TextField); };
    run.h.type(":e ");
    run.h.step();
    // What the tree lists, the hidden folder left out.
    CHECK(run.h.find("sub/", Role::TreeItem) != nullptr);
    CHECK(run.h.find("a.txt", Role::TreeItem) != nullptr);
    CHECK(run.h.find(".hidden/", Role::TreeItem) == nullptr);
    run.h.type("su");
    run.h.press(Key::Tab);
    run.h.step();
    CHECK(field()->value == "e sub/");
    run.h.press(Key::Tab);
    run.h.step();
    CHECK(field()->value == "e sub/b.txt");
    run.h.press(Key::Enter);
    run.h.step(2);
    CHECK(run.program->front() == run.program->workspace().name(run.folder.path / "sub/b.txt"));
}

TEST_CASE("With Vim on, the : line says :q will be refused before Enter, as the program knows the tabs",
          "[program][vim]") {
    Running run({{"a.txt", "abc\n"}});
    run.program->preferences().vim = true;
    run.press_row("a.txt");
    run.h.step();
    // Whether the last frame drew anything in `colour`.
    const auto drew = [&](ftk::ui::Color colour) {
        const uint32_t packed = ftk::ui::core::pack_colour(colour);
        for (const ftk::ui::core::RootOutput& root : run.h.output().roots) {
            for (const ftk::ui::core::Primitive& p : root.draw->primitives) {
                if (p.colour == packed) return true;
            }
        }
        return false;
    };
    run.h.type("x");
    run.h.type(":q");
    run.h.step();
    CHECK(drew(ftk::theme::warning()));
    run.h.press(Key::Escape);
    run.h.step();
    CHECK_FALSE(drew(ftk::theme::warning()));
    // Shown in a second group too, closing one tab loses nothing.
    run.h.press(Key::W, CTRL);
    run.h.type("v");
    run.h.step(2);
    run.h.type(":q");
    run.h.step();
    CHECK_FALSE(drew(ftk::theme::warning()));
}

TEST_CASE("With Vim on, keys.vim's maps, binds and settings apply from the start; what it can't use shows",
          "[program][vim][keys]") {
    Running run({{"a.txt", "one\ntwo\n"}}, [](const TempFolder& folder) {
        folder.write(".config/keys.vim",
                     "nnoremap Q dd\nbind normal <leader>fg files.find\nset ignorecase\nset relativenumber\nfrob\n"
                     "bind normal <leader>x files.nothing\n");
    });
    run.program->preferences().vim = true;
    run.press_row("a.txt");
    run.h.step();
    run.h.type("Q");
    run.h.step();
    CHECK(run.document("a.txt").buffer().text() == "two\n");
    // A bind shows in the menus, over the program's own keys.
    CHECK(run.program->shortcut("files.find") == chords_text(*vim::parse_notation("<Space>fg")));
    CHECK(run.program->vim_settings().pattern.ignore_case);
    CHECK(run.program->keys_errors() == std::vector<vim::KeysError>{
                                            {.line = 4, .message = "relativenumber: set in Preferences"},
                                            {.line = 5, .message = "Not supported here: frob"},
                                            {.line = 6, .message = "No command called files.nothing"},
                                        });
    CHECK(on_status_bar(run, ftk::theme::warning()));
}

TEST_CASE("With Vim on, keys.vim saved in fjedit applies at once", "[program][vim][keys]") {
    Running run({{"a.txt", "one\ntwo\n"}}, [](const TempFolder& folder) {
        folder.write(".config/keys.vim", "\" mine\n");
    });
    run.program->preferences().vim = true;
    run.program->open(run.keys_file());
    run.h.step();
    run.h.type("ggonnoremap Q dd");
    run.h.press(Key::Escape);
    ex(run, "w");
    CHECK(run.folder.read(".config/keys.vim") == "\" mine\nnnoremap Q dd\n");
    run.press_row("a.txt");
    run.h.step();
    run.h.type("Q");
    run.h.step();
    CHECK(run.document("a.txt").buffer().text() == "two\n");
}

TEST_CASE("Preferences' Open makes keys.vim with a few lines of comment, and opens it", "[program][keys]") {
    Running run({{"a.txt", ""}});
    REQUIRE(run.program->run("edit.preferences"));
    run.h.step(2);
    run.h.type("maps");
    run.press("Open");
    CHECK(std::filesystem::exists(run.keys_file()));
    CHECK(run.folder.read(".config/keys.vim").starts_with("\" Your maps for Vim's keys"));
    CHECK(run.program->front() == run.program->workspace().name(run.keys_file()));
}

TEST_CASE("Preferences keeps keys.vim's Open in reach however long its path", "[program][keys]") {
    ftk::test::KitHarness h;
    Preferences preferences;
    PreferencesWindow window;
    window.open();
    const std::filesystem::path keys = std::filesystem::path("folder") / std::string(200, 'a') / "keys.vim";
    bool asked = false;
    h.set_ui([&] {
        if (window.draw(preferences, {}, {}, keys).open_keys) asked = true;
    });
    h.step(2);
    h.type("maps");
    h.step();
    const auto* open = h.find("Open", Role::Button);
    REQUIRE(open != nullptr);
    h.click_at(open->rect.centre());
    h.step(2);
    CHECK(asked);
}

TEST_CASE("Every command the program names it runs", "[program]") {
    Running run({{"a.txt", ""}});
    for (const Program::Command& command : Program::commands()) CHECK(run.program->run(command.name));
}

TEST_CASE("With Vim on, Ctrl-O goes back through a group's jumps across files and Ctrl-I forward; a split copies them",
          "[program][vim]") {
    Running run({{"a.txt", "one\ntwo\nthree\nfour"}, {"b.txt", "b1\nb2"}});
    run.program->preferences().vim = true;
    run.press_row("a.txt");
    run.h.step();
    const std::filesystem::path a = run.program->workspace().name(run.folder.path / "a.txt");
    const std::filesystem::path b = run.program->workspace().name(run.folder.path / "b.txt");
    const auto text_of = [&](const std::filesystem::path& path) { return run.program->workspace().find(path)->buffer().text(); };
    const auto keys = [&](std::string_view typed) {
        run.h.type(typed);
        run.h.step(2);
    };
    const auto ctrl_o = [&] {
        run.h.press(Key::O, {.ctrl = true});
        run.h.step(2);
    };
    const auto ctrl_i = [&] {
        run.h.press(Key::Tab);
        run.h.step(2);
    };
    // G from line 2, then :e from line 4 into b.txt.
    keys("jG");
    ex(run, "e b.txt");
    REQUIRE(run.program->front() == b);
    // Back where :e left a.txt (x takes the f of four), then where G left.
    ctrl_o();
    REQUIRE(run.program->front() == a);
    keys("x");
    CHECK(text_of(a) == "one\ntwo\nthree\nour");
    keys("u");
    ctrl_o();
    keys("x");
    CHECK(text_of(a) == "one\nwo\nthree\nfour");
    keys("u");
    // Forward again, to b.txt where the first Ctrl-O left it.
    ctrl_i();
    ctrl_i();
    CHECK(run.program->front() == b);
    // a.txt closed, Ctrl-O opens it again at the place.
    ctrl_o();
    REQUIRE(run.program->front() == a);
    ex(run, "q");
    REQUIRE(run.program->workspace().find(a) == nullptr);
    ctrl_o();
    REQUIRE(run.program->front() == a);
    keys("x");
    CHECK(text_of(a) == "one\nwo\nthree\nfour");
    keys("u");
    // A split goes on from the same list.
    ex(run, "sp");
    REQUIRE(run.program->groups() == 2);
    ctrl_i();
    keys("x");
    CHECK(text_of(a) == "one\ntwo\nthree\nour");
}

TEST_CASE("With Vim on, a file's mark opens its file, closed or not, and is in one file at a time", "[program][vim]") {
    Running run({{"a.txt", "one\n  two\n"}, {"b.txt", "b\n"}});
    run.program->preferences().vim = true;
    run.press_row("a.txt");
    run.h.step();
    const std::filesystem::path a = run.program->workspace().name(run.folder.path / "a.txt");
    const std::filesystem::path b = run.program->workspace().name(run.folder.path / "b.txt");
    run.h.type("jlllmA");
    run.h.step(2);
    run.press_row("b.txt");
    run.h.step();
    run.h.type("`A");
    run.h.step(2);
    REQUIRE(run.program->front() == a);
    // At the very place: x takes the w.
    run.h.type("x");
    run.h.step(2);
    CHECK(run.program->workspace().find(a)->buffer().text() == "one\n  to\n");
    run.h.type("u");
    run.h.step(2);
    // Closed, the file keeps it; ' opens the file again on the first text
    // of the mark's line.
    ex(run, "q");
    REQUIRE(run.program->workspace().find(a) == nullptr);
    run.h.type("'A");
    run.h.step(2);
    REQUIRE(run.program->front() == a);
    run.h.type("x");
    run.h.step(2);
    CHECK(run.program->workspace().find(a)->buffer().text() == "one\n  wo\n");
    run.h.type("u");
    run.h.step(2);
    // Set in b.txt, it leaves a.txt.
    run.press_row("b.txt");
    run.h.step();
    run.h.type("mA");
    run.h.step(2);
    CHECK_FALSE(run.program->workspace().find(a)->mark('A'));
    CHECK(run.program->workspace().find(b)->mark('A'));
    // One no file has is said.
    run.h.type("'Z");
    run.h.step(2);
    CHECK(on_status_bar(run, ftk::theme::error()));
}
