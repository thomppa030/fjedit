#include "fjedit/widget/file_kind.hpp"
#include "fjedit/widget/icons.hpp"

#include "ftk/ui/kit/icons.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using namespace fjedit;
using ftk::theme::Hue;

namespace {

// The glyph a file is shown by, as text: the same glyph spelled in two
// places need not be the same pointer.
std::string_view icon_of(const std::filesystem::path& path) {
    return file_kind(path).icon;
}

} // namespace

TEST_CASE("fjell's files show as fjell shows them, each kind's icon in its category's hue", "[file_kind]") {
    register_file_kinds();
    CHECK(icon_of("ui/menu.fjss") == icon::ui_layout);
    CHECK(file_kind("ui/menu.fjss").hue == Hue::lilac);
    CHECK(icon_of("ui/menu.fjwidget") == icon::ui_layout);
    CHECK(icon_of("crate.fjmat") == icon::material);
    CHECK(file_kind("crate.fjmat").hue == Hue::blue);
    CHECK(icon_of("water.fjsl") == icon::shader);
    CHECK(icon_of("title.fjfont") == ftk::ui::icon::font);
    CHECK(file_kind("title.fjfont").hue == Hue::lilac);
    CHECK(icon_of("sparks.fjvfx") == icon::vfx);
    CHECK(file_kind("sparks.fjvfx").hue == Hue::periwinkle);
    CHECK(icon_of("crate.glb") == icon::mesh);
}

TEST_CASE("Code shows by the language it is read in, any other file by the kit's kinds", "[file_kind]") {
    CHECK(icon_of("scripts/coin.hpp") == icon::script);
    CHECK(file_kind("scripts/coin.hpp").hue == Hue::slate);
    CHECK(icon_of("SHOUT.CPP") == icon::script);
    CHECK(icon_of("shaders/blur.frag") == icon::shader);
    CHECK(file_kind("shaders/blur.frag").hue == Hue::blue);
    CHECK(icon_of("pictures/coin.png") == ftk::ui::icon::texture);
    CHECK(icon_of("notes.txt") == ftk::ui::icon::file);
    CHECK_FALSE(file_kind("notes.txt").hue.has_value());
    CHECK(icon_of("Makefile") == ftk::ui::icon::file);
}
