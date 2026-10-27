#pragma once

// The code of an editor's files, side by side (sheet 39): groups of tabs,
// the file in front of each in a code view, and what goes on around them.
// That is the keys and keys.vim, Vim's `:` commands, the jumps and the
// files' marks across files, the questions about unsaved files, find in
// files and a rename made in every file. It draws in the room left in
// whatever window holds it: fjedit's own, or a tab of fjell's.
//
//     fjedit::CodeArea code(files, preferences, {.keys = CodeArea::keys_file()});
//     code.take_keys();
//     code.draw();
//     code.draw_find_in_files(over);
//     code.draw_prompts();

#include "fjedit/core/jump_list.hpp"
#include "fjedit/core/keymap.hpp"
#include "fjedit/editor/code_files.hpp"
#include "fjedit/editor/code_status.hpp"
#include "fjedit/editor/find_in_files.hpp"
#include "fjedit/editor/preferences.hpp"
#include "fjedit/widget/code_view.hpp"

#include "ftk/app/picture.hpp"
#include "ftk/base/thread_pool.hpp"
#include "ftk/ui/core/rect.hpp"
#include "ftk/ui/kit/dialog.hpp"

#include <glm/vec2.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ftk::gpu {
class Device;
}

namespace fjedit {

/// A command of the code area's, or of the program it is in: what keys call
/// it by, what a list of keys shows for it, and the key that runs it.
struct CodeCommand {
    std::string_view name;
    std::string_view title;
    std::optional<Chord> key{};
};

/// A file as a dialog's list shows it: its kind's icon, its name, and its
/// folder within `root` with `more` after it.
[[nodiscard]] ftk::ui::UnsavedItem file_item(const std::filesystem::path& path, const std::filesystem::path& root,
                                             std::string_view more = {});

/// What a code area starts with besides its files.
struct CodeAreaOptions {
    /// keys.vim, one's own maps for Vim's keys (decision 7 of the Vim plan),
    /// read at the start and again when saved here; none reads none.
    std::filesystem::path keys{};
    /// The commands of the program it is in: bound to their keys beside the
    /// area's own, named by keys.vim and by the keys that can come next.
    std::vector<CodeCommand> commands{};
    /// Runs one of `commands`; false when it can't.
    std::function<bool(std::string_view)> run{};
    /// What Vim's `:qa` asks of the program: to close at once, the unsaved
    /// changes given up by `:qa!`.
    std::function<void()> leave{};
    /// Shown in a group with no file open: the program's mark, drawn on
    /// `device`. None, or no device as in a test's harness, shows the kit's
    /// file icon.
    std::optional<ftk::SvgPicture> mark{};
    ftk::gpu::Device* device{nullptr};
    /// What the question about work kept from before calls the program.
    std::string program_name{"fjedit"};
};

class CodeArea {
public:
    /// The code of `files`, shown as `preferences` say; both outlive it.
    CodeArea(CodeFiles& files, Preferences& preferences, CodeAreaOptions options = {});

    // Its views and Vim hold `this`: the area stays where it was made.
    CodeArea(const CodeArea&) = delete;
    CodeArea& operator=(const CodeArea&) = delete;
    CodeArea(CodeArea&&) = delete;
    CodeArea& operator=(CodeArea&&) = delete;

    /// Where this machine keeps one's own maps for Vim's keys: keys.vim in
    /// fjedit's settings folder, beside settings.ini.
    [[nodiscard]] static std::filesystem::path keys_file();

    /// The area's own commands, which run() runs.
    [[nodiscard]] static std::span<const CodeCommand> commands();

    /// The keymap mode the area's commands and its program's are bound in
    /// (keys.vim's `program`).
    static constexpr std::string_view PROGRAM = "program";
    /// The mode they are bound in with Vim on: all but Ctrl W, which is
    /// Vim's.
    static constexpr std::string_view VIM_PROGRAM = "vim.program";

    /// Runs the commands whose keys were pressed this frame, and lets go of
    /// what was just done once a key is pressed. The program calls it while
    /// the keys are the code's.
    void take_keys();

    /// Draws the groups side by side in the room left in the window being
    /// drawn, filling it, then does what their views asked.
    void draw();

    /// Draws find in files, while it is open, floating at the top of `over`.
    void draw_find_in_files(ftk::ui::core::Rect over);

    /// Draws the questions waiting: work kept from before, closing a tab
    /// with unsaved changes; and makes a rename once its edits come.
    void draw_prompts();

    /// Where its questions hang from: the middle of the top of the code,
    /// under the tabs once there are some.
    [[nodiscard]] glm::vec2 prompts_at() const { return prompts_at_; }

    /// What the files' update means for the views: folders listed again,
    /// and what was asked of a server started again let go.
    void files_updated(const CodeFiles::Update& update);

    /// Opens the file at `path` in a tab in front with the keyboard in it,
    /// or brings its tab forward when it is open; nothing but a note when it
    /// can't be read.
    void open(const std::filesystem::path& path);

    /// Opens the file at `path` with `span` selected, the keyboard in it.
    void go_to(const std::filesystem::path& path, Span span);

    /// Runs one of the area's commands or a view's by name ("file.save"), or
    /// one of the program's it was given; false when there is none.
    bool run(std::string_view command);

    /// Writes the file at `path`, as Save does: keys.vim saved here applies
    /// at once. What went wrong is noted on the files.
    void save(const std::filesystem::path& path);

