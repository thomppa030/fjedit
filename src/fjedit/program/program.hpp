#pragma once

// fjedit as a program (sheet 39): one folder open in a window, its files
// down the side, the files open in tabs over the code, the menus along the
// top and the status bar along the bottom. It draws with ftk's kit inside
// whatever runs its frames: a window of its own, or a test's harness.
//
//     fjedit::Program program(folder, preferences);
//     app.run({.ui = [&](ftk::ui::core::Context&, glm::vec2) { program.draw(); }});

#include "fjedit/core/jump_list.hpp"
#include "fjedit/core/keymap.hpp"
#include "fjedit/core/language_services.hpp"
#include "fjedit/core/recovery.hpp"
#include "fjedit/core/workspace.hpp"
#include "fjedit/editor/file_tree.hpp"
#include "fjedit/editor/find_in_files.hpp"
#include "fjedit/program/preferences_window.hpp"
#include "fjedit/widget/code_view.hpp"

#include "ftk/app/desktop_entry.hpp"
#include "ftk/app/picture.hpp"
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
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

    /// Where a program on the folder `root` keeps its unsaved work on this
    /// machine: a folder of its own under the user's state folder.
    [[nodiscard]] static std::filesystem::path recovery_folder(const std::filesystem::path& root);

    /// Where this machine keeps one's own maps for Vim's keys: keys.vim in
    /// fjedit's settings folder, beside settings.ini.
    [[nodiscard]] static std::filesystem::path keys_file();

    /// fjedit's desktop entry, which puts it in the desktop's launcher on
    /// Linux: the program at `program`, its icon the tile in `brand`.
    [[nodiscard]] static ftk::DesktopEntry desktop_entry(const std::filesystem::path& program,
                                                         const std::filesystem::path& brand);

    /// One of the program's own commands: what keys call it by, and what a
    /// list of keys shows for it.
    struct Command {
        std::string_view name;
        std::string_view title;
    };
    /// The program's own commands, which run() runs.
    [[nodiscard]] static std::span<const Command> commands();

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
    /// front is in first ("u", "\"+dd", one's own binds), then the
    /// program's. Empty when no keys run it.
    [[nodiscard]] std::string shortcut(std::string_view command) const;

    /// What keys.vim says that couldn't be used, by line; the status bar
    /// says the first with Vim on.
    [[nodiscard]] const std::vector<vim::KeysError>& keys_errors() const { return keys_errors_; }

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

    /// What Vim's :set changed for the session, which every view's Vim
    /// shares and the views show over Preferences.
    [[nodiscard]] const vim::Settings& vim_settings() const { return *vim_settings_; }
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

    // Tabs, and the one in front; the one to bring forward when next
    // drawn, as when a file is opened; and where Vim's jumps in it went
    // from, as a Vim window keeps them.
    struct Group {
        std::vector<Tab> tabs;
        std::size_t front{0};
        std::optional<std::size_t> select;
        JumpList jumps;
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
    // Writes fjedit's desktop entry, answered on the status bar.
    void create_desktop_entry();
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
    // How many tabs show the file at `path`, in every group.
    [[nodiscard]] std::size_t tabs_showing(const std::filesystem::path& path) const;
    // The first file opened that has unsaved changes, when one has.
    [[nodiscard]] std::optional<std::filesystem::path> first_unsaved() const;
    // The folder `folder`, typed after Vim's `:e` from the folder open,
    // listed as the file tree lists it; hidden ones too with `hidden`.
    [[nodiscard]] std::vector<vim::PathEntry> list_paths(std::string_view folder, bool hidden);
    // Runs a `:` command of Vim's that is the program's, handed up by the
    // view in front, as Vim runs it: what would lose unsaved changes is
    // refused, said where Vim says it, unless the command has a !.
    void run_ex(const vim::ExCommand& command);
    // Answers what the view in front's Vim asked of the program: runs its
    // `:` command, or keeps or goes to a file's mark.
    void answer_vim(const vim::Asked& asked);
    // A file's mark set in the file in front: taken from every other file,
    // open or let go, as a file's mark is in one file at a time.
    void keep_file_mark(char name);
    // A jump from where the caret of the tab in front is, or from `from`
    // in its file, kept in its group's jump list.
    void keep_jump();
    void keep_jump(const vim::JumpFrom& from);
    // Ctrl-O and Ctrl-I: the jump list of the group in front stepped
    // `count` places, its file opened in front when it is another.
    void step_jumps(long count);
    // A file's mark the file in front hasn't: the file that has it opened
    // in front, its caret at the mark or on its line's first text, as
    // `wanted` says. Vim's E20 is said when no file has it.
    void go_to_file_mark(const vim::MarkWanted& wanted);
    // The keyboard into group `index`'s tab in front.
    void focus_group(std::size_t index);
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
    // keys.vim read: its maps for every view, its binds into the keymap on
    // the program's own keys, its settings applied, and what it says that
    // couldn't be used kept for the status bar.
    void read_keys();
    // keys.vim opened in a tab, made first with a few lines of comment when
    // there is none.
    void open_keys();
    // Whether one of the commands is called `name`: the program's, the
    // view's or the editor's.
    [[nodiscard]] bool has_command(std::string_view name) const;
    // Opens the file at `path` with `span` selected, the keyboard in it.
    void go_to(const std::filesystem::path& path, Span span);
    // clangd for C and C++, and fjls for a game's script headers and
    // fjell's own files, at the paths the preferences give them.
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
    // How the code is shown, which outlive the program, and the window
    // they are edited in.
    Preferences& preferences_;
    PreferencesWindow preferences_window_;
    // Vim's registers and what its :set changed, which every view with Vim
    // on shares.
    std::shared_ptr<vim::Registers> registers_{std::make_shared<vim::Registers>()};
    std::shared_ptr<vim::Settings> vim_settings_{std::make_shared<vim::Settings>()};
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
    // What the view in front's Vim asked of the program, answered once the
    // views are drawn.
    std::vector<vim::Asked> vim_asked_;
    // The files' marks (`A`-`Z`) of files let go, each with its file, until
    // the file opens again.
    struct FileMark {
        std::filesystem::path path;
        Position at;
    };
    std::map<char, FileMark> closed_marks_;
    // The folders listed after Vim's `:e`, each with or without its hidden
    // entries, until the disk is next looked at; and how every view's Vim
    // asks for them.
    std::map<std::pair<std::filesystem::path, bool>, std::vector<vim::PathEntry>> path_listings_;
    vim::PathLister path_lister_{[this](std::string_view folder, bool hidden) { return list_paths(folder, hidden); }};
    // A rename typed in place, asked of the services: the names and where,
    // and the services' answer to come.
    struct Renaming {
        Rename rename;
        Asked<ftk::Result<WorkspaceEdit>> asked;
    };
    std::optional<Renaming> renaming_;
    // keys.vim: where it is, its maps, which every view plays, and what it
    // says that couldn't be used.
    std::filesystem::path keys_path_;
    std::shared_ptr<const std::vector<vim::Mapping>> maps_{std::make_shared<const std::vector<vim::Mapping>>()};
    std::vector<vim::KeysError> keys_errors_;
    // What went wrong last, for the status bar.
    std::string note_;
    // What was just done (a rename), for the status bar until the next key.
    std::string told_;
    // fjedit's logomark, shown in a group with no file open, and the device
    // it is drawn on.
    std::optional<ftk::SvgPicture> mark_;
    ftk::gpu::Device* device_{nullptr};
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
