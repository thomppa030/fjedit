#include "fjedit/core/commands.hpp"
#include "fjedit/core/recovery.hpp"
#include "fjedit/core/text_file.hpp"
#include "fjedit/core/workspace.hpp"

#include "print.hpp"
#include "temp_folder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace fjedit;
namespace fs = std::filesystem;

namespace {

std::string read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Writes `bytes` over `file` and dates it a little later, as a program
// writing it a moment after would, whatever the file system's clock steps.
void write_later(const fs::path& file, std::string_view bytes) {
    const auto before = fs::last_write_time(file);
    std::ofstream(file, std::ios::binary | std::ios::trunc).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    fs::last_write_time(file, before + std::chrono::seconds(2));
}

// Types `text` at the start of `document` as a view would.
void type_at_start(Document& document, std::string_view text) {
    Selections selections;
    const MonospaceLayout layout;
    CommandContext context{.document = document, .selections = selections, .layout = layout, .text = text};
    REQUIRE(run_command("edit.type", context));
}

} // namespace

TEST_CASE("A file reads back the bytes written to it, its line breaks and mark kept", "[files]") {
    const TempFolder folder;
    const fs::path file = folder.path / "a.txt";
    const std::string bytes = "\xEF\xBB\xBFone\r\ntwo\r\n";
    REQUIRE(write_text_file(file, bytes).has_value());
    const ftk::Result<std::string> read_back = read_text_file(file);
    REQUIRE(read_back.has_value());
    REQUIRE(*read_back == bytes);
    const Buffer buffer(*read_back);
    REQUIRE(buffer.line_ending() == LineEnding::CrLf);
    REQUIRE(buffer.byte_order_mark());
    REQUIRE(buffer.text() == bytes);
}

TEST_CASE("A file with a zero byte early on isn't text, and a missing one can't be read", "[files]") {
    const TempFolder folder;
    REQUIRE_FALSE(read_text_file(folder.write("blob.bin", std::string("ab\0cd", 5))).has_value());
    REQUIRE_FALSE(read_text_file(folder.path / "nowhere.txt").has_value());
}

TEST_CASE("Writing keeps a file's permissions, and a failed write leaves nothing", "[files]") {
    const TempFolder folder;
    const fs::path file = folder.write("run.sh", "echo a\n");
    fs::permissions(file, fs::perms::owner_exec, fs::perm_options::add);
    const bool can_mark = (fs::status(file).permissions() & fs::perms::owner_exec) != fs::perms::none;
    REQUIRE(write_text_file(file, "echo b\n").has_value());
    REQUIRE(read(file) == "echo b\n");
    if (can_mark) REQUIRE((fs::status(file).permissions() & fs::perms::owner_exec) != fs::perms::none);

    REQUIRE_FALSE(write_text_file(folder.path / "no" / "such" / "folder.txt", "x").has_value());
    REQUIRE_FALSE(fs::exists(folder.path / "no"));
}

TEST_CASE("Writing through a link writes the file it names and keeps the link", "[files]") {
    const TempFolder folder;
    const fs::path file = folder.write("real.txt", "one");
    std::error_code error;
    fs::create_symlink(file, folder.path / "link.txt", error);
    if (error) SKIP("this system makes no links here");
    REQUIRE(write_text_file(folder.path / "link.txt", "two").has_value());
    REQUIRE(fs::is_symlink(folder.path / "link.txt"));
    REQUIRE(read(file) == "two");
}

TEST_CASE("A file opens once in a workspace, however its path is written", "[workspace]") {
    const TempFolder folder;
    folder.write("scripts/coin.cpp", "int a;\n");
    Workspace workspace(folder.path);
    const ftk::Result<Document*> first = workspace.open(folder.path / "scripts/coin.cpp");
    REQUIRE(first.has_value());
    REQUIRE(*workspace.open("scripts/coin.cpp") == *first);
    REQUIRE(*workspace.open(folder.path / "scripts" / ".." / "scripts" / "coin.cpp") == *first);
    REQUIRE(workspace.files().size() == 1);
    REQUIRE((*first)->language().name == "C++");
    REQUIRE_FALSE(workspace.open("missing.cpp").has_value());
}

