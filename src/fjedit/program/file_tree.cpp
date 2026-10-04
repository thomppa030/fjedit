#include "fjedit/program/file_tree.hpp"

#include "fjedit/core/language.hpp"
#include "fjedit/program/icons.hpp"

#include "ftk/ui/kit/icons.hpp"
#include "ftk/ui/kit/tree.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace fjedit {

namespace {

namespace ui = ftk::ui;
using ftk::theme::Hue;

// `text` with A to Z as a to z, to sort names as people read them.
std::string folded(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

} // namespace

FileLook file_look(const std::filesystem::path& path) {
    const std::string_view name = language_for(path.filename().string()).name;
    if (name == "C++") return {.icon = icon::code_file, .hue = Hue::slate};
    if (name == "GLSL" || name == "FJSL") return {.icon = icon::code_file, .hue = Hue::blue};
    if (name == "FJSS") return {.icon = icon::sheet_file, .hue = Hue::lilac};
    if (name == "Widget") return {.icon = icon::widget_file, .hue = Hue::lilac};
    if (name == "JSON") {
        if (path.extension() == ".fjmat") return {.icon = icon::data_file, .hue = Hue::blue};
        return {.icon = icon::data_file, .hue = Hue::grey};
    }
    return {.icon = icon::plain_file};
}

const std::vector<FileTree::Entry>& FileTree::entries(const std::filesystem::path& folder) {
    const auto found = listed_.find(folder);
    if (found != listed_.end()) return found->second;
    std::vector<Entry> read;
    std::error_code error;
    for (std::filesystem::directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied,
                                                error),
         end;
         !error && it != end; it.increment(error)) {
        const std::string name = it->path().filename().string();
        if (name.starts_with('.')) continue;
        std::error_code kind_error;
        read.push_back({.path = it->path(), .name = name, .folder = it->is_directory(kind_error)});
    }
    std::ranges::sort(read, [](const Entry& a, const Entry& b) {
        if (a.folder != b.folder) return a.folder;
        return folded(a.name) < folded(b.name);
    });
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
    const bool was_open = open_.contains(folder);
    const ui::TreeRowResult row = ui::tree_row({
        .id = id.c_str(),
        .name = name,
        .depth = depth,
        .has_children = true,
        .icon = was_open ? icon::open_folder : ui::icon::folder,
        .tint = ftk::theme::accent(),
        .starts_open = depth == 0,
        .fold_on_click = true,
    });
    if (!row.open) {
        open_.erase(folder);
        return;
    }
    open_.insert(folder);
    for (const Entry& entry : entries(folder)) {
        if (entry.folder) {
            draw_folder(entry.path, depth + 1, selected, pressed);
            continue;
        }
        const std::string entry_id = entry.path.generic_string();
        const FileLook look = file_look(entry.path);
        const ui::TreeRowResult file = ui::tree_row({
            .id = entry_id.c_str(),
            .name = entry.name,
            .depth = depth + 1,
            .icon = look.icon,
            .hue = look.hue,
            .selection = entry.path == selected ? ui::RowSelection::Primary : ui::RowSelection::None,
        });
        if (file.clicked) pressed = entry.path;
    }
}

} // namespace fjedit
