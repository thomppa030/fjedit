#pragma once

// The files of the folder open, down the program's side (sheet 39): the
// folder itself, then folders before files, each by name, a file's icon in
// its category's hue. Names beginning with a dot are left out. A folder is
// read when it is first opened and again when the tree is told to forget
// what it read, so files made or removed elsewhere come and go.

#include "ftk/ui/theme.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fjedit {

/// How a file is shown: its kind's icon, and the hue of its category.
struct FileLook {
    const char* icon{nullptr};
    std::optional<ftk::theme::Hue> hue{};
};

/// How the file at `path` is shown, by its kind: C++ in Logic's hue, shaders
/// in Rendering's, stylesheets and widgets in UI's, JSON assets as data.
[[nodiscard]] FileLook file_look(const std::filesystem::path& path);

class FileTree {
public:
    explicit FileTree(std::filesystem::path root) : root_(std::move(root)) {}

    /// Draws the tree in the space left, its row for `selected` marked.
    /// Returns the file pressed, to open.
    [[nodiscard]] std::optional<std::filesystem::path> draw(const std::filesystem::path& selected);

    /// Reads each folder again when it is next shown.
    void forget_listings() { listed_.clear(); }

private:
    struct Entry {
        std::filesystem::path path;
        std::string name;
        bool folder{false};
    };

    // The entries of `folder`, read the first time they are asked for.
    [[nodiscard]] const std::vector<Entry>& entries(const std::filesystem::path& folder);
    void draw_folder(const std::filesystem::path& folder, int depth, const std::filesystem::path& selected,
                     std::optional<std::filesystem::path>& pressed);

    std::filesystem::path root_;
    std::map<std::filesystem::path, std::vector<Entry>> listed_;
    // The folders open when last drawn, which show the open folder's icon.
    std::set<std::filesystem::path> open_;
};

} // namespace fjedit
