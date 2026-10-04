#pragma once

// fjedit as a program (sheet 39): one folder open in a window, its files
// down the side, the files open in tabs over the code, the menus along the
// top and the status bar along the bottom. It draws with ftk's kit inside
// whatever runs its frames: a window of its own, or a test's harness.
//
//     fjedit::Program program(folder);
//     app.run({.ui = [&](ftk::ui::core::Context&, glm::vec2) { program.draw(); }});

#include "fjedit/core/keymap.hpp"
#include "fjedit/core/workspace.hpp"
#include "fjedit/program/file_tree.hpp"
#include "fjedit/widget/code_view.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit {

class Program {
public:
    /// The folder `root` open.
    explicit Program(std::filesystem::path root);

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
    void take_keys();
    // Every few seconds: files another program changed, reloaded when they
    // have no unsaved changes here and noted when they do; and the tree
    // read again.
    void check_disk(double now);

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
    // What went wrong last, for the status bar.
    std::string note_;
};

} // namespace fjedit
