#include "fjedit/widget/file_look.hpp"

#include "fjedit/core/language.hpp"
#include "fjedit/widget/icons.hpp"

namespace fjedit {

FileLook file_look(const std::filesystem::path& path) {
    using ftk::theme::Hue;
    const std::string_view name = language_for(path.filename().string()).name;
    if (name == "C++") return {.icon = icon::code_file, .hue = Hue::slate};
    if (name == "GLSL" || name == "FJSL") return {.icon = icon::code_file, .hue = Hue::blue};
    if (name == "FJSS") return {.icon = icon::sheet_file, .hue = Hue::lilac};
    if (name == "Widget") return {.icon = icon::widget_file, .hue = Hue::lilac};
    if (name == "JSON") {
        if (path.extension() == ".fjmat") return {.icon = icon::data_file, .hue = Hue::blue};
        return {.icon = icon::data_file, .hue = Hue::grey};
    }
    return {.icon = icon::plain_file};
}

} // namespace fjedit
