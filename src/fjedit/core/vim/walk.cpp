#include "fjedit/core/vim/walk.hpp"

#include "fjedit/core/motion.hpp"

#include "ftk/base/utf8.hpp"

#include <string_view>

namespace fjedit::vim::detail {

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

int Walk::kind() const {
    const std::string_view line = buffer_.line(at_.line);
    if (at_.byte >= line.size()) return BLANK;
    std::size_t byte = at_.byte;
    const CharKind kind = char_kind(ftk::utf8::next(line, byte));
    if (kind == CharKind::Space) return BLANK;
    if (big_) return PUNCTUATION;
    return kind == CharKind::Word ? WORD : PUNCTUATION;
}

int Walk::forward() {
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

int Walk::back() {
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

int Walk::forward_past_line_end() {
    const int step = forward();
    if (step != ALONG && step != AT_TEXT_EDGE && at_.byte != 0) return forward();
    return step;
}

int Walk::back_past_line_end() {
    const int step = back();
    if (step == ONTO_NEXT_LINE && at_.byte != 0) return back();
    return step;
}

bool Walk::skip(int kind, bool on) {
    while (this->kind() == kind) {
        if ((on ? forward() : back()) == AT_TEXT_EDGE) return true;
    }
    return false;
}

bool forward_words(Walk& walk, std::size_t count, bool stop_at_line_end) {
    for (; count > 0; --count) {
        // A step that leaves the line ends an operator's last count there.
        const bool stops = stop_at_line_end && count == 1;
        const auto ends = [&](int step) { return step == AT_TEXT_EDGE || (step != ALONG && stops); };
        const int from = walk.kind();
        const bool last_line = walk.on_last_line();
        const int step = walk.forward();
        if (step == AT_TEXT_EDGE || (step != ALONG && last_line)) return false;
        if (ends(step)) return true;
        if (from != BLANK) {
            while (walk.kind() == from) {
                if (ends(walk.forward())) return true;
            }
        }
        while (walk.kind() == BLANK && !walk.on_empty_line()) {
            if (ends(walk.forward())) return true;
        }
    }
    return true;
}

bool end_of_words(Walk& walk, std::size_t count, bool stay, bool stop_at_empty_line) {
    for (; count > 0; --count) {
        const int from = walk.kind();
        if (walk.forward() == AT_TEXT_EDGE) return false;
        bool on_empty_line = false;
        if (walk.kind() == from && from != BLANK) {
            // Inside a word: on to its end.
            if (walk.skip(from, true)) return false;
        } else if (!stay || from == BLANK) {
            // At a word's end: past the blanks, to the next word's end.
            while (walk.kind() == BLANK) {
                if (stop_at_empty_line && walk.on_empty_line()) {
                    on_empty_line = true;
                    break;
                }
                if (walk.forward() == AT_TEXT_EDGE) return false;
            }
            if (!on_empty_line && walk.skip(walk.kind(), true)) return false;
        }
        if (!on_empty_line) (void)walk.back();
        stay = false;
    }
    return true;
}

bool back_words(Walk& walk, std::size_t count) {
    for (; count > 0; --count) {
        if (walk.back() == AT_TEXT_EDGE) return false;
        bool empty_line = false;
        while (walk.kind() == BLANK) {
            if (walk.on_empty_line()) {
                empty_line = true;
                break;
            }
            if (walk.back() == AT_TEXT_EDGE) return true;
        }
        if (empty_line) continue;
        if (walk.skip(walk.kind(), false)) return true;
        (void)walk.forward();
    }
    return true;
}

bool back_ends_of_words(Walk& walk, std::size_t count) {
    for (; count > 0; --count) {
        const int from = walk.kind();
        if (walk.back() == AT_TEXT_EDGE) return false;
        if (from != BLANK) {
            while (walk.kind() == from) {
                if (walk.back() == AT_TEXT_EDGE) return true;
            }
        }
        while (walk.kind() == BLANK && !walk.on_empty_line()) {
            if (walk.back() == AT_TEXT_EDGE) return true;
        }
    }
    return true;
}

} // namespace fjedit::vim::detail
