#include "fjedit/core/vim/motions.hpp"

#include "fjedit/core/structure.hpp"
#include "fjedit/core/vim/walk.hpp"

#include "ftk/base/utf8.hpp"

#include <algorithm>
#include <string_view>

namespace fjedit::vim {

namespace {

bool continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// Where the character at `byte` of `line` ends; `byte` must be before the
// line's end.
std::size_t next_byte(std::string_view line, std::size_t byte) {
    (void)ftk::utf8::next(line, byte);
    return byte;
}

// Where the character before `byte` of `line` starts; `byte` must be past 0.
std::size_t previous_byte(std::string_view line, std::size_t byte) {
    --byte;
    while (byte > 0 && continuation(line[byte])) --byte;
    return byte;
}

} // namespace

Position word_start_after(const Buffer& buffer, Position at, std::size_t count, bool big, bool stop_at_line_end) {
    detail::Walk walk(buffer, at, big);
    (void)detail::forward_words(walk, count, stop_at_line_end);
    return walk.at();
}

Position word_end_after(const Buffer& buffer, Position at, std::size_t count, bool big, bool stay) {
    detail::Walk walk(buffer, at, big);
    (void)detail::end_of_words(walk, count, stay, false);
    return walk.at();
}

Position word_start_before(const Buffer& buffer, Position at, std::size_t count, bool big) {
    detail::Walk walk(buffer, at, big);
    (void)detail::back_words(walk, count);
    return walk.at();
}

Position word_end_before(const Buffer& buffer, Position at, std::size_t count, bool big) {
    detail::Walk walk(buffer, at, big);
    (void)detail::back_ends_of_words(walk, count);
    return walk.at();
}

Position first_non_blank(const Buffer& buffer, std::size_t line) {
    const std::string_view text = buffer.line(line);
    std::size_t byte = 0;
    while (byte < text.size() && (text[byte] == ' ' || text[byte] == '\t')) ++byte;
    return {.line = line, .byte = byte};
}

Position last_character(const Buffer& buffer, std::size_t line) {
    const std::string_view text = buffer.line(line);
    return {.line = line, .byte = text.empty() ? 0 : previous_byte(text, text.size())};
}

namespace {

// `}` forward, `{` back: Vim's paragraph motions, on empty lines.
Moved paragraph(const Buffer& buffer, Position at, std::size_t count, bool forward) {
    std::size_t line = at.line;
    for (; count > 0; --count) {
        bool passed_text = false;
        for (bool first = true;; first = false) {
            const bool empty = buffer.line(line).empty();
            if (!empty) passed_text = true;
            if (!first && passed_text && empty) break;
            const bool edge = forward ? line + 1 >= buffer.line_count() : line == 0;
            if (edge) {
                if (count > 1) return {.to = at, .kind = MotionKind::Exclusive};
                break;
            }
            line = forward ? line + 1 : line - 1;
        }
    }
    if (forward && line + 1 == buffer.line_count() && !buffer.line(line).empty()) {
        return {.to = last_character(buffer, line), .kind = MotionKind::Inclusive};
    }
    return {.to = {.line = line, .byte = 0}, .kind = MotionKind::Exclusive};
}

bool bracket(char c) {
    return c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}';
}

} // namespace

Moved paragraph_after(const Buffer& buffer, Position at, std::size_t count) {
    return paragraph(buffer, at, count, true);
}

Moved paragraph_before(const Buffer& buffer, Position at, std::size_t count) {
    return paragraph(buffer, at, count, false);
}

std::optional<Position> paired_bracket(const Buffer& buffer, const Syntax& syntax, Position at) {
    const std::string_view line = buffer.line(at.line);
    for (std::size_t byte = at.byte; byte < line.size(); ++byte) {
        if (!bracket(line[byte]) || in_string_or_comment(syntax, at.line, byte)) continue;
        const Position here{.line = at.line, .byte = byte};
        const std::optional<BracketPair> pair = matching_bracket(buffer, syntax, here);
        if (!pair) return std::nullopt;
        return pair->open == here ? pair->close : pair->open;
    }
    return std::nullopt;
}

std::optional<Position> find_in_line(const Buffer& buffer, Position at, const CharSearch& search, std::size_t count,
                                     bool again) {
    const std::string_view line = buffer.line(at.line);
    const std::string_view target = search.target;
    if (target.empty() || count == 0) return std::nullopt;
    std::size_t found = std::string_view::npos;

    if (search.forward) {
        std::size_t from = at.byte < line.size() ? next_byte(line, at.byte) : line.size();
        // Looking again once, a till's match right beside the caret is
        // passed.
        if (again && search.till && count == 1 && line.substr(from).starts_with(target)) from += target.size();
        for (; count > 0; --count) {
            found = line.find(target, from);
            if (found == std::string_view::npos) return std::nullopt;
            from = found + target.size();
        }
        return Position{.line = at.line, .byte = search.till ? previous_byte(line, found) : found};
    }

    // Back: matches that end at or before the caret's character.
    std::size_t before = std::min(at.byte, line.size());
    if (again && search.till && count == 1 && before > 0) {
        const std::size_t beside = previous_byte(line, before);
        if (line.substr(beside).starts_with(target)) before = beside;
    }
    for (; count > 0; --count) {
        if (before == 0) return std::nullopt;
        found = line.rfind(target, before - 1);
        if (found == std::string_view::npos) return std::nullopt;
        before = found;
    }
    return Position{.line = at.line, .byte = search.till ? found + target.size() : found};
}

Position left_in_line(const Buffer& buffer, Position at, std::size_t count) {
    const std::string_view line = buffer.line(at.line);
    std::size_t byte = std::min(at.byte, line.size());
    for (; count > 0 && byte > 0; --count) byte = previous_byte(line, byte);
    return {.line = at.line, .byte = byte};
}

Position right_in_line(const Buffer& buffer, Position at, std::size_t count) {
    const std::string_view line = buffer.line(at.line);
    std::size_t byte = std::min(at.byte, line.size());
    for (; count > 0 && byte < line.size(); --count) byte = next_byte(line, byte);
    return {.line = at.line, .byte = byte};
}

} // namespace fjedit::vim
