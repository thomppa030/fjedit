#include "fjedit/core/file_search.hpp"

#include "fjedit/core/text_file.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <system_error>
#include <utility>

namespace fjedit {

namespace {

// Whether `text` is matched whole by `pattern`, where * stands for any run
// of characters and ? for one.
bool glob(std::string_view pattern, std::string_view text) {
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star = std::string_view::npos;
    std::size_t from = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            from = t;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            t = ++from;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

std::string_view trimmed(std::string_view text) {
    const std::size_t first = text.find_first_not_of(' ');
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

// The files under `folder` to search, added to `files` a folder at a time,
// with what git ignores there left out.
void walk(const std::filesystem::path& folder, const std::filesystem::path& root, std::string_view include,
          GitIgnore& ignore, std::stop_token stop, std::vector<std::filesystem::path>& files) {
    ignore.enter(folder);
    std::error_code error;
    std::filesystem::directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied, error);
    for (const std::filesystem::directory_iterator end; !error && it != end; it.increment(error)) {
        if (stop.stop_requested()) break;
        const std::filesystem::path& path = it->path();
        if (path.filename().string().starts_with('.')) continue;
        // A link is a file to git, whatever it points at.
        std::error_code kind_error;
        const bool link = it->is_symlink(kind_error);
        const bool is_folder = !link && it->is_directory(kind_error);
        if (ignore.ignored(path, is_folder)) continue;
        if (is_folder) {
            walk(path, root, include, ignore, stop, files);
        } else if (it->is_regular_file(kind_error) && included(path.lexically_relative(root), include)) {
            files.push_back(path);
        }
    }
    ignore.leave();
}

// The first `limit` matches in the file at `path`: as `open` holds it, or
// as it is on disk; none when it isn't text.
std::vector<FileHit> search_file(const std::filesystem::path& path, std::string_view needle, FindOptions options,
                                 const std::map<std::filesystem::path, std::string>& open, std::size_t limit) {
    const auto held = open.find(path);
    std::string read;
    if (held == open.end()) {
        ftk::Result<std::string> bytes = read_text_file(path);
        if (!bytes) return {};
        read = std::move(*bytes);
    }
    const std::string_view text = held != open.end() ? std::string_view(held->second) : std::string_view(read);
    std::vector<FileHit> hits;
    for (const TextMatch& match : find_in_text(text, needle, options, limit)) {
        hits.push_back({.path = path, .span = match.span, .line = std::string(match.line)});
    }
    return hits;
}

} // namespace

bool included(const std::filesystem::path& relative, std::string_view include) {
    if (trimmed(include).empty()) return true;
    const std::string path = relative.generic_string();
    const std::string name = relative.filename().string();
    std::size_t begin = 0;
    while (begin <= include.size()) {
        const std::size_t comma = std::min(include.find(',', begin), include.size());
        const std::string_view pattern = trimmed(include.substr(begin, comma - begin));
        begin = comma + 1;
        if (pattern.empty()) continue;
        if (pattern.ends_with('/')) {
            if (path.starts_with(pattern)) return true;
        } else if (pattern.find('/') != std::string_view::npos ? glob(pattern, path) : glob(pattern, name)) {
            return true;
        }
    }
    return false;
}

std::vector<std::filesystem::path> files_to_search(const std::filesystem::path& root, std::string_view include,
                                                   std::stop_token stop, const GitPlaces& places) {
    std::vector<std::filesystem::path> files;
    GitIgnore ignore(root, places);
    walk(root, root, include, ignore, stop, files);
    std::ranges::sort(files);
    return files;
}

void search_files(ftk::ThreadPool& pool, std::span<const std::filesystem::path> files, std::string_view needle,
                  FindOptions options, const FoundHits& found,
                  const std::map<std::filesystem::path, std::string>& open, std::stop_token stop, std::size_t limit) {
    if (needle.empty() || limit == 0) return;
    // Each file's matches wait in its place until every file before it is
    // done, so they are handed on in order whichever thread found them.
    std::vector<std::vector<FileHit>> searched(files.size());
    std::vector<bool> done(files.size(), false);
    std::mutex mutex;
    // The files handed on, all of them before the rest, and their matches.
    std::size_t handed = 0;
    std::size_t given = 0;
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> count{0};
    // Each thread takes the next file until none is left: a big file holds
    // up only the thread searching it. Once the files taken have the
    // matches asked for, no more are taken, as every file left comes after
    // them.
    pool.parallel_for(0, pool.thread_count() + 1, 1, [&](uint32_t, uint32_t, uint32_t) {
        while (!stop.stop_requested() && count.load() < limit) {
            const std::size_t index = next.fetch_add(1);
            if (index >= files.size()) return;
            std::vector<FileHit> hits = search_file(files[index], needle, options, open, limit);
            count += hits.size();

            const std::lock_guard lock(mutex);
            searched[index] = std::move(hits);
            done[index] = true;
            // The files done from the first not yet handed on, together.
            std::vector<FileHit> run;
            for (; handed < files.size() && done[handed]; ++handed) {
                std::vector<FileHit>& file = searched[handed];
                const std::size_t taken = std::min(file.size(), limit - given);
                run.insert(run.end(), std::make_move_iterator(file.begin()),
                           std::make_move_iterator(file.begin() + static_cast<std::ptrdiff_t>(taken)));
                given += taken;
                file = {};
            }
            if (!run.empty()) found(std::move(run));
        }
    });
}

std::vector<FileHit> search_files(ftk::ThreadPool& pool, std::span<const std::filesystem::path> files,
                                  std::string_view needle, FindOptions options,
                                  const std::map<std::filesystem::path, std::string>& open, std::stop_token stop,
                                  std::size_t limit) {
    std::vector<FileHit> hits;
    search_files(
        pool, files, needle, options,
        [&hits](std::vector<FileHit>&& found) {
            hits.insert(hits.end(), std::make_move_iterator(found.begin()), std::make_move_iterator(found.end()));
        },
        open, stop, limit);
    return hits;
}

} // namespace fjedit
