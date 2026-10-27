#include "fjedit/core/file_search.hpp"

#include "print.hpp"
#include "temp_folder.hpp"

#include "ftk/base/thread_pool.hpp"

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <map>
#include <stop_token>
#include <string>
#include <vector>

using namespace fjedit;
namespace fs = std::filesystem;

TEST_CASE("Patterns name files by name, by path, or by folder", "[file_search]") {
    REQUIRE(included("scripts/coin.cpp", ""));
    REQUIRE(included("scripts/coin.cpp", "*.cpp"));
    REQUIRE(included("scripts/coin.cpp", "*.hpp, *.cpp"));
    REQUIRE_FALSE(included("scripts/coin.cpp", "*.hpp"));
    REQUIRE(included("scripts/coin.cpp", "scripts/"));
    REQUIRE_FALSE(included("shaders/toon.fjsl", "scripts/"));
    REQUIRE(included("scripts/coin.cpp", "scripts/c?in.*"));
    REQUIRE_FALSE(included("other/scripts/coin.cpp", "scripts/*.cpp"));
}

TEST_CASE("The files to search leave out folders and files whose names begin with a dot", "[file_search]") {
    const TempFolder folder;
    folder.write("b.cpp", "");
    folder.write("a.fjsl", "");
    folder.write(".hidden", "");
    folder.write(".git/config", "");
    folder.write("scripts/coin.cpp", "");
    const std::vector<fs::path> files = files_to_search(folder.path);
    REQUIRE(files == std::vector<fs::path>{folder.path / "a.fjsl", folder.path / "b.cpp", folder.path / "scripts/coin.cpp"});
    REQUIRE(files_to_search(folder.path, "*.cpp").size() == 2);
}

TEST_CASE("The files to search leave out what git ignores, and nothing in a folder it ignores", "[file_search]") {
    const TempFolder folder;
    const TempFolder home;
    folder.write(".git/HEAD", "ref: refs/heads/main\n");
    folder.write(".gitignore", "build/\n*.o\n!keep.o\n");
    folder.write("src/a.cpp", "");
    folder.write("src/a.o", "");
    folder.write("src/keep.o", "");
    folder.write("build/gen.cpp", "");
    folder.write("build/keep.o", "");
    const GitPlaces places{.home = home.path, .config_home = home.path / ".config"};
    REQUIRE(files_to_search(folder.path, "", {}, places) ==
            std::vector<fs::path>{folder.path / "src/a.cpp", folder.path / "src/keep.o"});
}

TEST_CASE("The walk for the files to search stops when asked", "[file_search]") {
    const TempFolder folder;
    folder.write("a.cpp", "");
    folder.write("scripts/coin.cpp", "");
    std::stop_source stop;
    stop.request_stop();
    REQUIRE(files_to_search(folder.path, "", stop.get_token()).empty());
}

TEST_CASE("Every match in every file, in order, open files as they stand", "[file_search]") {
    const TempFolder folder;
    ftk::ThreadPool pool(3);
    folder.write("a.cpp", "float spin_speed;\nspin();\n");
    folder.write("b.cpp", "spin_speed = spin_speed * 2;\n");
    folder.write("blob.bin", std::string("spin_speed\0", 11));
    const std::vector<fs::path> files = files_to_search(folder.path);
    std::vector<FileHit> hits = search_files(pool, files, "spin_speed", {.whole_word = true});
    REQUIRE(hits.size() == 3);
    REQUIRE(hits[0] == FileHit{folder.path / "a.cpp", {{0, 6}, {0, 16}}, "float spin_speed;"});
    REQUIRE(hits[2].path == folder.path / "b.cpp");
    REQUIRE(hits[2].span == Span{{0, 13}, {0, 23}});

    // An open file is searched as it stands here, not as it is on disk.
    const std::map<fs::path, std::string> open{{folder.path / "a.cpp", "int x;\n"}};
    hits = search_files(pool, files, "spin_speed", {.whole_word = true}, open);
    REQUIRE(hits.size() == 2);
    REQUIRE(hits[0].path == folder.path / "b.cpp");
}

TEST_CASE("A search stops at its limit, or when asked", "[file_search]") {
    const TempFolder folder;
    ftk::ThreadPool pool(3);
    folder.write("a.txt", "x x x x x");
    const std::vector<fs::path> files = files_to_search(folder.path);
    REQUIRE(search_files(pool, files, "x", {}, {}, {}, 3).size() == 3);
    std::stop_source stop;
    stop.request_stop();
    REQUIRE(search_files(pool, files, "x", {}, {}, stop.get_token()).empty());
}

namespace {

// Forty files, the i-th holding i + 1 lines of x: 820 in all.
void write_forty(const TempFolder& folder) {
    for (int i = 0; i < 40; ++i) {
        std::string text;
        for (int line = 0; line <= i; ++line) text += "x\n";
        folder.write(std::format("f{:02}.txt", i), text);
    }
}

} // namespace

TEST_CASE("Files searched on many threads give their matches in order, and the limit takes the first", "[file_search]") {
    // File i holds i + 1 matches, so the first 100 end in file 13, its
    // ninth line: files 0 to 12 hold 91.
    const TempFolder folder;
    write_forty(folder);
    ftk::ThreadPool pool(3);
    const std::vector<fs::path> files = files_to_search(folder.path);

    const std::vector<FileHit> all = search_files(pool, files, "x", {});
    REQUIRE(all.size() == 820);
    for (std::size_t i = 1; i < all.size(); ++i) {
        const bool in_order = all[i - 1].path < all[i].path ||
                              (all[i - 1].path == all[i].path && all[i - 1].span.from.line + 1 == all[i].span.from.line);
        REQUIRE(in_order);
    }

    const std::vector<FileHit> first = search_files(pool, files, "x", {}, {}, {}, 100);
    REQUIRE(first.size() == 100);
    REQUIRE(std::vector<FileHit>(all.begin(), all.begin() + 100) == first);
    REQUIRE(first.back().path == folder.path / "f13.txt");
    REQUIRE(first.back().span.from.line == 8);
}

TEST_CASE("A search hands on its matches as it finds them, in order", "[file_search]") {
    const TempFolder folder;
    write_forty(folder);
    ftk::ThreadPool pool(3);
    const std::vector<fs::path> files = files_to_search(folder.path);
    std::vector<std::vector<FileHit>> runs;
    search_files(pool, files, "x", {}, [&runs](std::vector<FileHit>&& hits) { runs.push_back(std::move(hits)); });
    std::vector<FileHit> handed;
    for (const std::vector<FileHit>& run : runs) {
        REQUIRE_FALSE(run.empty());
        handed.insert(handed.end(), run.begin(), run.end());
    }
    REQUIRE(handed == search_files(pool, files, "x", {}));
}
