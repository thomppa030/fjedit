#pragma once

// fjedit as a program (sheet 39): one folder open in a window, its files
// down the side, the files open in tabs over the code, the menus along the
// top and the status bar along the bottom. It draws with ftk's kit inside
// whatever runs its frames: a window of its own, or a test's harness. The
// files, the code and the tree are the editor library's; the program lays
// them out and adds what belongs to its window.
//
//     fjedit::Program program(folder, preferences);
//     app.run({.ui = [&](ftk::ui::core::Context&, glm::vec2) { program.draw(); }});

#include "fjedit/editor/code_area.hpp"
#include "fjedit/editor/code_files.hpp"
#include "fjedit/editor/file_tree.hpp"
#include "fjedit/program/preferences_window.hpp"

#include "ftk/app/desktop_entry.hpp"
#include "ftk/ui/kit/dialog.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit {

/// What a program starts with besides its folder.
struct ProgramOptions {
    /// Where unsaved work is kept against a crash, and offered back from at
    /// the start; none keeps nothing.
    std::filesystem::path recovery{};
    /// Its language services, in the order a file is asked of them. Not
    /// given, it runs clangd for C and C++, and fjls for a game's script
    /// headers and fjell's own files.
    std::optional<std::vector<ServiceMaker>> services{};
    /// Wakes the frame loop from another thread, when a language service's
    /// answer comes: ftk::Window::wake() for a window.
    std::function<void()> wake{};
    /// keys.vim, one's own maps for Vim's keys (decision 7 of the Vim plan),
    /// read at the start and again when saved here; none reads none.
    std::filesystem::path keys{};
    /// Asks where another folder is, for File > Open folder: a window's
    /// folder browser, which hands the folder picked to open_folder(). None
    /// leaves the entry unavailable.
    std::function<void()> pick_folder{};
    /// The device its window draws with, which the program's pictures are
    /// made on. None, as in a test's harness, leaves each picture's room
    /// empty.
    ftk::gpu::Device* device{nullptr};
    /// fjedit's brand files, brand/ beside the program: a group with no file
    /// open shows its logomark from there. None, or a mark that doesn't read,
    /// shows the kit's file icon.
    std::filesystem::path brand{};
    /// What File › Create desktop entry writes: fjedit's, desktop_entry().
    /// None leaves the entry out of the File menu.
    std::optional<ftk::DesktopEntry> desktop_entry{};
    /// Where it writes it: the user's folder for desktop entries,
    /// ftk::desktop_data_home(). None, as on a system without desktop
    /// entries, leaves the entry out of the File menu.
    std::filesystem::path desktop_data_home{};
};

class Program {
public:
    /// The folder `root` open, the code shown as `preferences` say. They
    /// outlive the program, as they outlive each folder opened in a window.
    Program(std::filesystem::path root, Preferences& preferences, ProgramOptions options = {});

    // Its code area holds `this`: the program stays where it was made.
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;
    Program(Program&&) = delete;
    Program& operator=(Program&&) = delete;

    /// Where a program on the folder `root` keeps its unsaved work on this
    /// machine: a folder of its own under the user's state folder.
    [[nodiscard]] static std::filesystem::path recovery_folder(const std::filesystem::path& root);

    /// fjedit's desktop entry, which puts it in the desktop's launcher on
    /// Linux: the program at `program`, its icon the tile in `brand`.
    [[nodiscard]] static ftk::DesktopEntry desktop_entry(const std::filesystem::path& program,
                                                         const std::filesystem::path& brand);

    /// Every command the program runs by name: its code area's, then its
    /// own (Open folder, Quit, Preferences).
    [[nodiscard]] static std::span<const CodeCommand> commands();

    /// Draws a frame of the program, filling the window's work area.
    void draw();

    /// Runs one of the program's commands or its code area's by name
    /// ("file.save"); false when there is none.
    bool run(std::string_view command);

    /// Asked to quit, by the window or the menu: true when nothing unsaved
    /// stands in the way. Otherwise it asks what to do with the unsaved
    /// files on its next draw and answers false; quitting() then says when
    /// it may go. Callable between frames.
    bool ask_to_quit();

    /// Asked to open the folder `folder` in this one's place: as quitting,
    /// it asks first what to do with the unsaved files, on its next draw.
    /// folder_wanted() says the folder once nothing stands in the way.
    /// Callable between frames.
    void open_folder(std::filesystem::path folder);

    /// The folder asked to open in this one's place, once the unsaved files
    /// were answered: the window makes a program on it.
    [[nodiscard]] const std::optional<std::filesystem::path>& folder_wanted() const { return folder_wanted_; }

    /// Quitting was asked for and answered: the window may close.
    [[nodiscard]] bool quitting() const { return quitting_; }

    /// The files open, their language services and the work kept for them.
    [[nodiscard]] const CodeFiles& files() const { return files_; }

    /// The code: its groups of tabs, its keys and find in files.
    [[nodiscard]] CodeArea& code() { return code_; }
    [[nodiscard]] const CodeArea& code() const { return code_; }

    /// How this machine likes the code shown, which every view takes as it
    /// is drawn; a window keeps them in its settings file through
    /// preferences_section().
    [[nodiscard]] Preferences& preferences() { return preferences_; }

    [[nodiscard]] const PreferencesWindow& preferences_window() const { return preferences_window_; }

private:
    void draw_menus();
    void draw_side();
    void draw_status();
    void ask_quit();
    // Writes fjedit's desktop entry, answered on the status bar.
    void create_desktop_entry();
    // The files brought up to date: every few seconds the disk looked at,
    // and the folders listed read again then; what the views asked of a
    // server started again let go.
    void update_files(double now);
    // How clangd and fjls are, for the Preferences window.
    [[nodiscard]] LanguageServerShown clangd_shown() const;
    [[nodiscard]] LanguageServerShown fjls_shown() const;

    CodeFiles files_;
    CodeArea code_;
    FileTree tree_;
    // How the code is shown, which outlive the program, and the window
    // they are edited in.
    Preferences& preferences_;
    PreferencesWindow preferences_window_;
    // Quitting, or leaving for another folder: asked for, the unsaved files
    // asked about while asked, the folder left for, and the answer given.
    bool quit_asked_{false};
    std::vector<std::filesystem::path> quit_paths_;
    std::vector<ftk::ui::UnsavedItem> quit_items_;
    std::optional<std::filesystem::path> leaving_for_;
    bool quitting_{false};
    std::optional<std::filesystem::path> folder_wanted_;
    // What asks where another folder is.
    std::function<void()> pick_folder_;
    // What File › Create desktop entry writes and where, and its answer, for
    // the status bar until the next key: written or not, what it says, and
    // the entry's path when it was written.
    std::optional<ftk::DesktopEntry> desktop_entry_;
    std::filesystem::path desktop_data_home_;
    struct EntryAnswer {
        bool written{false};
        std::string text{};
        std::string where{};
    };
    std::optional<EntryAnswer> entry_answer_;
};

} // namespace fjedit
