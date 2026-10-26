#include "fjedit/widget/file_kind.hpp"

#include "fjedit/core/language.hpp"
#include "fjedit/widget/icons.hpp"

#include "ftk/ui/kit/icons.hpp"

#include <array>
#include <string_view>

namespace fjedit {

namespace {

using ftk::ui::AssetKind;
using H = ftk::theme::Hue;

// fjell's categories, each by its hue (fjell's src/ui/categories.hpp).
namespace category {
constexpr H RENDERING = H::blue;
constexpr H ENVIRONMENT = H::cyan;
constexpr H PHYSICS = H::coral;
constexpr H ANIMATION = H::pink;
constexpr H AUDIO = H::orange;
constexpr H VFX = H::periwinkle;
constexpr H UI = H::lilac;
constexpr H LOGIC = H::slate;
constexpr H STRUCTURE = H::grey;
} // namespace category

namespace C = category;

constexpr AssetKind SCRIPT{"C++ file", icon::script, C::LOGIC};
constexpr AssetKind SHADER{"shader", icon::shader, C::RENDERING};
constexpr AssetKind MESH{"mesh", icon::mesh, C::RENDERING};
constexpr AssetKind BLEND{"blend space", icon::blend_space, C::ANIMATION};
constexpr AssetKind AUDIO{"audio clip", ftk::ui::icon::audio, C::AUDIO};
constexpr AssetKind UI_LAYOUT{"UI layout", icon::ui_layout, C::UI};
constexpr AssetKind FONT{"font", ftk::ui::icon::font, C::UI};
constexpr AssetKind INPUT{"input", icon::input, C::LOGIC};

struct Entry {
    std::string_view extension;
    AssetKind kind;
};

// fjell's file types, as fjell registers them (its src/ui/asset_kinds.cpp).
// Images, fonts and audio files the kit knows already.
constexpr std::array ENTRIES{
    Entry{".fjmesh", MESH}, Entry{".glb", MESH}, Entry{".gltf", MESH}, Entry{".fbx", MESH}, Entry{".obj", MESH},
    Entry{".fjskel", {"skeleton", icon::skeleton, C::ANIMATION}},
    Entry{".fjmat", {"material", icon::material, C::RENDERING}},
    Entry{".fjtex", {"texture", ftk::ui::icon::texture, C::RENDERING}},
    Entry{".fjlut", {"colour grade", icon::colour_grade, C::RENDERING}},
    Entry{".cube", {"colour grade table", icon::colour_grade, C::RENDERING}},
    Entry{".fjsl", SHADER},
    Entry{".fjanim", {"animation", icon::animation, C::ANIMATION}},
    Entry{".fjanimset", {"animation set", icon::animation, C::ANIMATION}},
    Entry{".fjblend1D", BLEND}, Entry{".fjblend2D", BLEND},
    Entry{".fjaudio", AUDIO},
    Entry{".fjvfx", {"effect", icon::vfx, C::VFX}},
    Entry{".fjui", UI_LAYOUT}, Entry{".fjss", {"style sheet", icon::ui_layout, C::UI}},
    Entry{".fjwidget", {"widget", icon::ui_layout, C::UI}},
    Entry{".fjfont", FONT},
    Entry{".fjsurface", {"surface", icon::surface, C::PHYSICS}},
    Entry{".fjday", {"day profile", icon::day_profile, C::ENVIRONMENT}},
    Entry{".fjweather", {"weather", icon::weather, C::ENVIRONMENT}},
    Entry{".fjclimate", {"climate", icon::climate, C::ENVIRONMENT}},
    Entry{".fjwater", {"water", icon::water, C::ENVIRONMENT}},
    Entry{".fjterrain", {"terrain", icon::surface, C::ENVIRONMENT}},
    Entry{".fjinst", {"instance set", icon::mesh, C::RENDERING}},
    Entry{".fjplacement", {"placement recipe", icon::placement, C::RENDERING}},
    Entry{".fjell", {"scene", icon::scene, C::STRUCTURE}},
    Entry{".fjp", {"preset", icon::preset, C::STRUCTURE}},
    Entry{".fjinput", INPUT}, Entry{".fjaction", INPUT},
    Entry{".fjsave", {"saved game", icon::saved_game, C::STRUCTURE}},
};

} // namespace

void register_file_kinds() {
    for (const Entry& entry : ENTRIES) {
        ftk::ui::register_asset_kind(entry.extension, entry.kind);
    }
}

AssetKind file_kind(const std::filesystem::path& path) {
    const std::string_view language = language_for(path.filename().string()).name;
    if (language == "C++") return SCRIPT;
    if (language == "GLSL") return SHADER;
    return ftk::ui::asset_kind(path.extension().string());
}

} // namespace fjedit
