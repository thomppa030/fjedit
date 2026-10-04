#include "fjedit/program/program.hpp"

#include "fjedit/core/commands.hpp"
#include "fjedit/core/layout.hpp"

#include "ftk/ui/core/context.hpp"
#include "ftk/ui/kit/dialog.hpp"
#include "ftk/ui/kit/feedback.hpp"
#include "ftk/ui/kit/icons.hpp"
#include "ftk/ui/kit/layout.hpp"
#include "ftk/ui/kit/menu.hpp"
#include "ftk/ui/kit/pane.hpp"
#include "ftk/ui/kit/region.hpp"
#include "ftk/ui/kit/scope.hpp"
#include "ftk/ui/kit/status_bar.hpp"
#include "ftk/ui/kit/tabs.hpp"
#include "ftk/ui/kit/text.hpp"
#include "ftk/ui/kit/viewport_toolbar.hpp"
#include "ftk/ui/kit/window.hpp"
#include "ftk/ui/theme.hpp"

#include <algorithm>
#include <format>
#include <iterator>

namespace fjedit {

namespace {

namespace ui = ftk::ui;
namespace theme = ftk::theme;
using ftk::ui::Key;

// The side bar's width (sheet 39).
constexpr float SIDE_WIDTH = 260.0f;
// How often the disk is looked at for files changed elsewhere.
constexpr double DISK_CHECK = 2.0;

// The standard keys, with the program's own in its mode.
Keymap program_keys() {
    Keymap keys = Keymap::standard();
    const auto ctrl = [](Key key) { return Chord{.key = key, .modifiers = {.ctrl = true}}; };
    keys.bind(Program::PROGRAM, {ctrl(Key::S)}, "file.save");
    keys.bind(Program::PROGRAM, {ctrl(Key::W)}, "file.close");
    keys.bind(Program::PROGRAM, {ctrl(Key::PageDown)}, "tab.next");
    keys.bind(Program::PROGRAM, {ctrl(Key::PageUp)}, "tab.previous");
    return keys;
}

// The label a file's tab is drawn and known by: its name, and its path
// after ## so two files of one name each have their own.
std::string tab_label(const std::filesystem::path& path) {
    return path.filename().string() + "##" + path.generic_string();
}

// The keys that run `command`, as a menu shows them: the program's, else a
// view's.
std::string shortcut(const Keymap& keys, std::string_view command) {
    std::optional<std::vector<Chord>> chords = keys.keys_for(Program::PROGRAM, command);
    if (!chords) chords = keys.keys_for(Keymap::EDITING, command);
    if (!chords) return {};
    std::string out;
    for (const Chord& chord : *chords) {
        if (!out.empty()) out += ' ';
        out += chord_text(chord);
    }
    return out;
}

} // namespace

Program::Program(std::filesystem::path root)
    : workspace_(std::move(root)), keymap_(program_keys()), tree_(workspace_.root()), groups_(1) {}

std::optional<std::filesystem::path> Program::front() const {
    const Tab* tab = front_tab();
    if (tab == nullptr) return std::nullopt;
    return tab->path;
}

Program::Tab* Program::front_tab() {
    Group& group = groups_[active_];
    return group.front < group.tabs.size() ? &group.tabs[group.front] : nullptr;
}

const Program::Tab* Program::front_tab() const {
    const Group& group = groups_[active_];
    return group.front < group.tabs.size() ? &group.tabs[group.front] : nullptr;
}

void Program::open(const std::filesystem::path& path) {
    const ftk::Result<Document*> document = workspace_.open(path);
    if (!document) {
        note_ = document.error();
        return;
    }
    note_.clear();
    Group& group = groups_[active_];
    const std::filesystem::path named = workspace_.name(path);
    auto found = std::ranges::find(group.tabs, named, &Tab::path);
    if (found == group.tabs.end()) {
        group.tabs.push_back({.path = named, .view = std::make_unique<EditorView>(**document)});
        found = std::prev(group.tabs.end());
    }
    const auto index = static_cast<std::size_t>(std::distance(group.tabs.begin(), found));
    group.front = index;
    group.select = index;
    found->view->focus = true;
}

bool Program::run(std::string_view command) {
    Group& group = groups_[active_];
    Tab* tab = front_tab();
    if (command == "file.save") {
        if (tab != nullptr) save(tab->path);
    } else if (command == "file.save_all") {
        for (const std::filesystem::path& path : workspace_.files()) {
            if (workspace_.unsaved(path)) save(path);
        }
    } else if (command == "file.close") {
        close_front();
    } else if (command == "tab.next" || command == "tab.previous") {
        if (group.tabs.empty()) return true;
        const std::size_t count = group.tabs.size();
        group.select = command == "tab.next" ? (group.front + 1) % count : (group.front + count - 1) % count;
    } else if (find_command(command) != nullptr || is_view_command(command)) {
        // The view in front's: it runs at the view's next draw.
        if (tab != nullptr) tab->view->queued.emplace_back(command);
    } else {
        return false;
    }
    return true;
}

void Program::save(const std::filesystem::path& path) {
    if (const ftk::Result<> saved = workspace_.save(path); !saved) {
        note_ = saved.error();
        return;
    }
    changed_.erase(workspace_.name(path));
    note_.clear();
}

void Program::check_disk(double now) {
    ui::core::current().request_frame_at(std::max(next_check_, now));
    if (now < next_check_) return;
    next_check_ = now + DISK_CHECK;
    for (const std::filesystem::path& path : workspace_.changed_on_disk()) {
        if (workspace_.unsaved(path)) {
            changed_.insert(path);
        } else if (const ftk::Result<> reloaded = workspace_.reload(path); !reloaded) {
            note_ = reloaded.error();
        }
    }
    tree_.forget_listings();
}

void Program::close_front() {
    const Tab* tab = front_tab();
    if (tab == nullptr) return;
    // Another tab showing the file keeps its changes: closing this one
    // loses nothing.
    std::size_t showing = 0;
    for (const Group& group : groups_) showing += static_cast<std::size_t>(std::ranges::count(group.tabs, tab->path, &Tab::path));
    if (showing == 1 && workspace_.unsaved(tab->path)) {
        closing_ = tab->path;
        return;
    }
    close_tab(groups_[active_], groups_[active_].front);
}

void Program::close_tab(Group& group, std::size_t index) {
    if (index >= group.tabs.size()) return;
    const std::filesystem::path path = group.tabs[index].path;
    group.tabs.erase(group.tabs.begin() + static_cast<std::ptrdiff_t>(index));
    // The tab in front stays in front; closed, the one after it takes its
    // place, or the one before when it was the last.
    if (group.front > index || (group.front == group.tabs.size() && group.front > 0)) --group.front;
    if (!group.tabs.empty()) group.select = group.front;
    const bool shown = std::ranges::any_of(groups_, [&](const Group& other) {
        return std::ranges::contains(other.tabs, path, &Tab::path);
    });
    if (!shown) workspace_.close(path);
}

void Program::take_keys() {
    ui::core::Context& context = ui::core::current();
    const ui::Modifiers held = context.modifiers();
    for (std::size_t k = 0; k < ui::KEY_COUNT; ++k) {
        const auto key = static_cast<Key>(k);
        if (!context.key_pressed(key)) continue;
        const KeyMatch match = keymap_.find(PROGRAM, std::vector<Chord>{{.key = key, .modifiers = held}});
        if (match.command) (void)run(*match.command);
    }
}

void Program::draw() {
    check_disk(ui::core::current().time());
    take_keys();
    draw_menus();
    const ui::WorkArea work = ui::work_area();
    const float height = work.size.y - theme::STATUS_BAR;
    ui::next_window_place(work.pos, {work.size.x, height});
    (void)ui::begin_host_window("##fjedit", {.scrollbar = false, .to_front = false});
    if (auto side = ui::Pane("##side", {SIDE_WIDTH, height})) draw_side();
    ui::same_line(0.0f);
    if (auto editor = ui::Region("##editor", {0.0f, height}, {.wheel_scrolls = false})) draw_group(groups_[0], 0);
    ui::end_host_window();
    draw_status();
    draw_prompts();
}

void Program::draw_menus() {
    const bool open_file = front_tab() != nullptr;
    const auto entry = [&](std::string label, std::string_view command, bool enabled = true) {
        return ui::MenuEntry{.label = std::move(label),
                             .shortcut = shortcut(keymap_, command),
                             .enabled = enabled,
                             .chosen = [this, command] { (void)run(command); }};
    };
    const std::vector<ui::Menu> menus{
        {.label = "File",
         .entries = [&] {
             return std::vector<ui::MenuEntry>{entry("Save", "file.save", open_file), entry("Save all", "file.save_all"),
                                               ui::MenuEntry::separator(), entry("Close", "file.close", open_file)};
         }},
        {.label = "Edit",
         .entries = [&] {
             return std::vector<ui::MenuEntry>{
                 entry("Undo", "edit.undo", open_file),     entry("Redo", "edit.redo", open_file),
                 ui::MenuEntry::separator(),                entry("Cut", "edit.cut", open_file),
                 entry("Copy", "edit.copy", open_file),     entry("Paste", "edit.paste", open_file),
                 ui::MenuEntry::separator(),                entry("Find", "find.open", open_file),
                 entry("Replace", "find.replace", open_file)};
         }},
        {.label = "View",
         .entries = [&] {
             return std::vector<ui::MenuEntry>{{.label = "Minimap",
                                                .checked = minimap_,
                                                .chosen = [this] { minimap_ = !minimap_; }}};
         }},
        {.label = "Go",
         .entries = [&] { return std::vector<ui::MenuEntry>{entry("Go to line", "goto.open", open_file)}; }},
    };
    ui::menu_bar(menus);
}

void Program::draw_side() {
    {
        const ui::FontScope bold(ui::Font::Bold, 0.0f);
        ui::align_text_to_frame();
        ui::text(std::string(ui::icon::folder) + "  Files");
    }
    ui::separator();
    const std::optional<std::filesystem::path> shown = front();
    if (const std::optional<std::filesystem::path> pressed = tree_.draw(shown.value_or(std::filesystem::path{}))) {
        open(*pressed);
    }
}

void Program::draw_group(Group& group, std::size_t index) {
    const ui::IdScope scope(static_cast<int>(index));
    if (group.tabs.empty()) {
        (void)ui::empty_state(ui::icon::file, "No file open", "Open one from the files on the left");
        return;
    }
    std::optional<std::size_t> closed;
    if (auto tabs = ui::DocumentTabs("##tabs")) {
        for (std::size_t i = 0; i < group.tabs.size(); ++i) {
            const Tab& tab = group.tabs[i];
            const std::string label = tab_label(tab.path);
            const FileLook look = file_look(tab.path);
            const ui::DocumentTabResult result = ui::document_tab({.label = label.c_str(),
                                                                   .icon = look.icon,
                                                                   .hue = look.hue,
                                                                   .unsaved = workspace_.unsaved(tab.path),
                                                                   .select = group.select == i});
            if (result.front) group.front = i;
            if (result.closed) closed = i;
        }
    }
    group.select.reset();

    // The code of the tab in front, in the space under the tabs, under a
    // strip while its file changed on disk with unsaved changes here.
    if (group.front < group.tabs.size()) {
        Tab& tab = group.tabs[group.front];
        if (changed_.contains(tab.path)) {
            const std::string text =
                std::format("{} changed on disk. It has unsaved changes here.", tab.path.filename().string());
            const char* answers[] = {"Keep mine", "Load from disk"};
            if (const std::optional<std::size_t> answer = ui::notice_strip(ui::Severity::Warning, text.c_str(), answers)) {
                changed_.erase(tab.path);
                if (*answer == 1) {
                    if (const ftk::Result<> reloaded = workspace_.reload(tab.path); !reloaded) note_ = reloaded.error();
                }
            }
        }
        tab.view->minimap = minimap_;
        Document* document = workspace_.find(tab.path);
        const glm::vec2 at = ui::cursor();
        const glm::vec2 room = ui::space_left();
        if (document != nullptr && room.x > 0.0f && room.y > 0.0f) {
            const CodeViewResult result = code_view("Code", *document, *tab.view, keymap_, {.min = at, .max = at + room});
            if (result.focused) active_ = index;
            ui::reserve(room);
        }
    }
    if (closed) {
        group.front = *closed;
        active_ = index;
        close_front();
    }
}

void Program::draw_status() {
    if (auto bar = ui::StatusBar()) {
        if (!note_.empty()) ui::status_item(ui::icon::warning, note_);
        bar.right();
        const Tab* tab = front_tab();
        const Document* document = tab != nullptr ? workspace_.find(tab->path) : nullptr;
        if (document == nullptr) return;
        const Buffer& buffer = document->buffer();
        const Position caret = tab->view->selections.primary().caret;
        const Indent indent = document->indent();
        const std::size_t column = column_of(buffer.line(caret.line), caret.byte, std::max<std::size_t>(indent.width, 1));
        ui::status_item(nullptr, std::format("Ln {}, Col {}", caret.line + 1, column + 1));
        ui::status_rule();
        ui::status_item(nullptr, std::format("{}: {}", indent.tabs ? "Tabs" : "Spaces", indent.width));
        ui::status_rule();
        ui::status_item(nullptr, buffer.byte_order_mark() ? "UTF-8 with BOM" : "UTF-8");
        ui::status_rule();
        ui::status_item(nullptr, document->language().name);
    }
}

void Program::draw_prompts() {
    if (!closing_) return;
    if (!asking_) {
        ui::open_dialog("##unsaved");
        asking_ = true;
    }
    const std::string title = std::format("Save changes to {}?", closing_->filename().string());
    const ui::UnsavedAnswer answer =
        ui::unsaved_dialog("##unsaved", title.c_str(), "What you changed is lost if you don't save it.");
    if (answer == ui::UnsavedAnswer::None) return;
    const std::filesystem::path path = *closing_;
    closing_.reset();
    asking_ = false;
    if (answer == ui::UnsavedAnswer::Cancel) return;
    if (answer == ui::UnsavedAnswer::Save) {
        save(path);
        if (workspace_.unsaved(path)) return;
    }
    Group& group = groups_[active_];
    const auto found = std::ranges::find(group.tabs, path, &Tab::path);
    if (found != group.tabs.end()) close_tab(group, static_cast<std::size_t>(std::distance(group.tabs.begin(), found)));
}

} // namespace fjedit
