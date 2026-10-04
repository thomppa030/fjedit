#include "fjedit/core/text_file.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

namespace fjedit {

namespace {

// How far into a file a zero byte marks it as no text.
constexpr std::size_t BINARY_LOOK = 8000;

} // namespace

ftk::Result<std::string> read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return ftk::make_error("Can't read " + path.string());
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) return ftk::make_error("Can't read " + path.string());
    const std::string_view head = std::string_view(bytes).substr(0, BINARY_LOOK);
    if (head.find('\0') != std::string_view::npos) return ftk::make_error(path.filename().string() + " isn't text");
    return bytes;
}

ftk::Result<> write_text_file(const std::filesystem::path& path, std::string_view bytes) {
    std::error_code error;
    // Through a link to the file it names, which keeps the link. A path
    // with nothing at it yet is no link.
    std::filesystem::path target = path;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path, error))) {
        target = std::filesystem::canonical(path, error);
        if (error) return ftk::make_error("Can't follow " + path.string() + ": " + error.message());
    }

    std::filesystem::path beside = target;
    beside += ".fjedit-saving";
    {
        std::ofstream out(beside, std::ios::binary | std::ios::trunc);
        if (!out) return ftk::make_error("Can't write beside " + target.string());
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(beside, error);
            return ftk::make_error("Can't write " + target.string());
        }
    }
    if (std::filesystem::exists(target, error)) {
        const std::filesystem::perms kept = std::filesystem::status(target, error).permissions();
        if (!error) std::filesystem::permissions(beside, kept, error);
    }
    std::filesystem::rename(beside, target, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(beside, ignored);
        return ftk::make_error("Can't replace " + target.string() + ": " + error.message());
    }
    return {};
}

} // namespace fjedit
