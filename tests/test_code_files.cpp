#include "fjedit/editor/code_files.hpp"

#include "fjedit/core/commands.hpp"
#include "fjedit/core/layout.hpp"

#include "fake_language.hpp"
#include "temp_folder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace fjedit;
namespace fs = std::filesystem;

namespace {

// Types `text` at the start of `document` as a view would.
void type_at_start(Document& document, std::string_view text) {
    Selections selections;
    const MonospaceLayout layout;
    CommandContext context{.document = document, .selections = selections, .layout = layout, .text = text};
    REQUIRE(run_command("edit.type", context));
}

// Writes `bytes` over `file` and dates it a little later, as a program
// writing it a moment after would, whatever the file system's clock steps.
void write_later(const fs::path& file, std::string_view bytes) {
    const auto before = fs::last_write_time(file);
    std::ofstream(file, std::ios::binary | std::ios::trunc).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    fs::last_write_time(file, before + std::chrono::seconds(2));
}

// The files of a folder of the test's own, its work kept in a folder of
// its own beside them, and a fake for clangd that counts how often it is
// made.
struct Files {
    TempFolder folder;
    TempFolder kept;
    Preferences preferences;
    FakeLanguage* language{nullptr};
    int languages_made{0};
    std::unique_ptr<CodeFiles> files;

    explicit Files(std::initializer_list<std::pair<const char*, const char*>> written) {
        for (const auto& [name, bytes] : written) folder.write(name, bytes);
        std::vector<ServiceMaker> services{{.name = "clangd",
                                            .extensions = {"cpp"},
                                            .make = [this](const fs::path&) -> std::unique_ptr<LanguageService> {
                                                auto made = std::make_unique<FakeLanguage>();
                                                language = made.get();
                                                ++languages_made;
                                                return made;
                                            }}};
        files = std::make_unique<CodeFiles>(folder.path, preferences,
                                            CodeFilesOptions{.recovery = kept.path, .services = std::move(services)});
    }

    [[nodiscard]] fs::path at(std::string_view name) const { return files->workspace().name(folder.path / name); }

    Document& open(std::string_view name) {
        const ftk::Result<Document*> document = files->open(folder.path / name);
        REQUIRE(document);
        return **document;
    }
};

} // namespace

TEST_CASE("Code files are saved where they were read, the work kept for them forgotten", "[editor][files]") {
    Files f({{"a.cpp", "int a;\n"}});
    Document& a = f.open("a.cpp");
    type_at_start(a, "x");
    REQUIRE(f.files->unsaved(f.at("a.cpp")));
    CHECK(f.files->unsaved_files() == std::vector<fs::path>{f.at("a.cpp")});
    CHECK(f.files->first_unsaved() == f.at("a.cpp"));

    // Looked at, the unsaved work is kept against a crash.
    CHECK(f.files->update(0.0).looked);
    REQUIRE(Recovery(f.kept.path).kept().size() == 1);

    REQUIRE(f.files->save(f.at("a.cpp")));
    CHECK(f.folder.read("a.cpp") == "xint a;\n");
    CHECK_FALSE(f.files->unsaved(f.at("a.cpp")));
    CHECK_FALSE(f.files->first_unsaved().has_value());
    CHECK(Recovery(f.kept.path).kept().empty());
}

TEST_CASE("Code files look at the disk every two seconds", "[editor][files]") {
    Files f({{"a.cpp", "int a;\n"}});
    CHECK(f.files->update(10.0).looked);
    CHECK(f.files->next_check() == 12.0);
    CHECK_FALSE(f.files->update(11.0).looked);
    CHECK(f.files->update(12.0).looked);
}

TEST_CASE("A code file changed elsewhere comes in when nothing here is unsaved, and is held when something is",
          "[editor][files]") {
    Files f({{"a.cpp", "int a;\n"}, {"b.cpp", "int b;\n"}});
    Document& a = f.open("a.cpp");
    Document& b = f.open("b.cpp");
    type_at_start(b, "x");
    write_later(f.folder.path / "a.cpp", "int outside_a;\n");
    write_later(f.folder.path / "b.cpp", "int outside_b;\n");
    (void)f.files->update(0.0);
    CHECK(a.buffer().text() == "int outside_a;\n");
    CHECK_FALSE(f.files->changed_on_disk(f.at("a.cpp")));
    CHECK(b.buffer().text() == "xint b;\n");
    REQUIRE(f.files->changed_on_disk(f.at("b.cpp")));

    // Mine kept, it isn't asked again.
    f.files->keep_mine(f.at("b.cpp"));
    CHECK_FALSE(f.files->changed_on_disk(f.at("b.cpp")));
    // Read again, it is the file as it is now.
    REQUIRE(f.files->reload(f.at("b.cpp")));
    CHECK(b.buffer().text() == "int outside_b;\n");
}