TEST_CASE("Saving writes the document, which is no longer unsaved and no change on disk", "[workspace]") {
    const TempFolder folder;
    const fs::path file = folder.write("a.txt", "one\r\ntwo");
    Workspace workspace(folder.path);
    Document& document = **workspace.open(file);
    REQUIRE_FALSE(workspace.unsaved(file));
    type_at_start(document, "x");
    REQUIRE(workspace.unsaved(file));
    REQUIRE(workspace.save(file).has_value());
    REQUIRE(read(file) == "xone\r\ntwo");
    REQUIRE_FALSE(workspace.unsaved(file));
    REQUIRE(workspace.changed_on_disk().empty());

    workspace.close(file);
    REQUIRE(workspace.find(file) == nullptr);
}

TEST_CASE("A change another program makes is named once, and reloaded as one step", "[workspace]") {
    const TempFolder folder;
    const fs::path file = folder.write("a.txt", "one\ntwo\nthree\nfour");
    Workspace workspace(folder.path);
    Document& document = **workspace.open(file);
    const uint64_t seen = document.version();

    write_later(file, "one\nTWO\nthree\nfour");
    REQUIRE(workspace.changed_on_disk() == std::vector<fs::path>{workspace.name(file)});
    REQUIRE(workspace.changed_on_disk().empty());

    REQUIRE(workspace.reload(file).has_value());
    REQUIRE(document.buffer().text() == "one\nTWO\nthree\nfour");
    REQUIRE_FALSE(workspace.unsaved(file));
    // Only the line that changed was replaced: a caret on another stays.
    Selections caret(Selection::at({3, 2}));
    for (const Edit& edit : *document.edits_since(seen)) caret.map(edit);
    REQUIRE(caret.primary().caret == Position{3, 2});

    // It can be undone, back to what was here, which the file doesn't hold.
    REQUIRE(document.undo().has_value());
    REQUIRE(document.buffer().text() == "one\ntwo\nthree\nfour");
    REQUIRE(workspace.unsaved(file));
}

TEST_CASE("A reload takes a file's new line breaks and mark", "[workspace]") {
    const TempFolder folder;
    const fs::path file = folder.write("a.txt", "one\ntwo");
    Workspace workspace(folder.path);
    Document& document = **workspace.open(file);
    write_later(file, "\xEF\xBB\xBFone\r\ntwo");
    REQUIRE(workspace.reload(file).has_value());
    REQUIRE(document.buffer().text() == "\xEF\xBB\xBFone\r\ntwo");
}

TEST_CASE("Kept work comes back with the file it is for, the latest kept", "[recovery]") {
    const TempFolder folder;
    Recovery recovery(folder.path / "recovery");
    REQUIRE(recovery.kept().empty());
    REQUIRE(recovery.keep("/work/a.cpp", "one").has_value());
    REQUIRE(recovery.keep("/work/b.cpp", "two\r\n").has_value());
    REQUIRE(recovery.keep("/work/a.cpp", "three").has_value());
    std::vector<Recovery::Kept> kept = recovery.kept();
    REQUIRE(kept.size() == 2);
    const auto a = std::ranges::find(kept, fs::path("/work/a.cpp"), &Recovery::Kept::path);
    REQUIRE(a != kept.end());
    REQUIRE(a->bytes == "three");

    recovery.forget("/work/a.cpp");
    kept = recovery.kept();
    REQUIRE(kept.size() == 1);
    REQUIRE(kept[0].bytes == "two\r\n");
}

TEST_CASE("Text put into a document is one step it can undo, and unsaved", "[workspace]") {
    const TempFolder folder;
    const fs::path file = folder.write("a.txt", "one\ntwo");
    Workspace workspace(folder.path);
    Document& document = **workspace.open(file);
    REQUIRE(workspace.set_text(file, "one\nTWO").has_value());
    REQUIRE(document.buffer().text() == "one\nTWO");
    REQUIRE(workspace.unsaved(file));
    REQUIRE(document.undo().has_value());
    REQUIRE_FALSE(workspace.unsaved(file));
}
