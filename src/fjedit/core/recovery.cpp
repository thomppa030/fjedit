#include "fjedit/core/recovery.hpp"

#include "fjedit/core/text_file.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <system_error>

namespace fjedit {

namespace {

// What a kept file begins with, before the path it is for on a line of its
// own and then the bytes.
constexpr std::string_view HEADER = "fjedit recovered work 1\n";
constexpr std::string_view EXTENSION = ".kept";

// The same number for a path on every run and every machine: FNV-1a.
uint64_t path_hash(std::string_view text) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

} // namespace

std::filesystem::path Recovery::kept_path(const std::filesystem::path& path) const {
    return folder_ / std::format("{:016x}{}", path_hash(path.generic_string()), EXTENSION);
}

ftk::Result<> Recovery::keep(const std::filesystem::path& path, std::string_view bytes) {
    std::error_code error;
    std::filesystem::create_directories(folder_, error);
    if (error) return ftk::make_error("Can't make " + folder_.string() + ": " + error.message());
    std::string kept(HEADER);
    kept += path.generic_string();
    kept += '\n';
    kept += bytes;
    return write_text_file(kept_path(path), kept);
}

void Recovery::forget(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove(kept_path(path), error);
}

std::vector<Recovery::Kept> Recovery::kept() const {
    std::vector<Kept> out;
    std::error_code error;
    for (std::filesystem::directory_iterator it(folder_, error), end; !error && it != end; it.increment(error)) {
        if (it->path().extension() != EXTENSION) continue;
        const ftk::Result<std::string> read = read_text_file(it->path());
        if (!read || !read->starts_with(HEADER)) continue;
        const std::size_t path_end = read->find('\n', HEADER.size());
        if (path_end == std::string::npos) continue;
        std::error_code when_error;
        const auto when = std::filesystem::last_write_time(it->path(), when_error);
        out.push_back({.path = std::filesystem::path(read->substr(HEADER.size(), path_end - HEADER.size())),
                       .bytes = read->substr(path_end + 1),
                       .when = when});
    }
    std::ranges::sort(out, {}, &Kept::when);
    return out;
}

} // namespace fjedit
