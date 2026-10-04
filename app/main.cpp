// fjedit, the program: a folder open in a window of its own.
//
//     fjedit [folder]     the folder, or the one it was started in

#include "fjedit/program/program.hpp"

#include "ftk/app/app_window.hpp"
#include "ftk/base/log.hpp"
#include "ftk/gpu/window.hpp"
#include "ftk/platform/platform.hpp"
#include "ftk/ui/core/context.hpp"
#include "ftk/ui/core_kit.hpp"
#include "ftk/ui/kit/settings_file.hpp"

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
    // A language server's answer comes on a thread of its own: it wakes the
    // window to draw it.
    fjedit::Program program(folder, {.recovery = fjedit::Program::recovery_folder(folder),
                                     .services = std::nullopt,
                                     .wake = [] { ftk::Window::wake(); }});

    // fjedit's settings file, this machine's: where its windows were left
    // and its preferences. The window's UI is the current one from here on,
    // so the file read now places its windows.
    ftk::ui::core::set_current(&app.ui());
    ftk::ui::add_settings_section(fjedit::preferences_section(program.preferences()));
    const std::filesystem::path settings_folder = ftk::platform::config_dir() / "fjedit";
    std::filesystem::create_directories(settings_folder, error);
    const std::string settings = (settings_folder / "settings.ini").string();
    ftk::ui::keep_settings_file(settings);

    auto ran = app.run({
        .ui =
            [&](ftk::ui::core::Context&, glm::vec2) {
                program.draw();
                ftk::ui::save_settings_when_due();
            },
        // The window waits while the program asks about unsaved files, and
        // closes once it has its answer.
        .update =
            [&] {
                ftk::Window& window = app.window();
                if (window.should_close() && !program.ask_to_quit()) window.cancel_close();
                if (program.quitting()) window.request_close();
            },
    });
    ftk::ui::save_settings_file(settings);
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
