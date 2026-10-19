#include "fjedit/core/vim/motions.hpp"

#include "fjedit/core/motion.hpp"
#include "fjedit/core/structure.hpp"

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

// The classes Vim's word motions tell characters apart by.
constexpr int BLANK = 0;
constexpr int PUNCTUATION = 1;
constexpr int WORD = 2;

// What a step of a walk did.
constexpr int ALONG = 0;
constexpr int ONTO_NEXT_LINE = 1;
constexpr int ONTO_LINE_END = 2;
constexpr int AT_TEXT_EDGE = -1;

// A place in the text as Vim's word motions walk it, a character at a time
// with each line's end a place of its own, where its break is.
class Walk {
public:
    Walk(const Buffer& buffer, Position at, bool big) : buffer_(buffer), at_(at), big_(big) {}

    [[nodiscard]] Position at() const { return at_; }

    // BLANK for a space, a tab or a line's end; for a WORD, PUNCTUATION
    // for anything else; otherwise WORD or PUNCTUATION by the character.
    [[nodiscard]] int kind() const {
        const std::string_view line = buffer_.line(at_.line);
        if (at_.byte >= line.size()) return BLANK;
        std::size_t byte = at_.byte;
        const CharKind kind = char_kind(ftk::utf8::next(line, byte));
        if (kind == CharKind::Space) return BLANK;
        if (big_) return PUNCTUATION;
        return kind == CharKind::Word ? WORD : PUNCTUATION;
    }

    [[nodiscard]] bool on_empty_line() const { return at_.byte == 0 && buffer_.line(at_.line).empty(); }
    [[nodiscard]] bool on_last_line() const { return at_.line + 1 == buffer_.line_count(); }

    // A step on: ALONG the line, ONTO_LINE_END, or ONTO_NEXT_LINE's start;
    // AT_TEXT_EDGE at the last line's end, where it stays.
    int forward() {
        const std::string_view line = buffer_.line(at_.line);
        if (at_.byte < line.size()) {
            at_.byte = next_byte(line, at_.byte);
            return at_.byte < line.size() ? ALONG : ONTO_LINE_END;
        }
        if (at_.line + 1 < buffer_.line_count()) {
            at_ = {.line = at_.line + 1, .byte = 0};
            return ONTO_NEXT_LINE;
        }
        return AT_TEXT_EDGE;
    }

    // A step back: ALONG the line, or onto the end of the line before
    // (ONTO_NEXT_LINE, the next line back); AT_TEXT_EDGE at the text's
    // start, where it stays.
    int back() {
        if (at_.byte > 0) {
            at_.byte = previous_byte(buffer_.line(at_.line), at_.byte);
            return ALONG;
        }
        if (at_.line > 0) {
            --at_.line;
            at_.byte = buffer_.line(at_.line).size();
            return ONTO_NEXT_LINE;
        }
        return AT_TEXT_EDGE;
    }

    // Steps on, or back, over places of `kind`; true when the text's edge
    // came first.
    bool skip(int kind, bool on) {
        while (this->kind() == kind) {
            if ((on ? forward() : back()) == AT_TEXT_EDGE) return true;
        }
        return false;
    }

private:
    const Buffer& buffer_;
    Position at_;
    bool big_;
};

} // namespace

Position word_start_after(const Buffer& buffer, Position at, std::size_t count, bool big, bool stop_at_line_end) {
    Walk walk(buffer, at, big);
    for (; count > 0; --count) {
        // A step that leaves the line ends an operator's last count there.
        const bool stops = stop_at_line_end && count == 1;
        const auto ends = [&](int step) { return step == AT_TEXT_EDGE || (step != ALONG && stops); };
        const int from = walk.kind();
        const bool last_line = walk.on_last_line();
        const int step = walk.forward();
        if (step == AT_TEXT_EDGE || (step != ALONG && last_line) || ends(step)) break;
        bool ended = false;
        if (from != BLANK) {
            while (!ended && walk.kind() == from) ended = ends(walk.forward());
        }
        while (!ended && walk.kind() == BLANK && !walk.on_empty_line()) ended = ends(walk.forward());
        if (ended) break;
    }
    return walk.at();
}

Position word_end_after(const Buffer& buffer, Position at, std::size_t count, bool big, bool stay) {
    Walk walk(buffer, at, big);
    for (; count > 0; --count) {
        const int from = walk.kind();
        if (walk.forward() == AT_TEXT_EDGE) break;
        if (walk.kind() == from && from != BLANK) {
            // Inside a word: on to its end.
            if (walk.skip(from, true)) break;
        } else if (!stay || from == BLANK) {
            // At a word's end: past the blanks, to the next word's end.
            bool edge = false;
            while (!edge && walk.kind() == BLANK) edge = walk.forward() == AT_TEXT_EDGE;
            if (edge || walk.skip(walk.kind(), true)) break;
        }
        (void)walk.back();
        stay = false;
    }
    return walk.at();
}

Position word_start_before(const Buffer& buffer, Position at, std::size_t count, bool big) {
    Walk walk(buffer, at, big);
    for (; count > 0; --count) {
        if (walk.back() == AT_TEXT_EDGE) break;
        bool empty_line = false;
        bool edge = false;
        while (walk.kind() == BLANK) {
            if (walk.on_empty_line()) {
                empty_line = true;
                break;
            }
            if (walk.back() == AT_TEXT_EDGE) {
                edge = true;
                break;
            }
        }
        if (edge) break;
        if (empty_line) continue;
        if (walk.skip(walk.kind(), false)) break;
        (void)walk.forward();
    }
    return walk.at();
}

Position word_end_before(const Buffer& buffer, Position at, std::size_t count, bool big) {
    Walk walk(buffer, at, big);
    for (; count > 0; --count) {
        const int from = walk.kind();
        if (walk.back() == AT_TEXT_EDGE) break;
        bool edge = false;
        if (from != BLANK) {
            while (!edge && walk.kind() == from) edge = walk.back() == AT_TEXT_EDGE;
        }
        while (!edge && walk.kind() == BLANK && !walk.on_empty_line()) edge = walk.back() == AT_TEXT_EDGE;
        if (edge) break;
    }
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
