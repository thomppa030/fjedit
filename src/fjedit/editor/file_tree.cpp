#include "fjedit/editor/file_tree.hpp"

#include "fjedit/widget/file_kind.hpp"

#include "ftk/ui/kit/icons.hpp"
#include "ftk/ui/kit/tree.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

namespace fjedit {

namespace {

namespace ui = ftk::ui;

// `text` with A to Z as a to z, to sort names as people read them.
std::string folded(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

} // namespace

std::vector<FolderEntry> read_folder(const std::filesystem::path& folder, bool hidden) {
    std::vector<FolderEntry> read;
    std::error_code error;
    for (std::filesystem::directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied,
                                                error),
         end;
         !error && it != end; it.increment(error)) {
        const std::string name = it->path().filename().string();
        if (!hidden && name.starts_with('.')) continue;
        std::error_code kind_error;
        read.push_back({.path = it->path(), .name = name, .folder = it->is_directory(kind_error)});
    }
    std::ranges::sort(read, [](const FolderEntry& a, const FolderEntry& b) {
        if (a.folder != b.folder) return a.folder;
        return folded(a.name) < folded(b.name);
    });
    return read;
}

const std::vector<FolderEntry>& FileTree::entries(const std::filesystem::path& folder) {
    const auto found = listed_.find(folder);
    if (found != listed_.end()) return found->second;
    std::vector<FolderEntry> read = read_folder(folder);
    // By the rules of each folder above it, then its own.
    GitIgnore ignore(folder, places_);
    ignore.enter(folder);
    std::erase_if(read, [&](const FolderEntry& entry) { return ignore.ignored(entry.path, entry.folder); });
    return listed_.emplace(folder, std::move(read)).first->second;
}

std::optional<std::filesystem::path> FileTree::draw(const std::filesystem::path& selected) {
    std::optional<std::filesystem::path> pressed;
    if (auto tree = ui::Tree("##files")) draw_folder(root_, 0, selected, pressed);
    return pressed;
}

void FileTree::draw_folder(const std::filesystem::path& folder, int depth, const std::filesystem::path& selected,
                           std::optional<std::filesystem::path>& pressed) {
    const std::string id = folder.generic_string();
    const std::string name = folder.filename().string();
    // A folder in Structure's grey, open or not, as fjell shows one.
    const ui::TreeRowResult row = ui::tree_row({
        .id = id.c_str(),
        .name = name,
        .depth = depth,
        .has_children = true,
        .icon = ui::icon::folder,
        .hue = ftk::theme::Hue::grey,
        .starts_open = depth == 0,
        .fold_on_click = true,
    });
    if (!row.open) return;
    for (const FolderEntry& entry : entries(folder)) {
        if (entry.folder) {
            draw_folder(entry.path, depth + 1, selected, pressed);
            continue;
        }
        const std::string entry_id = entry.path.generic_string();
        const ui::AssetKind kind = file_kind(entry.path);
        const ui::TreeRowResult file = ui::tree_row({
            .id = entry_id.c_str(),
            .name = entry.name,
            .depth = depth + 1,
            .icon = kind.icon,
            .hue = kind.hue,
            .selection = entry.path == selected ? ui::RowSelection::Primary : ui::RowSelection::None,
        });
        if (file.clicked) pressed = entry.path;
    }
}

} // namespace fjedit
