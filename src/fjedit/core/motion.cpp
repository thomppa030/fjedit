#include "fjedit/core/motion.hpp"

#include "ftk/base/utf8.hpp"

namespace fjedit {

namespace {

bool continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// The character starting at `byte` of `line`; `byte` must be before its end.
uint32_t char_at(std::string_view line, std::size_t byte) {
    return ftk::utf8::next(line, byte);
}

// Where the character before `byte` of `line` starts; `byte` must be past 0.
std::size_t previous(std::string_view line, std::size_t byte) {
    --byte;
    while (byte > 0 && continuation(line[byte])) --byte;
    return byte;
}

// Where the character at `byte` of `line` ends.
std::size_t next(std::string_view line, std::size_t byte) {
    (void)ftk::utf8::next(line, byte);
    return byte;
}

} // namespace

CharKind char_kind(uint32_t c) {
    if (c == ' ' || c == '\t') return CharKind::Space;
    if (c >= 0x80) return CharKind::Word;
    const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    const bool digit = c >= '0' && c <= '9';
    return letter || digit || c == '_' ? CharKind::Word : CharKind::Punctuation;
}

Position char_right(const Buffer& buffer, Position at) {
    const std::string_view line = buffer.line(at.line);
    if (at.byte < line.size()) return {.line = at.line, .byte = next(line, at.byte)};
    if (at.line + 1 < buffer.line_count()) return {.line = at.line + 1, .byte = 0};
    return at;
}

Position char_left(const Buffer& buffer, Position at) {
    if (at.byte > 0) return {.line = at.line, .byte = previous(buffer.line(at.line), at.byte)};
    if (at.line > 0) return {.line = at.line - 1, .byte = buffer.line(at.line - 1).size()};
    return at;
}

Position word_right(const Buffer& buffer, Position at) {
    const std::string_view line = buffer.line(at.line);
    if (at.byte == line.size()) return char_right(buffer, at);

    std::size_t byte = at.byte;
    while (byte < line.size() && char_kind(char_at(line, byte)) == CharKind::Space) byte = next(line, byte);
    if (byte < line.size()) {
        const CharKind kind = char_kind(char_at(line, byte));
        while (byte < line.size() && char_kind(char_at(line, byte)) == kind) byte = next(line, byte);
    }
    return {.line = at.line, .byte = byte};
}

Position word_left(const Buffer& buffer, Position at) {
    if (at.byte == 0) return char_left(buffer, at);
    const std::string_view line = buffer.line(at.line);

    std::size_t byte = at.byte;
    auto kind_before = [&](std::size_t b) { return char_kind(char_at(line, previous(line, b))); };
    while (byte > 0 && kind_before(byte) == CharKind::Space) byte = previous(line, byte);
    if (byte > 0) {
        const CharKind kind = kind_before(byte);
        while (byte > 0 && kind_before(byte) == kind) byte = previous(line, byte);
    }
    return {.line = at.line, .byte = byte};
}

Position line_start(const Buffer& buffer, Position at) {
    const std::string_view line = buffer.line(at.line);
    std::size_t first = 0;
    while (first < line.size() && (line[first] == ' ' || line[first] == '\t')) ++first;
    return {.line = at.line, .byte = at.byte == first ? 0 : first};
}

Position line_end(const Buffer& buffer, Position at) {
    return {.line = at.line, .byte = buffer.line(at.line).size()};
}

Position lines_up(const Buffer& buffer, const Layout& layout, Position at, float x, std::size_t lines) {
    const std::optional<std::size_t> line = layout.line_above(buffer, at.line, lines);
    if (!line) return {.line = 0, .byte = 0};
    return layout.position_at(buffer, *line, x);
}

Position lines_down(const Buffer& buffer, const Layout& layout, Position at, float x, std::size_t lines) {
    const std::optional<std::size_t> line = layout.line_below(buffer, at.line, lines);
    if (!line) return buffer.end();
    return layout.position_at(buffer, *line, x);
}

} // namespace fjedit
