#include "fjedit/core/workspace.hpp"

#include "fjedit/core/language.hpp"
#include "fjedit/core/text_file.hpp"

#include <algorithm>
#include <string>
#include <system_error>

namespace fjedit {

namespace {

// `path` whole: through the links and dots in it, as far as there is a file.
std::filesystem::path whole(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (error) return path.lexically_normal();
    std::filesystem::path named = std::filesystem::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : named;
}

} // namespace

Workspace::Workspace(std::filesystem::path root) : root_(whole(root)) {}

std::filesystem::path Workspace::name(const std::filesystem::path& path) const {
    return whole(path.is_absolute() ? path : root_ / path);
}

std::optional<Workspace::Stamp> Workspace::stamp_of(const std::filesystem::path& path) {
    std::error_code error;
    const auto written = std::filesystem::last_write_time(path, error);
    if (error) return std::nullopt;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return std::nullopt;
    return Stamp{.written = written, .size = size};
}

Workspace::OpenFile* Workspace::find_open(const std::filesystem::path& path) {
    const std::filesystem::path named = name(path);
    const auto found = std::ranges::find(open_, named, &OpenFile::path);
    return found == open_.end() ? nullptr : &*found;
}

const Workspace::OpenFile* Workspace::find_open(const std::filesystem::path& path) const {
    const std::filesystem::path named = name(path);
    const auto found = std::ranges::find(open_, named, &OpenFile::path);
    return found == open_.end() ? nullptr : &*found;
}

ftk::Result<Document*> Workspace::open(const std::filesystem::path& path) {
    if (OpenFile* file = find_open(path)) return file->document.get();
    const std::filesystem::path named = name(path);
    const ftk::Result<std::string> bytes = read_text_file(named);
    if (!bytes) return ftk::make_error(bytes.error());
    auto document = std::make_unique<Document>(*bytes, language_for(named.filename().string()));
    open_.push_back({.path = named, .document = std::move(document), .known = stamp_of(named), .named = {}});
    return open_.back().document.get();
}

Document* Workspace::find(const std::filesystem::path& path) const {
    const OpenFile* file = find_open(path);
    return file == nullptr ? nullptr : file->document.get();
}

std::vector<std::filesystem::path> Workspace::files() const {
    std::vector<std::filesystem::path> out;
    out.reserve(open_.size());
    for (const OpenFile& file : open_) out.push_back(file.path);
    return out;
}

bool Workspace::unsaved(const std::filesystem::path& path) const {
    const OpenFile* file = find_open(path);
    return file != nullptr && !file->document->history().at_saved();
}

ftk::Result<> Workspace::save(const std::filesystem::path& path) {
    OpenFile* file = find_open(path);
    if (file == nullptr) return ftk::make_error(path.string() + " isn't open");
    if (auto written = write_text_file(file->path, file->document->buffer().text()); !written) return written;
    file->document->history().mark_saved();
    file->known = stamp_of(file->path);
    file->named.reset();
    return {};
}

void Workspace::close(const std::filesystem::path& path) {
    const std::filesystem::path named = name(path);
    std::erase_if(open_, [&](const OpenFile& file) { return file.path == named; });
}

std::vector<std::filesystem::path> Workspace::changed_on_disk() {
    std::vector<std::filesystem::path> changed;
    for (OpenFile& file : open_) {
        const std::optional<Stamp> now = stamp_of(file.path);
        if (!now || now == file.known || now == file.named) continue;
        file.named = now;
        changed.push_back(file.path);
    }
    return changed;
}

void Workspace::replace(Document& document, std::string_view bytes) {
    const Buffer fresh(bytes);
    const Buffer& held = document.buffer();

    // The lines alike at the start and at the end are left; the ones between
    // are replaced. Each side keeps one line between, so the edit has a span.
    const std::size_t shorter = std::min(held.line_count(), fresh.line_count());
    std::size_t head = 0;
    while (head + 1 < shorter && held.line(head) == fresh.line(head)) ++head;
    std::size_t tail = 0;
    while (head + tail + 1 < shorter &&
           held.line(held.line_count() - 1 - tail) == fresh.line(fresh.line_count() - 1 - tail)) {
        ++tail;
    }
    const std::size_t held_last = held.line_count() - 1 - tail;
    std::string text;
    for (std::size_t line = head; line < fresh.line_count() - tail; ++line) {
        if (line > head) text += '\n';
        text += fresh.line(line);
    }
    const Span replaced{.from = {head, 0}, .to = {held_last, held.line(held_last).size()}};
    if (held.text(replaced) != text) {
        const Edit edit{.changes = {{.span = replaced, .text = text}}};
        const Selections before(Selection::at(replaced.from));
        Edit undo = document.apply(edit);
        const Selections after(Selection::at(spans_after(edit).front().to));
        document.history().record({.made = edit, .undo = std::move(undo), .before = before, .after = after,
                                   .kind = EditKind::Other});
    }
    document.set_file_format(fresh.line_ending(), fresh.byte_order_mark());
}

ftk::Result<> Workspace::reload(const std::filesystem::path& path) {
    OpenFile* file = find_open(path);
    if (file == nullptr) return ftk::make_error(path.string() + " isn't open");
    const ftk::Result<std::string> bytes = read_text_file(file->path);
    if (!bytes) return ftk::make_error(bytes.error());
    replace(*file->document, *bytes);
    file->document->history().mark_saved();
    file->known = stamp_of(file->path);
    file->named.reset();
    return {};
}

ftk::Result<> Workspace::set_text(const std::filesystem::path& path, std::string_view bytes) {
    OpenFile* file = find_open(path);
    if (file == nullptr) return ftk::make_error(path.string() + " isn't open");
    replace(*file->document, bytes);
    return {};
}

} // namespace fjedit
