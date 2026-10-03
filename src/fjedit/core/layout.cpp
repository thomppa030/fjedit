#include "fjedit/core/layout.hpp"

#include "ftk/base/utf8.hpp"

namespace fjedit {

std::size_t column_of(std::string_view line, std::size_t byte, std::size_t tab_width) {
    std::size_t cells = 0;
    for (std::size_t i = 0; i < byte && i < line.size();) {
        const uint32_t c = ftk::utf8::next(line, i);
        cells = c == '\t' ? (cells / tab_width + 1) * tab_width : cells + 1;
    }
    return cells;
}

float MonospaceLayout::x_of(const Buffer& buffer, Position at) const {
    return static_cast<float>(column_of(buffer.line(at.line), at.byte, tab_width_));
}

Position MonospaceLayout::position_at(const Buffer& buffer, std::size_t line, float x) const {
    const std::string_view text = buffer.line(line);
    std::size_t cells = 0;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t start = i;
        const uint32_t c = ftk::utf8::next(text, i);
        const std::size_t next = c == '\t' ? (cells / tab_width_ + 1) * tab_width_ : cells + 1;
        const float middle = (static_cast<float>(cells) + static_cast<float>(next)) * 0.5f;
        if (x < middle) return {.line = line, .byte = start};
        cells = next;
    }
    return {.line = line, .byte = text.size()};
}

} // namespace fjedit
