#include "fjedit/widget/text_layout.hpp"

#include "fjedit/core/folds.hpp"

#include <algorithm>
#include <cmath>

namespace fjedit {

TextLayout::TextLayout(ftk::ui::core::TextSystem& text, ftk::ui::core::TextStyle style, std::size_t tab_width)
    : text_(&text), style_(style), tab_width_(std::max<std::size_t>(tab_width, 1)),
      space_(text.caret_x(" ", style, 1)) {}

float TextLayout::line_height() const {
    return text_->line_height(style_);
}

float TextLayout::next_stop(float x) const {
    const float stop = space_ * static_cast<float>(tab_width_);
    if (stop <= 0.0f) return x;
    // A hair's allowance, so a caret that sits on a stop goes on to the next.
    return (std::floor(x / stop + 1e-4f) + 1.0f) * stop;
}

float TextLayout::advance(std::string_view line, std::size_t from, std::size_t to, float x) const {
    to = std::min(to, line.size());
    while (from < to) {
        const std::size_t tab = line.find('\t', from);
        const std::size_t end = std::min(tab, to);
        if (end > from) {
            const std::string_view run = line.substr(from, end - from);
            x += text_->caret_x(run, style_, run.size());
        }
        if (end == to) break;
        x = next_stop(x);
        from = end + 1;
    }
    return x;
}

float TextLayout::x_of(const Buffer& buffer, Position at) const {
    return advance(buffer.line(at.line), 0, at.byte, 0.0f);
}

std::size_t TextLayout::byte_at(std::string_view line, float x) const {
    float run_x = 0.0f;
    std::size_t from = 0;
    while (from <= line.size()) {
        const std::size_t tab = line.find('\t', from);
        const std::size_t end = std::min(tab, line.size());
        const std::string_view run = line.substr(from, end - from);
        const float width = run.empty() ? 0.0f : text_->caret_x(run, style_, run.size());
        if (x < run_x + width) return from + text_->caret_at(run, style_, x - run_x);
        run_x += width;
        if (end == line.size()) break;
        // A tab: the near half is before it, the far half after.
        const float stop = next_stop(run_x);
        if (x < (run_x + stop) * 0.5f) return end;
        if (x < stop) return end + 1;
        run_x = stop;
        from = end + 1;
    }
    return line.size();
}

Position TextLayout::position_at(const Buffer& buffer, std::size_t line, float x) const {
    return {.line = line, .byte = byte_at(buffer.line(line), x)};
}

std::optional<std::size_t> TextLayout::line_below(const Buffer& buffer, std::size_t line, std::size_t count) const {
    return folds_ != nullptr ? folds_->line_below(buffer, line, count) : Layout::line_below(buffer, line, count);
}

std::optional<std::size_t> TextLayout::line_above(const Buffer& buffer, std::size_t line, std::size_t count) const {
    return folds_ != nullptr ? folds_->line_above(line, count) : Layout::line_above(buffer, line, count);
}

} // namespace fjedit
