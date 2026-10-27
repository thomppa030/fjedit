#pragma once

// Finding text across a folder's files (sheet 39, W3): which files to look
// in, and the matches in each, found as find_all() finds them in one. What
// git ignores is never looked in, a build's output above all. Files open
// are searched as they stand, unsaved changes and all.
//
//     const auto files = fjedit::files_to_search(root, "*.cpp, shaders/");
//     const auto hits = fjedit::search_files(pool, files, "spin_speed", {.whole_word = true}, open_texts);

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/find.hpp"
#include "fjedit/core/git_ignore.hpp"

#include "ftk/base/thread_pool.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit {

/// A match in a file: where it is, and the line it is on as it was found.
struct FileHit {
    std::filesystem::path path;
    Span span;
    std::string line;

    bool operator==(const FileHit&) const = default;
};

/// Whether the file at `relative`, a path within the folder searched, is
/// one `include` names: patterns apart by commas, each a file name with *
/// and ? ("*.cpp"), a path within the folder with them ("scripts/*.hpp"),
/// or a folder ending in a slash ("shaders/"). An empty `include` names
/// every file.
[[nodiscard]] bool included(const std::filesystem::path& relative, std::string_view include);

/// The files under `root` to search, in order of their paths: files and
/// folders whose names begin with a dot are left out, and what git ignores
/// (GitIgnore, its user's config found through `places`), folders it
/// ignores not gone into; and of the rest only the files `include` names
/// are kept (included()). A link to a folder isn't followed. It stops early
/// when `stop` asks, with what it has found.
[[nodiscard]] std::vector<std::filesystem::path> files_to_search(const std::filesystem::path& root,
                                                                 std::string_view include = {},
                                                                 std::stop_token stop = {},
                                                                 const GitPlaces& places = GitPlaces::of_user());

/// What search_files() hands on as it goes: the matches of the files it
/// has searched since it last handed any on, which come after every match
/// handed on before. It is called on one thread at a time, whichever
/// searched last.
using FoundHits = std::function<void(std::vector<FileHit>&& hits)>;

/// Every match of `needle` in `files`, in their order and each file's in
/// order, handed to `found` as they are found. A file in `open` is searched
/// as the bytes given there, any other as it is on disk; one that isn't text
/// is passed over. The files are searched on `pool`'s threads and the
/// calling one, a file at a time. It stops at `limit` matches, or early
/// when `stop` asks.
void search_files(ftk::ThreadPool& pool, std::span<const std::filesystem::path> files, std::string_view needle,
                  FindOptions options, const FoundHits& found,
                  const std::map<std::filesystem::path, std::string>& open = {}, std::stop_token stop = {},
                  std::size_t limit = 10'000);

/// The same, every match returned at once when the search ends: early when
/// `stop` asks, with what it has found.
[[nodiscard]] std::vector<FileHit> search_files(ftk::ThreadPool& pool, std::span<const std::filesystem::path> files,
                                                std::string_view needle, FindOptions options,
                                                const std::map<std::filesystem::path, std::string>& open = {},
                                                std::stop_token stop = {}, std::size_t limit = 10'000);

} // namespace fjedit
