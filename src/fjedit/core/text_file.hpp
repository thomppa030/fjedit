#pragma once

// Reading and writing the files the editor opens. A file's bytes go into a
// Buffer as they are (its line breaks and byte order mark kept there) and
// come back out of Buffer::text(); nothing is converted on the way.
//
//     auto bytes = fjedit::read_text_file(path);
//     if (!bytes) return report(bytes.error());
//     ...
//     if (auto written = fjedit::write_text_file(path, document.buffer().text()); !written) report(written.error());

#include "ftk/base/result.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace fjedit {

/// The bytes of the file at `path`. An error when it can't be read, or when
/// it holds a zero byte early on and so is no text the editor can show.
[[nodiscard]] ftk::Result<std::string> read_text_file(const std::filesystem::path& path);

/// Writes `bytes` to the file at `path`, or to the file it links to: to a
/// file beside it first, which then takes its place, so a crash part way
/// never leaves half a file. The file keeps its permissions. An error when
/// it can't be written, and the file is as it was.
[[nodiscard]] ftk::Result<> write_text_file(const std::filesystem::path& path, std::string_view bytes);

} // namespace fjedit
