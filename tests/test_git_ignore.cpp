#include "fjedit/core/git_ignore.hpp"

#include "temp_folder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using namespace fjedit;
namespace fs = std::filesystem;

namespace {

// A repository of a test's own, and a home of its own for the user's git
// config, so the machine's never comes into it.
struct Repo {
    TempFolder folder;
    TempFolder home;

    Repo() { folder.write(".git/HEAD", "ref: refs/heads/main\n"); }

    [[nodiscard]] GitPlaces places() const { return {.home = home.path, .config_home = home.path / ".config"}; }

    // Whether git ignores `path` in the repository: a folder when it ends
    // in a slash, a file otherwise.
    [[nodiscard]] bool ignored(std::string_view path) const {
        const bool is_folder = path.ends_with('/');
        if (is_folder) path.remove_suffix(1);
        const fs::path full = folder.path / path;
        GitIgnore ignore(full.parent_path(), places());
        ignore.enter(full.parent_path());
        return ignore.ignored(full, is_folder);
    }
};

} // namespace

TEST_CASE("Out of a repository nothing is ignored", "[git_ignore]") {
    const TempFolder folder;
    const TempFolder home;
    folder.write(".gitignore", "*.o\n");
    GitIgnore ignore(folder.path, {.home = home.path, .config_home = home.path / ".config"});
    ignore.enter(folder.path);
    REQUIRE_FALSE(ignore.in_repository());
    REQUIRE_FALSE(ignore.ignored(folder.path / "a.o", false));
}

TEST_CASE("A pattern with no slash matches a name at any depth, and one ending in a slash only folders", "[git_ignore]") {
    Repo repo;
    repo.folder.write(".gitignore", "*.o\nbuild/\n");
    REQUIRE(repo.ignored("a.o"));
    REQUIRE(repo.ignored("src/deep/b.o"));
    REQUIRE(repo.ignored("build/"));
    REQUIRE(repo.ignored("src/build/"));
    REQUIRE_FALSE(repo.ignored("build"));
    REQUIRE_FALSE(repo.ignored("a.c"));
}

TEST_CASE("A slash before a pattern's end ties it to the folder of its file", "[git_ignore]") {
    Repo repo;
    repo.folder.write(".gitignore", "/out\ndocs/gen\n");
    repo.folder.write("sub/.gitignore", "/local\n");
    REQUIRE(repo.ignored("out/"));
    REQUIRE_FALSE(repo.ignored("src/out/"));
    REQUIRE(repo.ignored("docs/gen/"));
    REQUIRE_FALSE(repo.ignored("src/docs/gen/"));
    REQUIRE(repo.ignored("sub/local"));
    REQUIRE_FALSE(repo.ignored("local"));
    REQUIRE_FALSE(repo.ignored("sub/x/local"));
}

TEST_CASE("The last pattern that matches decides, and ! takes a file back", "[git_ignore]") {
    Repo repo;
    repo.folder.write(".gitignore", "*.log\n!keep.log\n");
    REQUIRE(repo.ignored("a.log"));
    REQUIRE_FALSE(repo.ignored("keep.log"));
    repo.folder.write(".gitignore", "!keep.log\n*.log\n");
    REQUIRE(repo.ignored("keep.log"));
}

TEST_CASE("A deeper .gitignore has the last word over those above it", "[git_ignore]") {
    Repo repo;
    repo.folder.write(".gitignore", "*.txt\n");
    repo.folder.write("sub/.gitignore", "!notes.txt\n");
    REQUIRE(repo.ignored("notes.txt"));
    REQUIRE_FALSE(repo.ignored("sub/notes.txt"));
    REQUIRE(repo.ignored("sub/other.txt"));
}

TEST_CASE("** as a whole part of a pattern spans any number of folders", "[git_ignore]") {
    Repo repo;
    repo.folder.write(".gitignore", "**/cache\na/**/z\nlogs/**\n");
    REQUIRE(repo.ignored("cache/"));
    REQUIRE(repo.ignored("x/y/cache/"));
    REQUIRE(repo.ignored("a/z"));
    REQUIRE(repo.ignored("a/b/c/z"));
    REQUIRE_FALSE(repo.ignored("b/z"));
    REQUIRE(repo.ignored("logs/x/y"));
    REQUIRE_FALSE(repo.ignored("logs/"));
}

TEST_CASE("Brackets, ? and backslashes read as git reads them, and spaces at a line's end go", "[git_ignore]") {
    Repo repo;
    repo.folder.write(".gitignore", "file[0-9].txt\n[!a]b\nv?.c\n\\#hash\n\\!bang\nspaced   \n# a comment\n");
    REQUIRE(repo.ignored("file3.txt"));
    REQUIRE_FALSE(repo.ignored("filex.txt"));
    REQUIRE(repo.ignored("cb"));
    REQUIRE_FALSE(repo.ignored("ab"));
    REQUIRE(repo.ignored("v1.c"));
    REQUIRE_FALSE(repo.ignored("v12.c"));
    REQUIRE(repo.ignored("#hash"));
    REQUIRE(repo.ignored("!bang"));
    REQUIRE(repo.ignored("spaced"));
    REQUIRE_FALSE(repo.ignored("# a comment"));
}

TEST_CASE("The repository's info/exclude and the user's excludes file count too", "[git_ignore]") {
    Repo repo;
    repo.folder.write(".git/info/exclude", "secret.txt\n");
    repo.home.write(".config/git/ignore", "*.swp\n");
    REQUIRE(repo.ignored("secret.txt"));
    REQUIRE(repo.ignored("a.swp"));

    // core.excludesFile names another in the place of the usual one.
    repo.home.write(".gitconfig", "[user]\n\tname = Kay\n[core]\n\texcludesFile = ~/my_ignores # mine\n");
    repo.home.write("my_ignores", "*.bak\n");
    REQUIRE(repo.ignored("a.bak"));
    REQUIRE_FALSE(repo.ignored("a.swp"));
}

TEST_CASE("A worktree's .git file leads to the repository it shares info/exclude with", "[git_ignore]") {
    const TempFolder main;
    const TempFolder tree;
    const TempFolder home;
    main.write(".git/info/exclude", "shared.txt\n");
    main.write(".git/worktrees/w/commondir", "../..\n");
    tree.write(".git", "gitdir: " + (main.path / ".git/worktrees/w").generic_string() + "\n");
    tree.write(".gitignore", "*.o\n");
    GitIgnore ignore(tree.path, {.home = home.path, .config_home = home.path / ".config"});
    ignore.enter(tree.path);
    REQUIRE(ignore.in_repository());
    REQUIRE(ignore.ignored(tree.path / "shared.txt", false));
    REQUIRE(ignore.ignored(tree.path / "a.o", false));
    REQUIRE_FALSE(ignore.ignored(tree.path / "a.c", false));
}

TEST_CASE("core.ignoreCase lets patterns match in either case", "[git_ignore]") {
    Repo repo;
    repo.folder.write(".gitignore", "*.LOG\nBuild/\n");
    REQUIRE_FALSE(repo.ignored("a.log"));
    repo.folder.write(".git/config", "[core]\n\tignorecase = true\n");
    REQUIRE(repo.ignored("a.log"));
    REQUIRE(repo.ignored("build/"));
}
