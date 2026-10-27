#include "fjedit/core/buffer.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>

namespace fjedit {

namespace {

bool continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// How many breaks of each kind split_lines() found.
struct Breaks {
    std::size_t lf{0};
    std::size_t crlf{0};
};

// `text` cut into lines at its breaks (\n, \r\n or a lone \r), without them.
// A text ending in a break ends in an empty line.
std::vector<std::string_view> split_lines(std::string_view text, Breaks* breaks = nullptr) {
    std::vector<std::string_view> lines;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c != '\n' && c != '\r') continue;
        lines.push_back(text.substr(begin, i - begin));
        if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            if (breaks != nullptr) ++breaks->crlf;
            ++i;
        } else if (c == '\n' && breaks != nullptr) {
            ++breaks->lf;
        }
        begin = i + 1;
    }
    lines.push_back(text.substr(begin));
    return lines;
}

// Where `text` ends when it is put in at `from`: its breaks, as split_lines()
// counts them, move the end down and the bytes after the last one along.
Position end_of(Position from, std::string_view text) {
    std::size_t breaks = 0;
    std::size_t last = 0; // where the text's last line begins
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c != '\n' && c != '\r') continue;
        if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
        ++breaks;
        last = i + 1;
    }
    if (breaks == 0) return {.line = from.line, .byte = from.byte + text.size()};
    return {.line = from.line + breaks, .byte = text.size() - last};
}

std::size_t moved(std::size_t value, std::ptrdiff_t by) {
    return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(value) + by);
}

// How the text after the changes walked so far has moved, walking an edit's
// changes in order: every line by the lines they added, and the rest of the
// line the last one ended on along by the bytes its new text took up there.
struct Shift {
    std::ptrdiff_t lines{0};
    std::size_t line{std::numeric_limits<std::size_t>::max()};
    std::ptrdiff_t bytes{0};

    // Where `at`, after every change walked so far and before the next, is now.
    [[nodiscard]] Position operator()(Position at) const {
        return {.line = moved(at.line, lines), .byte = at.line == line ? moved(at.byte, bytes) : at.byte};
    }

    // Past a change whose span `before` became `after`.
    void past(Span before, Span after) {
        lines += static_cast<std::ptrdiff_t>(after.to.line - after.from.line) -
                 static_cast<std::ptrdiff_t>(before.to.line - before.from.line);
        line = before.to.line;
        bytes = static_cast<std::ptrdiff_t>(after.to.byte) - static_cast<std::ptrdiff_t>(before.to.byte);
    }
};

} // namespace

std::vector<Span> spans_after(const Edit& edit) {
    std::vector<Span> spans;
    spans.reserve(edit.changes.size());
    Shift shift;
    for (const Change& change : edit.changes) {
        const Position from = shift(change.span.from);
        const Span after{.from = from, .to = end_of(from, change.text)};
        spans.push_back(after);
        shift.past(change.span, after);
    }
    return spans;
}

Position map(const Edit& edit, Position at, Bias bias) {
    const std::vector<Span> after = spans_after(edit);
    // Whether the change after the i-th one begins at `at` as well.
    auto next_here = [&](std::size_t i) {
        return i + 1 < edit.changes.size() && edit.changes[i + 1].span.from == at;
    };

    Shift shift;
    for (std::size_t i = 0; i < edit.changes.size(); ++i) {
        const Span before = edit.changes[i].span;
        if (at < before.from) break;
        if (before.empty() ? at == before.from : at < before.to) {
            // Where text went in, or inside text replaced, the bias decides,
            // except at a replaced span's start, which stays before it. Past
            // inserted text, a change going in at the same place comes next.
            if (!before.empty() && at == before.from) return after[i].from;
            if (bias == Bias::Before) return after[i].from;
            if (!(before.empty() && next_here(i))) return after[i].to;
        } else if (at == before.to && !next_here(i)) {
            // At the end of replaced text: after the new text.
            return after[i].to;
        }
        shift.past(before, after[i]);
    }
    return shift(at);
}

Buffer::Buffer() : lines_(1) {}

Buffer::Buffer(std::string_view text) {
    if (text.starts_with(BYTE_ORDER_MARK)) {
        byte_order_mark_ = true;
        text.remove_prefix(BYTE_ORDER_MARK.size());
    }

    Breaks breaks;
    const std::vector<std::string_view> lines = split_lines(text, &breaks);
    lines_.assign(lines.begin(), lines.end());

    // A lone \r breaks a line but votes for neither: it is too rare to be
    // what a file means to keep.
    line_ending_ = breaks.crlf > breaks.lf ? LineEnding::CrLf : LineEnding::Lf;
}

Position Buffer::end() const {
    return {.line = lines_.size() - 1, .byte = lines_.back().size()};
}

std::string Buffer::text(Span span) const {
    const Position from = span.from;
    const Position to = span.to;
    if (from.line == to.line) return lines_[from.line].substr(from.byte, to.byte - from.byte);

    std::string out = lines_[from.line].substr(from.byte);
    for (std::size_t i = from.line + 1; i < to.line; ++i) {
        out += '\n';
        out += lines_[i];
    }
    out += '\n';
    out.append(lines_[to.line], 0, to.byte);
    return out;
}

std::string Buffer::text() const {
    const std::string_view separator = line_ending_ == LineEnding::CrLf ? "\r\n" : "\n";
    std::size_t size = byte_order_mark_ ? BYTE_ORDER_MARK.size() : 0;
    for (const std::string& line : lines_) size += line.size() + separator.size();

    std::string out;
    out.reserve(size);
    if (byte_order_mark_) out += BYTE_ORDER_MARK;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        if (i > 0) out += separator;
        out += lines_[i];
    }
    return out;
}

Position Buffer::clamp(Position at) const {
    at.line = std::min(at.line, lines_.size() - 1);
    const std::string& line = lines_[at.line];
    at.byte = std::min(at.byte, line.size());
    while (at.byte > 0 && at.byte < line.size() && continuation(line[at.byte])) --at.byte;
    return at;
}

Edit Buffer::apply(const Edit& edit) {
    for (std::size_t i = 0; i < edit.changes.size(); ++i) {
        [[maybe_unused]] const Span span = edit.changes[i].span;
        assert(clamp(span.from) == span.from && clamp(span.to) == span.to);
        assert(span.from <= span.to);
        assert(i == 0 || edit.changes[i - 1].span.to <= span.from);
    }

    const std::vector<Span> after = spans_after(edit);
    Edit undo;
    undo.changes.reserve(edit.changes.size());
    for (std::size_t i = 0; i < edit.changes.size(); ++i) {
        undo.changes.push_back({.span = after[i], .text = text(edit.changes[i].span)});
    }

    // From the last change back, so the spans still to go are where they were.
    for (std::size_t i = edit.changes.size(); i-- > 0;) replace(edit.changes[i].span, edit.changes[i].text);
    ++version_;
    return undo;
}

void Buffer::replace(Span span, std::string_view text) {
    const std::vector<std::string_view> pieces = split_lines(text);
    const std::string tail = lines_[span.to.line].substr(span.to.byte);

    std::string& first = lines_[span.from.line];
    first.resize(span.from.byte);
    first += pieces.front();

    const auto at = lines_.begin() + static_cast<std::ptrdiff_t>(span.from.line) + 1;
    lines_.erase(at, at + static_cast<std::ptrdiff_t>(span.to.line - span.from.line));
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(span.from.line) + 1, pieces.begin() + 1,
                  pieces.end());
    lines_[span.from.line + pieces.size() - 1] += tail;
}

} // namespace fjedit
