// fjedit, the program: a folder open in a window of its own.
//
//     fjedit [folder]     the folder, or the one it was started in

#include "fjedit/program/program.hpp"

#include "ftk/app/app_window.hpp"
#include "ftk/base/log.hpp"
#include "ftk/platform/platform.hpp"
#include "ftk/ui/core_kit.hpp"

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace {

int run(const std::filesystem::path& folder) {
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error)) {
        FTK_CORE_CRITICAL("fjedit opens a folder, and {} is none", folder.string());
        return EXIT_FAILURE;
    }
    auto made = ftk::AppWindow::create({
        .title = std::filesystem::absolute(folder, error).filename().string() + " - fjedit",
        .width = 1440,
        .height = 900,
        .device = {.name = "fjedit"},
    });
    if (!made) {
        FTK_CORE_CRITICAL("fjedit could not start: {}", made.error());
        return EXIT_FAILURE;
    }
    ftk::AppWindow& app = **made;
    // ftk_stage_editor_data() put the kit's fonts in ftk/ beside the program.
    if (!ftk::ui::use_kit_fonts(app.ui(), ftk::platform::executable_path().parent_path() / "ftk" / "fonts")) {
        FTK_CORE_CRITICAL("fjedit could not find its fonts beside it");
        return EXIT_FAILURE;
    }
    fjedit::Program program(folder);
    auto ran = app.run({.ui = [&](ftk::ui::core::Context&, glm::vec2) { program.draw(); }});
    if (!ran) {
        FTK_CORE_CRITICAL("fjedit stopped: {}", ran.error());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    ftk::log::init();
    const int code = run(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path());
    ftk::log::shutdown();
    return code;
}
