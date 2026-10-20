#pragma once

// How a file is shown wherever it is named: its kind's icon, tinted in its
// category's hue (sheet 39). The tree, the tabs, find in files and Vim's `:e`
// rows show it alike.

#include "ftk/ui/theme.hpp"

#include <filesystem>
#include <optional>

namespace fjedit {

/// How a file is shown: its kind's icon, and the hue of its category.
struct FileLook {
    const char* icon{nullptr};
    std::optional<ftk::theme::Hue> hue{};
};

/// How the file at `path` is shown, by its kind: C++ in Logic's hue, shaders
/// in Rendering's, stylesheets and widgets in UI's, JSON assets as data.
[[nodiscard]] FileLook file_look(const std::filesystem::path& path);

} // namespace fjedit
