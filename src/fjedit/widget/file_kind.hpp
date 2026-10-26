#pragma once

// What kind of file a file is, wherever it is named: the tree, the tabs, find
// in files and Vim's `:e` rows show its kind's icon in its hue, as fjell shows
// its files (ftk/ui/kit/asset_kind.hpp).

#include "ftk/ui/kit/asset_kind.hpp"

#include <filesystem>

namespace fjedit {

/// Gives the kit the kinds of fjell's files, as fjell registers them: what
/// fjedit on its own shows them by. A program that registers fjell's kinds
/// itself, fjell, needs none of this.
void register_file_kinds();

/// The kind of the file at `path`: C++ and GLSL by the language fjedit reads
/// them in, scripts in Logic's hue and shaders in Rendering's; any other by
/// its extension, as the kit has it registered, else a plain file in grey.
[[nodiscard]] ftk::ui::AssetKind file_kind(const std::filesystem::path& path);

} // namespace fjedit
