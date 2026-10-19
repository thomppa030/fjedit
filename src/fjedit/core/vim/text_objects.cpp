#include "fjedit/core/vim/text_objects.hpp"

#include "fjedit/core/structure.hpp"
#include "fjedit/core/vim/motions.hpp"
#include "fjedit/core/vim/walk.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace fjedit::vim {

namespace {

using detail::Walk;

// Lines the angle brackets' search goes through before it gives up, as the
// structure's does for the other brackets.
constexpr std::size_t SEARCH_LINES = 10000;

bool blank(char c) {
    return c == ' ' || c == '\t';
}

// Back to the start of the word, or of the blanks, the walk is on, within
// its line (Vim's back_in_line()).
void back_in_line(Walk& walk) {
    const int kind = walk.kind();
    while (walk.at().byte > 0) {
        (void)walk.back();
        if (walk.kind() != kind) {
            (void)walk.forward();
            break;
        }
    }
}

// `iw` `aw` `iW` `aW`, as Vim's current_word() takes them.
std::optional<Taken> words(const Buffer& buffer, Position at, std::size_t count, bool include, bool big) {
    Walk walk(buffer, at, big);
    back_in_line(walk);
    const Position start = walk.at();
    // An `a` that ends on a word, with no blanks after it, takes those
    // before it.
    bool blanks_after = false;
    bool inclusive = true;
    if ((walk.kind() == detail::BLANK) == include) {
        if (!detail::end_of_words(walk, 1, true, true)) return std::nullopt;
    } else {
        (void)detail::forward_words(walk, 1, true);
        if (walk.at().byte == 0) {
            (void)walk.back_past_line_end();
        } else {
            (void)walk.back();
        }
        blanks_after = include;
    }
    for (--count; count > 0; --count) {
        inclusive = true;
        if (walk.forward_past_line_end() == detail::AT_TEXT_EDGE) return std::nullopt;
        if (include != (walk.kind() == detail::BLANK)) {
            if (!detail::forward_words(walk, 1, true) && count > 1) return std::nullopt;
            // Just past a line break: the next line's first character isn't taken.
            if (walk.at().byte == 0) {
                inclusive = false;
            } else {
                (void)walk.back();
            }
        } else if (!detail::end_of_words(walk, 1, true, true)) {
            return std::nullopt;
        }
    }

    Position from = start;
    if (blanks_after && (walk.kind() != detail::BLANK || (walk.at().byte == 0 && !inclusive))) {
        // The blanks before the word instead, unless they indent the line.
        Walk before(buffer, start, big);
        if (before.at().byte > 0) {
            (void)before.back();
            back_in_line(before);
            if (before.kind() == detail::BLANK && before.at().byte > 0) from = before.at();
        }
    }
    const Position to = inclusive ? right_in_line(buffer, walk.at(), 1) : walk.at();
    return Taken{.span = {from, to}, .linewise = false};
}

// Whether `at`'s character and every one before it on its line are blanks
// (Vim's inindent(1)).
bool in_indent(const Buffer& buffer, Position at) {
    const std::string_view line = buffer.line(at.line);
    std::size_t blanks = 0;
    while (blanks < line.size() && blank(line[blanks])) ++blanks;
    return blanks >= at.byte + 1;
}

// The `<` before `at` that nothing between closes, counted plainly as Vim
// counts angle brackets, which the structure doesn't pair.
std::optional<Position> angle_opener(const Buffer& buffer, Position at) {
    std::size_t depth = 0;
    std::size_t line = at.line;
    for (std::size_t searched = 0; searched < SEARCH_LINES; ++searched) {
        const std::string_view text = buffer.line(line);
        for (std::size_t byte = line == at.line ? std::min(at.byte, text.size()) : text.size(); byte-- > 0;) {
            if (text[byte] == '>') {
                ++depth;
            } else if (text[byte] == '<') {
                if (depth == 0) return Position{.line = line, .byte = byte};
                --depth;
            }
        }
        if (line-- == 0) break;
    }
    return std::nullopt;
}

// The `>` that closes the `<` at `opener`, counted the same way.
std::optional<Position> angle_closer(const Buffer& buffer, Position opener) {
    std::size_t depth = 0;
    for (std::size_t line = opener.line; line < buffer.line_count() && line - opener.line < SEARCH_LINES; ++line) {
        const std::string_view text = buffer.line(line);
        for (std::size_t byte = line == opener.line ? opener.byte + 1 : 0; byte < text.size(); ++byte) {
            if (text[byte] == '<') {
                ++depth;
            } else if (text[byte] == '>') {
                if (depth == 0) return Position{.line = line, .byte = byte};
                --depth;
            }
        }
    }
    return std::nullopt;
}

// The blocks, as Vim's current_block() takes them.
std::optional<Taken> block(const Buffer& buffer, const Syntax& syntax, Position at, std::size_t count, bool include,
                           char open) {
    const std::string_view line = buffer.line(at.line);
    Position from = at;
    // A brace's block from its line's indent; on the opener, its own block.
    if (open == '{') {
        while (in_indent(buffer, from) && from.byte < line.size()) from = right_in_line(buffer, from, 1);
    }
    if (from.byte < line.size() && line[from.byte] == open) ++from.byte;

    std::optional<Position> opener;
    for (Position search = from; count > 0; --count) {
        const std::optional<Position> found =
            open == '<' ? angle_opener(buffer, search) : enclosing_opener(buffer, &syntax, search, open);
        if (!found) return std::nullopt;
        opener = found;
        search = *found;
    }
    std::optional<Position> closer;
    if (open == '<') {
        closer = angle_closer(buffer, *opener);
    } else if (const std::optional<BracketPair> pair = matching_bracket(buffer, syntax, *opener);
               pair && pair->open == *opener) {
        closer = pair->close;
    }
    if (!closer) return std::nullopt;
    if (include) return Taken{.span = {*opener, right_in_line(buffer, *closer, 1)}, .linewise = false};

    // Inside: from past the opener (and its line's end), to before the
    // closer, past the indent it stands in, so `di{` leaves the braces'
    // lines.
    Walk start(buffer, *opener, false);
    (void)start.forward_past_line_end();
    Walk end(buffer, *closer, false);
    bool at_line_start = closer->byte == 0;
    (void)end.back_past_line_end();
    while (in_indent(buffer, end.at())) {
        at_line_start = true;
        if (end.back_past_line_end() != detail::ALONG) break;
    }
    if (at_line_start) {
        (void)end.forward_past_line_end();
        return Taken{.span = {start.at(), end.at()}, .linewise = false};
    }
    if (start.at() <= end.at()) {
        return Taken{.span = {start.at(), right_in_line(buffer, end.at(), 1)}, .linewise = false};
    }
    return Taken{.span = {start.at(), start.at()}, .linewise = false};
}

// Where `quote` next stands on `line` from `from`, passing the character
// after a backslash with `escapes`; npos when nowhere.
std::size_t next_quote(std::string_view line, std::size_t from, char quote, bool escapes) {
    for (std::size_t byte = from; byte < line.size(); ++byte) {
        if (escapes && line[byte] == '\\') {
            ++byte;
        } else if (line[byte] == quote) {
            return byte;
        }
    }
    return std::string_view::npos;
}

// Where `quote` last stands on `line` before `from`, passing one an odd run
// of backslashes escapes; 0 when nowhere, which the caller tells from a
// quote there by looking.
std::size_t previous_quote(std::string_view line, std::size_t from, char quote) {
    std::size_t byte = from;
    while (byte > 0) {
        --byte;
        std::size_t escapes = 0;
        while (byte - escapes > 0 && line[byte - escapes - 1] == '\\') ++escapes;
        if (escapes % 2 == 1) {
            byte -= escapes;
        } else if (line[byte] == quote) {
            break;
        }
    }
    return byte;
}

// The quoted text, as Vim's current_quote() takes it.
std::optional<Taken> quoted(const Buffer& buffer, Position at, std::size_t count, bool include, char quote) {
    const std::string_view line = buffer.line(at.line);
    const std::size_t on = at.byte;
    std::size_t start = 0;
    std::size_t end = 0;
    if (on < line.size() && line[on] == quote) {
        // On a quote: whether it opens or closes is counted from the
        // line's start.
        std::size_t from = 0;
        for (;;) {
            start = next_quote(line, from, quote, false);
            if (start == std::string_view::npos || start > on) return std::nullopt;
            end = next_quote(line, start + 1, quote, true);
            if (end == std::string_view::npos) return std::nullopt;
            if (start <= on && on <= end) break;
            from = end + 1;
        }
    } else {
        // The quote before, or with none, the first after.
        start = previous_quote(line, on, quote);
        if (start >= line.size() || line[start] != quote) {
            start = next_quote(line, start, quote, false);
            if (start == std::string_view::npos) return std::nullopt;
        }
        end = next_quote(line, start + 1, quote, true);
        if (end == std::string_view::npos) return std::nullopt;
    }
    if (include) {
        if (end + 1 < line.size() && blank(line[end + 1])) {
            while (end + 1 < line.size() && blank(line[end + 1])) ++end;
        } else {
            while (start > 0 && blank(line[start - 1])) --start;
        }
    }
    // Two of an `i` take the quotes, as an `a` does, but no blanks.
    if (!include && count < 2) ++start;
    const std::size_t to = include || count > 1 ? end + 1 : end;
    return Taken{.span = {{at.line, start}, {at.line, to}}, .linewise = false};
}

// The paragraphs, as Vim's current_par() takes them: runs of lines with
// text, or of blank lines.
std::optional<Taken> paragraphs(const Buffer& buffer, Position at, std::size_t count, bool include) {
    const auto white = [&](std::ptrdiff_t line) {
        const std::string_view text = buffer.line(static_cast<std::size_t>(line));
        for (const char c : text) {
            if (!blank(c)) return false;
        }
        return true;
    };
    const auto last = static_cast<std::ptrdiff_t>(buffer.line_count()) - 1;
    auto start = static_cast<std::ptrdiff_t>(at.line);
    const bool white_in_front = white(start);
    while (start > 0 && white(start - 1) == white_in_front) --start;

    std::ptrdiff_t end = start;
    while (end <= last && white(end)) ++end;
    --end;
    std::size_t left = count;
    if (!include && white_in_front) --left;
    while (left > 0) {
        --left;
        if (end == last) return std::nullopt;
        const bool blanks_next = !include && white(end + 1);
        if (include || !blanks_next) {
            ++end;
            while (end < last && !white(end + 1)) ++end;
        }
        if (left == 0 && white_in_front && include) break;
        if (include || blanks_next) {
            while (end < last && white(end + 1)) ++end;
        }
    }
    // Without blank lines after it, an `ap` takes those before it.
    if (!white_in_front && !white(end) && include) {
        while (start > 0 && white(start - 1)) --start;
    }
    const auto first = static_cast<std::size_t>(start);
    const auto through = static_cast<std::size_t>(end);
    return Taken{.span = {{first, 0}, {through, buffer.line(through).size()}}, .linewise = true};
}

} // namespace

std::optional<Taken> text_object(const Buffer& buffer, const Syntax& syntax, Position at, std::string_view name,
                                 std::size_t count) {
    if (name.size() != 2 || (name[0] != 'i' && name[0] != 'a') || count == 0) return std::nullopt;
    const bool include = name[0] == 'a';
    switch (name[1]) {
        case 'w': return words(buffer, at, count, include, false);
        case 'W': return words(buffer, at, count, include, true);
        case '(': case ')': case 'b': return block(buffer, syntax, at, count, include, '(');
        case '{': case '}': case 'B': return block(buffer, syntax, at, count, include, '{');
        case '[': case ']': return block(buffer, syntax, at, count, include, '[');
        case '<': case '>': return block(buffer, syntax, at, count, include, '<');
        case '"': case '\'': case '`': return quoted(buffer, at, count, include, name[1]);
        case 'p': return paragraphs(buffer, at, count, include);
        default: return std::nullopt;
    }
}

} // namespace fjedit::vim
