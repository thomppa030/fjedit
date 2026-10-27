#pragma once

// The files of the folder open, as a tree down the side (sheet 39): the
// folder itself, then folders before files, each by name, a file's icon in
// its category's hue. Names beginning with a dot are left out, and so is
// what git ignores. A folder is read when it is first opened and again when
// the tree is told to forget what it read, so files made or removed
// elsewhere come and go.

#include "fjedit/core/git_ignore.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fjedit {

/// One of a folder's entries.
struct FolderEntry {
    std::filesystem::path path;
    std::string name;
    bool folder{false};
};

/// The entries of `folder`, folders before files, each by name as people
/// read them; those whose names begin with a dot only with `hidden`. None
/// when it can't be read.
[[nodiscard]] std::vector<FolderEntry> read_folder(const std::filesystem::path& folder, bool hidden = false);

class FileTree {
public:
    /// The tree of `root`, leaving out what git ignores by the repository's
    /// rules and the user's, whose git config is found through `places`.
    explicit FileTree(std::filesystem::path root, GitPlaces places = GitPlaces::of_user())
        : root_(std::move(root)), places_(std::move(places)) {}

    /// Draws the tree in the space left, its row for `selected` marked.
    /// Returns the file pressed, to open.
    [[nodiscard]] std::optional<std::filesystem::path> draw(const std::filesystem::path& selected);

    /// Reads each folder again when it is next shown.
    void forget_listings() { listed_.clear(); }

private:
    // The entries of `folder` git doesn't ignore, read the first time they
    // are asked for.
    [[nodiscard]] const std::vector<FolderEntry>& entries(const std::filesystem::path& folder);
    void draw_folder(const std::filesystem::path& folder, int depth, const std::filesystem::path& selected,
                     std::optional<std::filesystem::path>& pressed);

    std::filesystem::path root_;
    GitPlaces places_;
    std::map<std::filesystem::path, std::vector<FolderEntry>> listed_;
};

} // namespace fjedit
