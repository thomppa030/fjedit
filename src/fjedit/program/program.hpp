#pragma once

// fjedit as a program (sheet 39): one folder open in a window, its files
// down the side, the files open in tabs over the code, the menus along the
// top and the status bar along the bottom. It draws with ftk's kit inside
// whatever runs its frames: a window of its own, or a test's harness.
//
//     fjedit::Program program(folder);
//     app.run({.ui = [&](ftk::ui::core::Context&, glm::vec2) { program.draw(); }});

#include "fjedit/core/keymap.hpp"
#include "fjedit/core/language_services.hpp"
#include "fjedit/core/recovery.hpp"
#include "fjedit/core/workspace.hpp"
#include "fjedit/program/file_tree.hpp"
#include "fjedit/program/find_in_files.hpp"
#include "fjedit/program/preferences.hpp"
#include "fjedit/widget/code_view.hpp"

#include "ftk/ui/kit/dialog.hpp"

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
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
    /// headers and Fjell's own files.
    std::optional<std::vector<ServiceMaker>> services{};
    /// Wakes the frame loop from another thread, when a language service's
    /// answer comes: ftk::Window::wake() for a window.
    std::function<void()> wake{};
};

class Program {
public:
    /// The folder `root` open.
    explicit Program(std::filesystem::path root, ProgramOptions options = {});

    /// Where a program on the folder `root` keeps its unsaved work on this
    /// machine: a folder of its own under the user's state folder.
    [[nodiscard]] static std::filesystem::path recovery_folder(const std::filesystem::path& root);

    /// Draws a frame of the program, filling the window's work area.
    void draw();

    /// Opens the file at `path` in a tab in front with the keyboard in it,
    /// or brings its tab forward when it is open; nothing but a note on the
    /// status bar when it can't be read.
    void open(const std::filesystem::path& path);

    /// Runs one of the program's commands by name ("file.save"); false when
    /// there is none.
    bool run(std::string_view command);

    /// The keys a menu shows for `command` ("Ctrl S"): the program's, else
    /// those of the view's keys; with Vim on, those of the mode the view in
    /// front is in ("u", "\"+yy"). Empty when no keys run it.
    [[nodiscard]] std::string shortcut(std::string_view command) const;

    /// Asked to quit, by the window or the menu: true when nothing unsaved
    /// stands in the way. Otherwise it asks what to do with the unsaved
    /// files on its next draw and answers false; quitting() then says when
    /// it may go. Callable between frames.
    bool ask_to_quit();

    /// Quitting was asked for and answered: the window may close.
    [[nodiscard]] bool quitting() const { return quitting_; }

    [[nodiscard]] const Workspace& workspace() const { return workspace_; }

    /// The file in front in the group that last had the keyboard, when one
    /// is open there.
    [[nodiscard]] std::optional<std::filesystem::path> front() const;

    /// How many groups of tabs stand side by side: one, or more once split.
    [[nodiscard]] std::size_t groups() const { return groups_.size(); }

    /// Find in files, open or not.
    [[nodiscard]] const FindInFiles& find_in_files() const { return find_in_files_; }

    /// How this machine likes the code shown, which every view takes as it
    /// is drawn; a window keeps them in its settings file through
    /// preferences_section().
    [[nodiscard]] Preferences& preferences() { return preferences_; }
    [[nodiscard]] const PreferencesWindow& preferences_window() const { return preferences_window_; }

    /// The keymap mode the program's own keys are bound in.
    static constexpr std::string_view PROGRAM = "program";
    /// The mode they are bound in with Vim on: all but Ctrl W, which is
    /// Vim's.
    static constexpr std::string_view VIM_PROGRAM = "vim.program";

private:
    // A file open in a tab, and its view.
    struct Tab {
        std::filesystem::path path;
        std::unique_ptr<EditorView> view;
    };

    // Tabs, and the one in front; and the one to bring forward when next
    // drawn, as when a file is opened.
    struct Group {
        std::vector<Tab> tabs;
        std::size_t front{0};
        std::optional<std::size_t> select;
    };

    void draw_menus();
    void draw_side();
    void draw_group(Group& group, std::size_t index);
    void draw_status();
    void draw_prompts();
    void ask_close();
    void ask_quit();
    void ask_recover();
    // A new name for what a view asked to rename, then the service's
    // edits for it, made once they come.
    void ask_rename();
    // Makes `edits`: in the files open, as one step each while they are as
    // the edits were made for; in the others, on disk.
    void make_edits(const WorkspaceEdit& edits);
    void take_keys();
    // Every few seconds: files another program changed, reloaded when they
    // have no unsaved changes here and noted when they do; the tree read
    // again; and unsaved work kept.
    void check_disk(double now);
    // Keeps what each unsaved file holds when it changed since it was last
    // kept, and forgets what was kept for those now saved.
    void keep_unsaved();
    // Forgets the work kept for `path`: saved, or let go.
    void forget_kept(const std::filesystem::path& path);

