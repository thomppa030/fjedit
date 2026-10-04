#include "fjedit/core/text_file.hpp"

#include "print.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace fjedit;
namespace fs = std::filesystem;

namespace {

// A folder of its own for a test, gone with it.
struct TempFolder {
    fs::path path;

    TempFolder() {
        std::random_device random;
        path = fs::temp_directory_path() / ("fjedit-test-" + std::to_string(random()) + std::to_string(random()));
        fs::create_directories(path);
    }
    ~TempFolder() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    // Writes `bytes` to `name` in the folder, as another program would.
    fs::path write(const fs::path& name, std::string_view bytes) const {
        const fs::path file = path / name;
        fs::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return file;
    }
};

std::string read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
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
