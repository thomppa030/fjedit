#include "fjedit/core/file_search.hpp"

#include "print.hpp"
#include "temp_folder.hpp"

#include <catch2/catch_test_macros.hpp>

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
    folder.write("a.cpp", "float spin_speed;\nspin();\n");
    folder.write("b.cpp", "spin_speed = spin_speed * 2;\n");
    folder.write("blob.bin", std::string("spin_speed\0", 11));
    const std::vector<fs::path> files = files_to_search(folder.path);
    std::vector<FileHit> hits = search_files(files, "spin_speed", {.whole_word = true});
    REQUIRE(hits.size() == 3);
    REQUIRE(hits[0] == FileHit{folder.path / "a.cpp", {{0, 6}, {0, 16}}, "float spin_speed;"});
    REQUIRE(hits[2].path == folder.path / "b.cpp");
    REQUIRE(hits[2].span == Span{{0, 13}, {0, 23}});

    // An open file is searched as it stands here, not as it is on disk.
    const std::map<fs::path, std::string> open{{folder.path / "a.cpp", "int x;\n"}};
    hits = search_files(files, "spin_speed", {.whole_word = true}, open);
    REQUIRE(hits.size() == 2);
    REQUIRE(hits[0].path == folder.path / "b.cpp");
}

TEST_CASE("A search stops at its limit, or when asked", "[file_search]") {
    const TempFolder folder;
    folder.write("a.txt", "x x x x x");
    const std::vector<fs::path> files = files_to_search(folder.path);
    REQUIRE(search_files(files, "x", {}, {}, {}, 3).size() == 3);
    std::stop_source stop;
    stop.request_stop();
    REQUIRE(search_files(files, "x", {}, {}, stop.get_token()).empty());
}