TEST_CASE("A code file that can't be read or written is noted, and the note goes once one works", "[editor][files]") {
    Files f({{"a.cpp", "int a;\n"}});
    CHECK_FALSE(f.files->open(f.folder.path / "gone.cpp"));
    CHECK_FALSE(f.files->note().empty());
    (void)f.open("a.cpp");
    CHECK(f.files->note().empty());
}

TEST_CASE("Work kept from before is offered, put back unsaved, or let go", "[editor][files]") {
    Files f({{"a.cpp", "int a;\n"}, {"b.cpp", "int b;\n"}});
    {
        Recovery before(f.kept.path);
        REQUIRE(before.keep(f.at("a.cpp"), "int kept_a;\n"));
        REQUIRE(before.keep(f.at("b.cpp"), "int kept_b;\n"));
    }
    REQUIRE(f.files->keeps_work());
    REQUIRE(f.files->kept_from_before().size() == 2);

    Document& a = f.open("a.cpp");
    REQUIRE(f.files->put_back(f.at("a.cpp"), "int kept_a;\n"));
    CHECK(a.buffer().text() == "int kept_a;\n");
    CHECK(f.files->unsaved(f.at("a.cpp")));
    f.files->discard_kept(f.at("b.cpp"));
    const std::vector<Recovery::Kept> left = f.files->kept_from_before();
    REQUIRE(left.size() == 1);
    CHECK(left.front().path == f.at("a.cpp"));
}

TEST_CASE("A file's mark is in one file at a time, and waits for its file while it is let go", "[editor][files]") {
    Files f({{"a.cpp", "int a;\n"}, {"b.cpp", "int b;\n"}});
    Document& a = f.open("a.cpp");
    Document& b = f.open("b.cpp");
    a.set_mark('A', {0, 2});
    b.set_mark('A', {0, 1});
    f.files->keep_file_mark('A', f.at("b.cpp"));
    CHECK_FALSE(a.mark('A').has_value());
    CHECK(f.files->file_with_mark('A') == f.at("b.cpp"));

    f.files->let_go(f.at("b.cpp"));
    CHECK(f.files->workspace().find(f.at("b.cpp")) == nullptr);
    CHECK(f.files->file_with_mark('A') == f.at("b.cpp"));
    Document& again = f.open("b.cpp");
    CHECK(again.mark('A') == Position{0, 1});
    CHECK_FALSE(f.files->file_with_mark('Z').has_value());
}

TEST_CASE("A rename's edits are made in the code files open and written to the others", "[editor][files]") {
    Files f({{"a.cpp", "int speed;\n"}, {"b.cpp", "x = speed;\n"}});
    Document& a = f.open("a.cpp");
    WorkspaceEdit edits;
    edits[f.at("a.cpp")] = {.version = a.version(), .edit = {.changes = {{.span = {{0, 4}, {0, 9}}, .text = "rpm"}}}};
    edits[f.at("b.cpp")] = {.version = std::nullopt, .edit = {.changes = {{.span = {{0, 4}, {0, 9}}, .text = "rpm"}}}};
    f.files->make_edits(edits);
    CHECK(a.buffer().text() == "int rpm;\n");
    CHECK(f.files->unsaved(f.at("a.cpp")));
    CHECK(f.folder.read("b.cpp") == "x = rpm;\n");

    // One changed since the edits were made for it is left as it is.
    edits.erase(f.at("b.cpp"));
    edits[f.at("a.cpp")].version = a.version() + 1;
    f.files->make_edits(edits);
    CHECK(a.buffer().text() == "int rpm;\n");
    CHECK_FALSE(f.files->note().empty());
}

TEST_CASE("A language server is started again when its path in the preferences changes", "[editor][files]") {
    Files f({{"a.cpp", "int a;\n"}});
    Document& a = f.open("a.cpp");
    f.files->services().open(f.at("a.cpp"), a);
    REQUIRE(f.languages_made == 1);
    CHECK_FALSE(f.files->update(0.0).restarted);

    f.preferences.clangd = "/opt/llvm/bin/clangd";
    CHECK(f.files->update(0.5).restarted);
    CHECK(f.languages_made == 2);
    CHECK_FALSE(f.files->update(1.0).restarted);
}