    /// The keys a menu shows for `command` ("Ctrl S"): the area's, else
    /// those of the view's keys; with Vim on, those of the mode the view in
    /// front is in first ("u", "\"+dd", one's own binds), then the area's.
    /// Empty when no keys run it.
    [[nodiscard]] std::string shortcut(std::string_view command) const;

    /// The file in front in the group that last had the keyboard, when one
    /// is open there.
    [[nodiscard]] std::optional<std::filesystem::path> front() const;

    /// What the code says on a status bar now, of the file in front and the
    /// files open.
    [[nodiscard]] CodeStatus status();

    /// How many groups of tabs stand side by side: one, or more once split.
    [[nodiscard]] std::size_t groups() const { return groups_.size(); }

    /// Find in files, open or not.
    [[nodiscard]] const FindInFiles& find_in_files() const { return find_in_files_; }

    /// What Vim's :set changed for the session, which every view's Vim
    /// shares and the views show over the preferences.
    [[nodiscard]] const vim::Settings& vim_settings() const { return *vim_settings_; }

    /// keys.vim, where it is: empty when none is read.
    [[nodiscard]] const std::filesystem::path& keys_path() const { return keys_path_; }
    /// What keys.vim says that couldn't be used, by line.
    [[nodiscard]] const std::vector<vim::KeysError>& keys_errors() const { return keys_errors_; }
    /// keys.vim opened in a tab, made first with a few lines of comment when
    /// there is none.
    void open_keys();

    /// What was just done (a rename), until the next key.
    [[nodiscard]] const std::string& told() const { return told_; }

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

    void draw_group(Group& group, std::size_t index);
    void ask_close();
    void ask_recover();
    // A new name for what a view asked to rename, then the service's
    // edits for it, made once they come.
    void ask_rename();

    // The tab in front, when there is one.
    [[nodiscard]] Tab* front_tab();
    [[nodiscard]] const Tab* front_tab() const;
    // Closes the tab in front, asking first when its file has unsaved
    // changes that no other tab shows.
    void close_front();
    // How many tabs show the file at `path`, in every group.
    [[nodiscard]] std::size_t tabs_showing(const std::filesystem::path& path) const;
    // The folder `folder`, typed after Vim's `:e` from the folder open,
    // listed as the file tree lists it; hidden ones too with `hidden`.
    [[nodiscard]] std::vector<vim::PathEntry> list_paths(std::string_view folder, bool hidden);
    // Runs a `:` command of Vim's that is the area's, handed up by the view
    // in front, as Vim runs it: what would lose unsaved changes is refused,
    // said where Vim says it, unless the command has a !.
    void run_ex(const vim::ExCommand& command);
    // Answers what the view in front's Vim asked: runs its `:` command, or
    // keeps or goes to a file's mark.
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
    // A view of `document`, the file at `path`, joined to the language
    // services, which are told of the file when one answers for it.
    [[nodiscard]] std::unique_ptr<EditorView> make_view(const std::filesystem::path& path, Document& document);
    // `view` shown as the preferences have it, but where Vim's :set changed
    // it for the session.
    void show_as_preferred(EditorView& view);
    // Lets the file at `path` go: the places jumped from in it wait for it
    // to open again in each group's jump list, then the files let it go.
    void let_go(const std::filesystem::path& path);
    // keys.vim read: its maps for every view, its binds into the keymap on
    // the area's own keys, its settings applied, and what it says that
    // couldn't be used kept.
    void read_keys();
    // Whether one of the commands is called `name`: the area's, its
    // program's, the view's or the editor's.
    [[nodiscard]] bool has_command(std::string_view name) const;
    // Whether `name` is one of the program's commands.
    [[nodiscard]] bool program_command(std::string_view name) const;
    // A command's title, from the area's commands or its program's.
    [[nodiscard]] std::string command_title(std::string_view name) const;

    CodeFiles& files_;
    Preferences& preferences_;
    // The program's commands, what runs them, and what Vim's :qa asks of
    // it.
    std::vector<CodeCommand> program_commands_;
    std::function<bool(std::string_view)> run_program_;
    std::function<void()> leave_;
    Keymap keymap_;
    std::vector<Group> groups_;
    std::size_t active_{0};
    // A tab asked to close with unsaved changes, while it is asked about,
    // and whether the question is open.
    std::optional<std::filesystem::path> closing_;
    bool asking_{false};
    // Vim's registers and what its :set changed, which every view with Vim
    // on shares.
    std::shared_ptr<vim::Registers> registers_{std::make_shared<vim::Registers>()};
    std::shared_ptr<vim::Settings> vim_settings_{std::make_shared<vim::Settings>()};
    // Work kept from before, offered back once at the start, and the list
    // asked about; and what the question calls the program.
    bool recover_looked_{false};
    std::vector<Recovery::Kept> recovering_;
    std::vector<ftk::ui::UnsavedItem> recover_items_;
    std::string program_name_;
    // Where the questions hang from: under the tabs.
    glm::vec2 prompts_at_{0.0f};
    // The threads work is done on away from the frames: searches. Before
    // what uses them, so it goes first.
    ftk::ThreadPool pool_;
    FindInFiles find_in_files_{files_.workspace(), pool_};
    // A place a view asked to go to in another file, taken once the views
    // are drawn.
    std::optional<Location> going_;
    // What the view in front's Vim asked, answered once the views are
    // drawn.
    std::vector<vim::Asked> vim_asked_;
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
    // What was just done (a rename), until the next key.
    std::string told_;
    // The mark shown in a group with no file open, and the device it is
    // drawn on.
    std::optional<ftk::SvgPicture> mark_;
    ftk::gpu::Device* device_{nullptr};
};

} // namespace fjedit