    // The tab in front, when there is one.
    [[nodiscard]] Tab* front_tab();
    [[nodiscard]] const Tab* front_tab() const;
    // Closes the tab in front, asking first when its file has unsaved
    // changes that no other tab shows.
    void close_front();
    // Closes `group`'s tab `index`, and lets its file go when no tab
    // shows it any more. A group split off that has no tab left goes.
    void close_tab(Group& group, std::size_t index);
    // Opens the file in front of group `index` again in a group of its own
    // beside it, which then has the keyboard.
    void split(std::size_t index);
    // Opens find in files, looking for what the view in front selects.
    void open_find_in_files();
    // Does what find in files asked: opens a file at a match, and lets go
    // of the files it opened only to show them.
    void take_find_in_files(const FindInFilesAsk& ask);
    void save(const std::filesystem::path& path);
    // A view of `document`, the file at `path`, joined to the language
    // services, which are told of the file when one answers for it.
    [[nodiscard]] std::unique_ptr<EditorView> make_view(const std::filesystem::path& path, Document& document);
    // Lets the file at `path` go: its services first, then the workspace.
    void let_go(const std::filesystem::path& path);
    // Opens the file at `path` with `span` selected, the keyboard in it.
    void go_to(const std::filesystem::path& path, Span span);
    // clangd for C and C++, and fjls for a game's script headers and
    // Fjell's own files, at the paths the preferences give them.
    [[nodiscard]] ServiceMaker clangd();
    [[nodiscard]] ServiceMaker fjls();
    // The makers, those of clangd and fjls noting the path each starts at.
    [[nodiscard]] std::vector<ServiceMaker> noting_paths(std::vector<ServiceMaker> makers);
    // Starts a server again when the path the preferences give it changed:
    // what the views asked of the one before is let go.
    void follow_server_paths();
    // How clangd and fjls are, for the Preferences window.
    [[nodiscard]] LanguageServerShown clangd_shown() const;
    [[nodiscard]] LanguageServerShown fjls_shown() const;

    Workspace workspace_;
    Keymap keymap_;
    FileTree tree_;
    std::vector<Group> groups_;
    std::size_t active_{0};
    // A tab asked to close with unsaved changes, while it is asked about,
    // and whether the question is open.
    std::optional<std::filesystem::path> closing_;
    bool asking_{false};
    // How the code is shown, and the window they are edited in.
    Preferences preferences_;
    PreferencesWindow preferences_window_;
    // Vim's registers, which every view with Vim on shares.
    std::shared_ptr<vim::Registers> registers_{std::make_shared<vim::Registers>()};
    // Files changed on disk while they had unsaved changes here, until
    // answered; and when the disk is next looked at.
    std::set<std::filesystem::path> changed_;
    double next_check_{0.0};
    // Unsaved work kept against a crash, and each file's version as kept.
    std::optional<Recovery> recovery_;
    std::map<std::filesystem::path, uint64_t> kept_;
    // Work kept from before, offered back once at the start, and the list
    // asked about.
    bool recover_looked_{false};
    std::vector<Recovery::Kept> recovering_;
    std::vector<ftk::ui::UnsavedItem> recover_items_;
    // Quitting: asked for, the unsaved files asked about while asked, and
    // the answer given.
    bool quit_asked_{false};
    std::vector<std::filesystem::path> quit_paths_;
    std::vector<ftk::ui::UnsavedItem> quit_items_;
    bool quitting_{false};
    // Where the program's dialogs hang from: under the tabs.
    glm::vec2 dialog_at_{0.0f};
    // The threads work is done on away from the frames: searches. Before
    // what uses them, so it goes first.
    ftk::ThreadPool pool_;
    // Find in files, over the code.
    FindInFiles find_in_files_{workspace_, pool_};
    // What wakes the frame loop when a language service answers; and the
    // services, each running once a file it answers for opens.
    std::function<void()> wake_;
    LanguageServices services_;
    // The paths clangd and fjls were last started at.
    std::string clangd_started_;
    std::string fjls_started_;
    // A place a view asked to go to in another file, taken once the views
    // are drawn.
    std::optional<Location> going_;
    // A rename asked for: where, and what is named there; the new name
    // typed, whether its prompt is open, and the service's answer to come.
    struct Renaming {
        std::filesystem::path path;
        Position at;
        std::string name;
        std::string typed;
        bool prompting{false};
        Asked<ftk::Result<WorkspaceEdit>> asked;
    };
    std::optional<Renaming> renaming_;
    // What went wrong last, for the status bar.
    std::string note_;
};

} // namespace fjedit
