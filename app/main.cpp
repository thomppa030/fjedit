// fjedit, the program: a folder open in a window of its own.
//
//     fjedit [folder]     the folder, or the one it was started in

#include "fjedit/program/program.hpp"
#include "fjedit/widget/file_kind.hpp"

#include "ftk/app/app_window.hpp"
#include "ftk/app/app_identity.hpp"
#include "ftk/app/file_browser.hpp"
#include "ftk/base/delegate.hpp"
#include "ftk/base/log.hpp"
#include "ftk/gpu/window.hpp"
#include "ftk/platform/platform.hpp"
#include "ftk/ui/core/context.hpp"
#include "ftk/ui/core_kit.hpp"
#include "ftk/ui/kit/settings_file.hpp"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace {

// The window's title with the folder `root` open.
std::string title_of(const std::filesystem::path& root) {
    return root.filename().string() + " - fjedit";
}

int run(const std::filesystem::path& folder) {
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error)) {
        FTK_CORE_CRITICAL("fjedit opens a folder, and {} is none", folder.string());
        return EXIT_FAILURE;
    }
    // Who fjedit is to the desktop, before its window opens: what the window
    // manager knows it by, and its icon. The brand files are staged beside
    // the program in brand/.
    const std::filesystem::path brand = ftk::platform::executable_path().parent_path() / "brand";
    if (auto told = ftk::set_app_identity({.id = "fjedit", .name = "fjedit", .icon = brand / "fjedit-tile.svg"});
        !told) {
        FTK_CORE_WARN("fjedit goes without its icon: {}", told.error());
    }
    auto made = ftk::AppWindow::create({
        .title = title_of(fjedit::Workspace(folder).root()),
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
    const std::filesystem::path fonts = ftk::platform::executable_path().parent_path() / "ftk" / "fonts";
    if (!ftk::ui::use_kit_fonts(app.ui(), fonts)) {
        FTK_CORE_CRITICAL("fjedit could not find its fonts beside it");
        return EXIT_FAILURE;
    }
    // Fjell's files look as they do in Fjell.
    fjedit::register_file_kinds();
    // How this machine likes the code shown, and the folders opened lately,
    // whatever folder is open.
    fjedit::Preferences preferences;
    std::vector<std::string> recent_folders;

    // File > Open folder picks one in the kit's folder browser, a window of
    // its own, which offers the folders opened lately.
    ftk::FileBrowser browser;
    browser.set_device(&app.device(), [fonts](ftk::ui::core::Context& ui) { (void)ftk::ui::use_kit_fonts(ui, fonts); });
    browser.set_recent_folders({
        .load = [&recent_folders] { return recent_folders; },
        .save =
            [&recent_folders](const std::vector<std::string>& folders) {
                recent_folders = folders;
                ftk::ui::settings_changed();
            },
    });

    // The program on the folder open, made again for each folder opened in
    // its place. A language server's answer comes on a thread of its own:
    // it wakes the window to draw it.
    std::unique_ptr<fjedit::Program> program;
    const auto make_program = [&](const std::filesystem::path& root) {
        // The one before goes first, its servers and threads with it.
        program.reset();
        program = std::make_unique<fjedit::Program>(
            root, preferences,
            fjedit::ProgramOptions{
                .recovery = fjedit::Program::recovery_folder(root),
                .services = std::nullopt,
                .wake = [] { ftk::Window::wake(); },
                .keys = fjedit::Program::keys_file(),
                .pick_folder =
                    [&browser, &program] {
                        browser.open("Open folder", ftk::FileBrowser::Mode::select_directory, "Open",
                                     {.start = program->workspace().root()});
                    },
            });
        browser.set_project(program->workspace().root());
        app.window().set_title(title_of(program->workspace().root()));
    };
    make_program(folder);
    const ftk::Connection picked =
        browser.on_selected.bind([&program](const std::string& path) { program->open_folder(path); });

    // fjedit's settings file, this machine's: where its windows were left,
    // its preferences and the folders opened lately. The window's UI is the
    // current one from here on, so the file read now places its windows.
    ftk::ui::core::set_current(&app.ui());
    ftk::ui::add_settings_section(fjedit::preferences_section(preferences));
    ftk::ui::add_settings_section(fjedit::recent_folders_section(recent_folders));
    const std::filesystem::path settings_folder = ftk::platform::config_dir() / "fjedit";
    std::filesystem::create_directories(settings_folder, error);
    const std::string settings = (settings_folder / "settings.ini").string();
    ftk::ui::keep_settings_file(settings);

    auto ran = app.run({
        .ui =
            [&](ftk::ui::core::Context&, glm::vec2) {
                program->draw();
                ftk::ui::save_settings_when_due();
            },
        // The folder browser draws a window of its own, between the
        // program's frames. A folder picked opens once the program has its
        // answer about the unsaved files; the window waits while it asks,
        // and closes once it has its answer to quitting.
        .update =
            [&] {
                browser.tick();
                if (program->folder_wanted()) {
                    const std::filesystem::path wanted = *program->folder_wanted();
                    make_program(wanted);
                }
                ftk::Window& window = app.window();
                if (window.should_close() && !program->ask_to_quit()) window.cancel_close();
                if (program->quitting()) window.request_close();
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
