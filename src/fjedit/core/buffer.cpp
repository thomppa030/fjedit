#include "fjedit/core/buffer.hpp"

#include <algorithm>

namespace fjedit {

namespace {

constexpr std::string_view BYTE_ORDER_MARK = "\xEF\xBB\xBF";

bool continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

} // namespace

Buffer::Buffer() : lines_(1) {}

Buffer::Buffer(std::string_view text) {
    if (text.starts_with(BYTE_ORDER_MARK)) {
        byte_order_mark_ = true;
        text.remove_prefix(BYTE_ORDER_MARK.size());
    }

    std::size_t lf = 0;
    std::size_t crlf = 0;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c != '\n' && c != '\r') continue;
        lines_.emplace_back(text.substr(begin, i - begin));
        if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            ++crlf;
            ++i;
        } else if (c == '\n') {
            ++lf;
        }
        begin = i + 1;
    }
    lines_.emplace_back(text.substr(begin));

    // A lone \r breaks a line but votes for neither: it is too rare to be
    // what a file means to keep.
    line_ending_ = crlf > lf ? LineEnding::CrLf : LineEnding::Lf;
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

} // namespace fjedit
