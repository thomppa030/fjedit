#include "fjedit/widget/next_keys.hpp"

#include "fjedit/widget/code_view.hpp"

#include "ftk/ui/kit/item.hpp"
#include "ftk/ui/kit/key_cap.hpp"
#include "ftk/ui/kit/layout.hpp"
#include "ftk/ui/kit/plate.hpp"
#include "ftk/ui/kit/text.hpp"
#include "ftk/ui/theme.hpp"

#include <algorithm>
#include <format>

namespace fjedit {

namespace {

namespace ui = ftk::ui;
namespace theme = ftk::theme;

// The plate's width beside code at 13 px, wider as the code is.
constexpr float PLATE_WIDTH = 320.0f;
// The most keys it lists; the rest it counts.
constexpr std::size_t MOST = 16;
// Where a key label's name sits down from its top, which its title keeps
// level with.
constexpr float LABEL_TEXT_DROP = 1.0f;

// `text` as it fits `width` in the face drawn now, cut with an ellipsis.
std::string cut_to(std::string_view text, float width) {
    constexpr std::string_view ELLIPSIS = "\xE2\x80\xA6";
    if (ui::text_size(text).x <= width) return std::string(text);
    std::string out(text);
    while (!out.empty()) {
        std::size_t end = out.size() - 1;
        while (end > 0 && (static_cast<unsigned char>(out[end]) & 0xC0) == 0x80) --end;
        out.erase(end);
        if (ui::text_size(out + std::string(ELLIPSIS)).x <= width) return out + std::string(ELLIPSIS);
    }
    return std::string(ELLIPSIS);
}

} // namespace

void next_keys_plate(std::string_view typed, std::span<const NextKey> keys, float text_size, ftk::ui::core::Rect over) {
    const float width = std::min(PLATE_WIDTH * text_size / CODE_TEXT, over.width() - 2.0f * ui::PLATE_INSET);
    if (auto plate = ui::Plate("##next_keys", over.min, over.max, ui::PlatePlace::BottomRight, width)) {
        const ui::FontScope body(ui::Font::Body, text_size);
        // The keys so far, then what can come after them.
        {
            const ui::FontScope mono(theme::mono_font(), 0.0f);
            ui::text(typed);
        }
        ui::same_line();
        ui::text("then", theme::text_secondary());

        const float gap = 2.0f * ui::item_spacing().x;
        const float column = (plate.inner_width() - gap) * 0.5f;
        const glm::vec2 start = ui::cursor();
        float top = start.y;
        float bottom = start.y;
        const std::size_t shown = std::min(keys.size(), MOST);
        for (std::size_t i = 0; i < shown; ++i) {
            const bool right = i % 2 == 1;
            const float left = start.x + (right ? column + gap : 0.0f);
            ui::set_cursor({left, top});
            {
                const ui::FontScope mono(theme::mono_font(), 0.0f);
                ui::key_label(keys[i].key);
            }
            const float title_at = ui::item_max().x + ui::item_spacing().x;
            bottom = std::max(bottom, ui::item_max().y);
            ui::set_cursor({title_at, top + LABEL_TEXT_DROP});
            const std::string title = keys[i].group ? "+ " + keys[i].title : keys[i].title;
            ui::text(cut_to(title, left + column - title_at), keys[i].group ? theme::text() : theme::text_secondary());
            bottom = std::max(bottom, ui::item_max().y);
            if (right || i + 1 == shown) top = bottom + ui::item_spacing().y;
        }
        ui::set_cursor({start.x, top});
        if (keys.size() > shown) ui::text(std::format("and {} more", keys.size() - shown), theme::text_secondary());
    }
}

} // namespace fjedit
