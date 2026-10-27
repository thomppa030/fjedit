#include "fjedit/core/file_search.hpp"

#include "fjedit/core/text_file.hpp"

#include <algorithm>
#include <system_error>

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

std::vector<FileHit> search_files(std::span<const std::filesystem::path> files, std::string_view needle,
                                  FindOptions options, const std::map<std::filesystem::path, std::string>& open,
                                  std::stop_token stop, std::size_t limit) {
    std::vector<FileHit> hits;
    if (needle.empty()) return hits;
    for (const std::filesystem::path& path : files) {
        if (stop.stop_requested() || hits.size() >= limit) break;
        const auto held = open.find(path);
        std::string read;
        if (held == open.end()) {
            ftk::Result<std::string> bytes = read_text_file(path);
            if (!bytes) continue;
            read = std::move(*bytes);
        }
        const std::string_view text = held != open.end() ? std::string_view(held->second) : std::string_view(read);
        for (const TextMatch& match : find_in_text(text, needle, options, limit - hits.size())) {
            hits.push_back({.path = path, .span = match.span, .line = std::string(match.line)});
        }
    }
    return hits;
}

} // namespace fjedit
