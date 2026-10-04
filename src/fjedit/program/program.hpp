#pragma once

// fjedit as a program (sheet 39): one folder open in a window, its files
// down the side, the files open in tabs over the code, the menus along the
// top and the status bar along the bottom. It draws with ftk's kit inside
// whatever runs its frames: a window of its own, or a test's harness.
//
//     fjedit::Program program(folder);
//     app.run({.ui = [&](ftk::ui::core::Context&, glm::vec2) { program.draw(); }});

#include "fjedit/core/keymap.hpp"
#include "fjedit/core/recovery.hpp"
#include "fjedit/core/workspace.hpp"
#include "fjedit/program/file_tree.hpp"
#include "fjedit/widget/code_view.hpp"

#include "ftk/ui/kit/dialog.hpp"

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
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


    [[nodiscard]] const Workspace& workspace() const { return workspace_; }

    /// The file in front, when one is open.
    [[nodiscard]] std::optional<std::filesystem::path> front() const;

    /// The keymap mode the program's own keys are bound in.
    static constexpr std::string_view PROGRAM = "program";

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
    void ask_recover();
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
    // shows it any more.
    void close_tab(Group& group, std::size_t index);
    void save(const std::filesystem::path& path);

    Workspace workspace_;
    Keymap keymap_;
    FileTree tree_;
    std::vector<Group> groups_;
    std::size_t active_{0};
    // A tab asked to close with unsaved changes, while it is asked about,
    // and whether the question is open.
    std::optional<std::filesystem::path> closing_;
    bool asking_{false};
    // The views show the minimap.
    bool minimap_{true};
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
    // Where the program's dialogs hang from: under the tabs.
    glm::vec2 dialog_at_{0.0f};
    // What went wrong last, for the status bar.
    std::string note_;
};

} // namespace fjedit
