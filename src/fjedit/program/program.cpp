#include "fjedit/program/program.hpp"

#include "fjedit/core/commands.hpp"
#include "fjedit/core/find.hpp"
#include "fjedit/core/language.hpp"
#include "fjedit/core/layout.hpp"
#include "fjedit/core/text_file.hpp"
#include "fjedit/lsp/client.hpp"
#include "fjedit/program/icons.hpp"

#include "ftk/platform/platform.hpp"

#include "ftk/ui/core/context.hpp"
#include "ftk/ui/draw_list.hpp"
#include "ftk/ui/kit/button.hpp"
#include "ftk/ui/kit/dialog.hpp"
#include "ftk/ui/kit/feedback.hpp"
#include "ftk/ui/kit/icons.hpp"
#include "ftk/ui/kit/layout.hpp"
#include "ftk/ui/kit/menu.hpp"
#include "ftk/ui/kit/pane.hpp"
#include "ftk/ui/kit/region.hpp"
#include "ftk/ui/kit/scope.hpp"
#include "ftk/ui/kit/settings_file.hpp"
#include "ftk/ui/kit/status_bar.hpp"
#include "ftk/ui/kit/tabs.hpp"
#include "ftk/ui/kit/text.hpp"
#include "ftk/ui/kit/viewport_toolbar.hpp"
#include "ftk/ui/kit/window.hpp"
#include "ftk/ui/theme.hpp"

#include <algorithm>
#include <format>
#include <iterator>
#include <utility>

namespace fjedit {

namespace {

namespace ui = ftk::ui;
namespace theme = ftk::theme;
using ftk::ui::Key;

// The side bar's width (sheet 39).
constexpr float SIDE_WIDTH = 260.0f;
// How often the disk is looked at for files changed elsewhere.
constexpr double DISK_CHECK = 2.0;
// The buttons at the end of a tab row, in from its right edge.
constexpr float TAB_END_PAD = 4.0f;

// The standard keys, with the program's own in its mode.
Keymap program_keys() {
    Keymap keys = Keymap::standard();
    const auto ctrl = [](Key key) { return Chord{.key = key, .modifiers = {.ctrl = true}}; };
    keys.bind(Program::PROGRAM, {ctrl(Key::S)}, "file.save");
    keys.bind(Program::PROGRAM, {ctrl(Key::W)}, "file.close");
    keys.bind(Program::PROGRAM, {ctrl(Key::Q)}, "file.quit");
    keys.bind(Program::PROGRAM, {ctrl(Key::Backslash)}, "view.split");
    keys.bind(Program::PROGRAM, {{.key = Key::F, .modifiers = {.ctrl = true, .shift = true}}}, "files.find");
    keys.bind(Program::PROGRAM, {ctrl(Key::PageDown)}, "tab.next");
    keys.bind(Program::PROGRAM, {ctrl(Key::PageUp)}, "tab.previous");
    keys.bind(Program::PROGRAM, {ctrl(Key::Comma)}, "edit.preferences");
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

// How many lines `kept` and the file at `path` differ in, between the lines
// alike at their starts and ends; nothing when the file can't be read.
std::optional<std::size_t> lines_changed(const std::filesystem::path& path, std::string_view kept) {
    const ftk::Result<std::string> disk = read_text_file(path);
    if (!disk) return std::nullopt;
    const Buffer a(kept);
    const Buffer b(*disk);
    const std::size_t shorter = std::min(a.line_count(), b.line_count());
    std::size_t head = 0;
    while (head < shorter && a.line(head) == b.line(head)) ++head;
    std::size_t tail = 0;
    while (head + tail < shorter && a.line(a.line_count() - 1 - tail) == b.line(b.line_count() - 1 - tail)) ++tail;
    return std::max(a.line_count(), b.line_count()) - head - tail;
}

// A file as a dialog's list shows it: its kind's icon, its name, and its
// folder within `root` with `more` after it.
ui::UnsavedItem listed(const std::filesystem::path& path, const std::filesystem::path& root, std::string_view more) {
    const FileLook look = file_look(path);
    std::string folder = path.parent_path().lexically_relative(root).generic_string();
    if (folder == ".") folder.clear();
    std::string detail = folder;
    if (!more.empty()) detail += detail.empty() ? std::string(more) : "  " + std::string(more);
    return {.icon = look.icon,
            .icon_colour = look.hue ? theme::hue(*look.hue) : theme::text_secondary(),
            .name = path.filename().string(),
            .detail = std::move(detail)};
}

} // namespace

Program::Program(std::filesystem::path root, ProgramOptions options)
    : workspace_(std::move(root)), keymap_(program_keys()), tree_(workspace_.root()), groups_(1),
      wake_(std::move(options.wake)),
      services_(noting_paths(options.services ? std::move(*options.services) : std::vector<ServiceMaker>{clangd()}),
                workspace_.root()) {
    if (!options.recovery.empty()) recovery_.emplace(std::move(options.recovery));
}

ServiceMaker Program::clangd() {
    static const std::vector<std::string> EXTENSIONS{"c", "cc", "cpp", "cxx", "h", "hh", "hpp", "hxx", "inl", "ipp"};
    return {.name = "clangd",
            .extensions = EXTENSIONS,
            .make = [this](const std::filesystem::path& root) -> std::unique_ptr<LanguageService> {
                return std::make_unique<lsp::Client>(lsp::ClientOptions{
                    // No mark before a completion's name for whether it
                    // brings an #include: the name is the name.
                    .command = {preferences_.clangd, "--log=error", "--header-insertion-decorators=0"},
                    .root = root,
                    .extensions = EXTENSIONS,
                    .language_id = [](const std::filesystem::path& path) {
                        return path.extension() == ".c" ? std::string("c") : std::string("cpp");
                    },
                    .arrived = wake_,
                });
            }};
}

std::vector<ServiceMaker> Program::noting_paths(std::vector<ServiceMaker> makers) {
    for (ServiceMaker& maker : makers) {
        if (maker.name != "clangd" || !maker.make) continue;
        maker.make = [this, make = std::move(maker.make)](const std::filesystem::path& root) {
            clangd_started_ = preferences_.clangd;
            return make(root);
        };
    }
    return makers;
}

void Program::follow_server_paths() {
    if (!services_.status_of("clangd") || clangd_started_ == preferences_.clangd) return;
    services_.restart("clangd");
    // What a view asked of the one stopped will never be answered.
    for (Group& group : groups_) {
        for (Tab& tab : group.tabs) {
            tab.view->completing = {};
            tab.view->hovering = {};
            tab.view->going.reset();
            tab.view->formatting.reset();
        }
    }
    renaming_.reset();
}

LanguageServerShown Program::clangd_shown() const {
    LanguageServerShown shown{.status = services_.status_of("clangd"), .reads = {}};
    // Where clangd looks for them in the folder open, as it does from a
    // file at its top.
    for (const char* name : {"compile_commands.json", "build/compile_commands.json", "compile_flags.txt"}) {
        std::error_code error;
        if (std::filesystem::exists(workspace_.root() / name, error)) {
            shown.reads = name;
            break;
        }
    }
    return shown;
}

std::unique_ptr<EditorView> Program::make_view(const std::filesystem::path& path, Document& document) {
    auto view = std::make_unique<EditorView>(document);
    view->minimap = preferences_.minimap;
    view->text_size = static_cast<float>(preferences_.text_size);
    if (services_.serves(path)) {
        view->service = &services_;
        view->path = path;
        services_.open(path, document);
    }
    return view;
}

void Program::let_go(const std::filesystem::path& path) {
    services_.close(path);
    workspace_.close(path);
}

void Program::go_to(const std::filesystem::path& path, Span span) {
    open(path);
    Tab* tab = front_tab();
    const Document* document = tab != nullptr ? workspace_.find(tab->path) : nullptr;
    if (document == nullptr) return;
    const Buffer& buffer = document->buffer();
    tab->view->selections = Selections({.anchor = buffer.clamp(span.from), .caret = buffer.clamp(span.to)});
    tab->view->reveal = true;
}

std::filesystem::path Program::recovery_folder(const std::filesystem::path& root) {
    return Recovery::folder_for(ftk::platform::state_dir() / "fjedit" / "recovery", Workspace(root).root());
}

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
        group.tabs.push_back({.path = named, .view = make_view(named, **document)});
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
    } else if (command == "file.quit") {
        (void)ask_to_quit();
    } else if (command == "view.split") {
        split(active_);
    } else if (command == "files.find") {
        open_find_in_files();
    } else if (command == "edit.preferences") {
        preferences_window_.open();
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
    forget_kept(workspace_.name(path));
    services_.saved(workspace_.name(path));
    note_.clear();
}

void Program::keep_unsaved() {
    if (!recovery_) return;
    for (const std::filesystem::path& path : workspace_.files()) {
        if (!workspace_.unsaved(path)) {
            forget_kept(path);
            continue;
        }
        const Document& document = *workspace_.find(path);
        const auto kept = kept_.find(path);
        if (kept != kept_.end() && kept->second == document.version()) continue;
        if (const ftk::Result<> written = recovery_->keep(path, document.buffer().text()); !written) {
            note_ = written.error();
            continue;
        }
        kept_[path] = document.version();
    }
}

void Program::forget_kept(const std::filesystem::path& path) {
    if (kept_.erase(path) > 0 && recovery_) recovery_->forget(path);
}

bool Program::ask_to_quit() {
    if (quitting_) return true;
    const std::vector<std::filesystem::path> files = workspace_.files();
    if (std::ranges::none_of(files, [&](const std::filesystem::path& path) { return workspace_.unsaved(path); })) {
        quitting_ = true;
        return true;
    }
    quit_asked_ = true;
    return false;
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
    keep_unsaved();
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
    if (!shown) {
        let_go(path);
        forget_kept(path);
    }
    if (group.tabs.empty() && groups_.size() > 1) {
        const auto gone = static_cast<std::size_t>(&group - groups_.data());
        groups_.erase(groups_.begin() + static_cast<std::ptrdiff_t>(gone));
        if (active_ >= gone && active_ > 0) --active_;
    }
}

void Program::split(std::size_t index) {
    if (index >= groups_.size()) return;
    const Group& from = groups_[index];
    if (from.front >= from.tabs.size()) return;
    const Tab& shown = from.tabs[from.front];
    Document* document = workspace_.find(shown.path);
    if (document == nullptr) return;
    Group beside;
    auto view = make_view(shown.path, *document);
    view->selections = shown.view->selections;
    view->scroll = shown.view->scroll;
    view->focus = true;
    beside.tabs.push_back({.path = shown.path, .view = std::move(view)});
    beside.select = 0;
    groups_.insert(groups_.begin() + static_cast<std::ptrdiff_t>(index + 1), std::move(beside));
    active_ = index + 1;
}

void Program::open_find_in_files() {
    std::string seed;
    if (const Tab* tab = front_tab()) {
        const Selection& primary = tab->view->selections.primary();
        const Span span = primary.span();
        const Document* document = workspace_.find(tab->path);
        if (document != nullptr && !primary.empty() && span.from.line == span.to.line) {
            seed = document->buffer().text(span);
        }
    }
    find_in_files_.open(seed);
}

void Program::take_find_in_files(const FindInFilesAsk& ask) {
    for (const std::filesystem::path& path : ask.shown) {
        const bool in_a_tab = std::ranges::any_of(groups_, [&](const Group& group) {
            return std::ranges::contains(group.tabs, path, &Tab::path);
        });
        if (in_a_tab) continue;
        // Changed in the preview, it gets a tab to be saved from; otherwise
        // it goes.
        if (workspace_.unsaved(path)) {
            Group& group = groups_[active_];
            group.tabs.push_back({.path = path, .view = make_view(path, *workspace_.find(path))});
        } else {
            let_go(path);
        }
    }
    if (ask.go) go_to(ask.go->path, ask.go->span);
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
    follow_server_paths();
    // What the language services found since, before anything shows it.
    services_.update();
    check_disk(ui::core::current().time());
    take_keys();
    draw_menus();
    const ui::WorkArea work = ui::work_area();
    const float height = work.size.y - theme::STATUS_BAR;
    ui::next_window_place(work.pos, {work.size.x, height});
    (void)ui::begin_host_window("##fjedit", {.scrollbar = false, .to_front = false});
    if (auto side = ui::Pane("##side", {SIDE_WIDTH, height})) draw_side();
    ui::same_line(0.0f);
    if (auto editor = ui::Region("##editor", {0.0f, height}, {.wheel_scrolls = false})) {
        // Dialogs hang from the top of the editor, under the tabs once
        // there are some.
        dialog_at_ = ui::cursor() + glm::vec2{ui::space_left().x * 0.5f, 0.0f};
        const glm::vec2 code_min = ui::cursor();
        const glm::vec2 code_max = code_min + ui::space_left();
        // The groups side by side, each but the last as wide as it was left
        // and a rule after it.
        const float each = ui::space_left().x / static_cast<float>(groups_.size());
        for (std::size_t index = 0; index < groups_.size(); ++index) {
            const bool last = index + 1 == groups_.size();
            if (index > 0) ui::same_line(0.0f);
            const std::string id = std::format("##group{}", index);
            if (auto region = ui::Region(id.c_str(), {last ? 0.0f : each, height},
                                         {.resize_x = !last, .wheel_scrolls = false})) {
                draw_group(groups_[index], index);
                if (!last) {
                    const glm::vec2 max = ui::window_content_max();
                    ui::DrawList::window().rect_filled({max.x - 1.0f, ui::window_content_min().y}, max, theme::border());
                }
            }
            // A group may have closed its last tab and gone.
            if (index >= groups_.size()) break;
        }
        // Find in files floats over the code, under the tabs.
        const std::optional<std::filesystem::path> shown = front();
        const std::filesystem::path folder = shown ? shown->parent_path() : workspace_.root();
        const float tabs = dialog_at_.y - code_min.y;
        take_find_in_files(find_in_files_.draw({.min = {code_min.x, code_min.y + tabs}, .max = code_max}, folder, keymap_,
                                               static_cast<float>(preferences_.text_size)));
    }
    if (const std::optional<Location> going = std::exchange(going_, std::nullopt)) go_to(going->path, going->span);
    ui::end_host_window();
    draw_status();
    preferences_window_.draw(preferences_, clangd_shown());
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
                                               ui::MenuEntry::separator(), entry("Close", "file.close", open_file),
                                               ui::MenuEntry::separator(), entry("Quit", "file.quit")};
         }},
        {.label = "Edit",
         .entries = [&] {
             return std::vector<ui::MenuEntry>{
                 entry("Undo", "edit.undo", open_file),     entry("Redo", "edit.redo", open_file),
                 ui::MenuEntry::separator(),                entry("Cut", "edit.cut", open_file),
                 entry("Copy", "edit.copy", open_file),     entry("Paste", "edit.paste", open_file),
                 ui::MenuEntry::separator(),                entry("Find", "find.open", open_file),
                 entry("Replace", "find.replace", open_file), ui::MenuEntry::separator(),
                 entry("Rename", "symbol.rename", open_file), entry("Format", "format.document", open_file),
                 ui::MenuEntry::separator(),                entry("Preferences", "edit.preferences")};
         }},
        {.label = "View",
         .entries = [&] {
             return std::vector<ui::MenuEntry>{{.label = "Minimap",
                                                .checked = preferences_.minimap,
                                                .chosen = [this] {
                                                    preferences_.minimap = !preferences_.minimap;
                                                    ui::settings_changed();
                                                }}};
         }},
        {.label = "Go",
         .entries = [&] {
             return std::vector<ui::MenuEntry>{entry("Go to line", "goto.open", open_file),
                                               entry("Go to definition", "definition.go", open_file),
                                               entry("Find in files", "files.find")};
         }},
    };
    ui::menu_bar(menus);
}

void Program::draw_side() {
    const glm::vec2 top = ui::cursor();
    const float width = ui::space_left().x;
    {
        const ui::FontScope bold(ui::Font::Bold, 0.0f);
        ui::align_text_to_frame();
        ui::text(std::string(ui::icon::folder) + "  Files");
    }
    // At the heading's end, find in files.
    const glm::vec2 below = ui::cursor();
    ui::set_cursor({top.x + width - ui::frame_height(), top.y});
    if (ui::icon_button("##find_in_files", ui::icon::search, "Find in files (Ctrl Shift F)")) open_find_in_files();
    ui::set_cursor(below);
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
    const glm::vec2 row = ui::cursor();
    const float right = row.x + ui::space_left().x;
    const float middle = row.x + ui::space_left().x * 0.5f;
    const std::size_t front_before = group.front;
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
    // A tab pressed makes its group the one in front.
    if (group.front != front_before) active_ = index;
    dialog_at_ = {middle, ui::cursor().y};

    // At the tab row's right end, the split button.
    const glm::vec2 below = ui::cursor();
    const float button = ui::frame_height();
    ui::set_cursor({right - button - TAB_END_PAD, row.y + (below.y - row.y - button) * 0.5f});
    if (ui::icon_button("##split", icon::split, "Split (Ctrl \\)")) split(index);
    ui::set_cursor(below);

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
        tab.view->minimap = preferences_.minimap;
        tab.view->text_size = static_cast<float>(preferences_.text_size);
        Document* document = workspace_.find(tab.path);
        const glm::vec2 at = ui::cursor();
        const glm::vec2 room = ui::space_left();
        if (document != nullptr && room.x > 0.0f && room.y > 0.0f) {
            const CodeViewResult result = code_view("Code", *document, *tab.view, keymap_, {.min = at, .max = at + room});
            if (result.focused) active_ = index;
            if (result.go) going_ = result.go;
            if (result.rename && tab.view->service != nullptr) {
                const Span word = word_at(document->buffer(), *result.rename);
                if (word.empty()) {
                    note_ = "Nothing to rename there";
                } else {
                    renaming_ = Renaming{.path = tab.path, .at = word.from, .name = document->buffer().text(word),
                                         .typed = {}, .prompting = false, .asked = {}};
                }
            }
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
        // The problems in the files open, and the first of the worst in the
        // file in front, with the file and line it is at.
        std::size_t errors = 0;
        std::size_t warnings = 0;
        const Diagnostic* first = nullptr;
        const Tab* tab = front_tab();
        for (const std::filesystem::path& path : services_.files()) {
            for (const Diagnostic& problem : services_.diagnostics(path)) {
                errors += problem.severity == Severity::Error ? 1 : 0;
                warnings += problem.severity == Severity::Warning ? 1 : 0;
                if (tab == nullptr || path != tab->path || problem.severity > Severity::Warning) continue;
                if (first == nullptr || problem.severity < first->severity) first = &problem;
            }
        }
        const bool served = tab != nullptr && tab->view->service != nullptr;
        if (served || errors + warnings > 0) {
            ui::status_item(ui::icon::error, std::to_string(errors));
            ui::status_item(ui::icon::warning, std::to_string(warnings));
        }
        if (first != nullptr) {
            const std::string_view message = first->message;
            ui::status_note(message.substr(0, message.find('\n')),
                            std::format("{}:{}", tab->path.filename().string(), first->span.from.line + 1));
        }
        if (!note_.empty()) ui::status_item(ui::icon::warning, note_);
        bar.right();
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
        // Each language service the file in front asks, as a dot in how it
        // is.
        for (const ServiceShown& service : services_.shown(tab->path)) {
            ui::status_rule();
            switch (service.status.state) {
                case ServiceState::Ready:
                    ui::status_state(ui::StatusTone::Success, service.name, service.name + " is running");
                    break;
                case ServiceState::Starting:
                    ui::status_state(ui::StatusTone::Warning, service.name, service.status.note);
                    break;
                case ServiceState::Stopped:
                    ui::status_state(ui::StatusTone::Error, service.name, service.status.note);
                    break;
            }
        }
    }
}

void Program::draw_prompts() {
    ask_recover();
    ask_close();
    ask_quit();
    ask_rename();
}

void Program::ask_rename() {
    if (!renaming_) return;
    Renaming& renaming = *renaming_;
    if (!renaming.asked) {
        if (!renaming.prompting) {
            ui::open_dialog("##rename", {dialog_at_, 0.5f});
            renaming.prompting = true;
        }
        const std::string title = "Rename " + renaming.name;
        const ui::DialogAnswer answer = ui::prompt_dialog(
            "##rename", {.title = title.c_str(), .hint = "Its new name", .confirm = "Rename"}, renaming.typed);
        if (answer == ui::DialogAnswer::Cancel) {
            renaming_.reset();
        } else if (answer == ui::DialogAnswer::Confirm) {
            if (!services_.serves(renaming.path)) {
                renaming_.reset();
                return;
            }
            renaming.asked = services_.rename(renaming.path, renaming.at, renaming.typed);
        }
        return;
    }
    if (!renaming.asked->answered) return;
    const ftk::Result<WorkspaceEdit> edits = renaming.asked->answer;
    renaming_.reset();
    if (!edits) {
        note_ = edits.error();
        return;
    }
    make_edits(*edits);
}

void Program::make_edits(const WorkspaceEdit& edits) {
    for (const auto& [path, file] : edits) {
        if (Document* document = workspace_.find(path)) {
            if (file.version && *file.version != document->version()) {
                note_ = std::format("{} changed while it was being renamed in", path.filename().string());
                continue;
            }
            // Its views catch up through the edit, as with any made elsewhere.
            Selections selections;
            const MonospaceLayout layout;
            CommandContext context{.document = *document, .selections = selections, .layout = layout};
            (void)apply_edit(context, file.edit);
            continue;
        }
        const ftk::Result<std::string> bytes = read_text_file(path);
        if (!bytes) {
            note_ = bytes.error();
            continue;
        }
        Document document(*bytes, language_for(path.filename().string()));
        Selections selections;
        const MonospaceLayout layout;
        CommandContext context{.document = document, .selections = selections, .layout = layout};
        (void)apply_edit(context, file.edit);
        if (const ftk::Result<> written = write_text_file(path, document.buffer().text()); !written) note_ = written.error();
    }
}

void Program::ask_close() {
    if (!closing_) return;
    if (!asking_) {
        ui::open_dialog("##unsaved", {dialog_at_, 0.5f});
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

void Program::ask_quit() {
    if (quit_asked_) {
        quit_asked_ = false;
        quit_paths_.clear();
        quit_items_.clear();
        for (const std::filesystem::path& path : workspace_.files()) {
            if (!workspace_.unsaved(path)) continue;
            quit_paths_.push_back(path);
            quit_items_.push_back(listed(path, workspace_.root(), {}));
        }
        if (!quit_items_.empty()) ui::open_dialog("##quit", {dialog_at_, 0.5f});
    }
    if (quit_items_.empty()) return;
    const std::size_t count = quit_items_.size();
    const std::string title = count == 1 ? std::string("1 file has unsaved changes")
                                         : std::format("{} files have unsaved changes", count);
    const ui::UnsavedAnswer answer = ui::unsaved_list_dialog("##quit", title.c_str(), quit_items_);
    if (answer == ui::UnsavedAnswer::None) return;
    const std::vector<std::filesystem::path> paths = std::exchange(quit_paths_, {});
    const std::vector<ui::UnsavedItem> items = std::exchange(quit_items_, {});
    if (answer == ui::UnsavedAnswer::Cancel) return;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (answer == ui::UnsavedAnswer::Save && items[i].save) {
            save(paths[i]);
            // A file that couldn't be saved keeps the program open.
            if (workspace_.unsaved(paths[i])) return;
        } else {
            forget_kept(paths[i]);
        }
    }
    quitting_ = true;
}

void Program::ask_recover() {
    if (!recovery_) return;
    if (!recover_looked_) {
        recover_looked_ = true;
        recovering_ = recovery_->kept();
        for (const Recovery::Kept& kept : recovering_) {
            const std::optional<std::size_t> changed = lines_changed(kept.path, kept.bytes);
            const std::string more = !changed        ? std::string("no longer on disk")
                                     : *changed == 1 ? std::string("1 line changed")
                                                     : std::format("{} lines changed", *changed);
            recover_items_.push_back(listed(kept.path, workspace_.root(), more));
        }
        if (!recovering_.empty()) ui::open_dialog("##recover", {dialog_at_, 0.5f});
    }
    if (recovering_.empty()) return;
    const std::string text = std::format("fjedit closed with unsaved changes in {}. They were kept on this machine.",
                                         recovering_.size() == 1 ? std::string("one file")
                                                                 : std::format("{} files", recovering_.size()));
    const ui::RecoverAnswer answer =
        ui::recover_dialog("##recover", "Recover unsaved changes", text.c_str(), recover_items_);
    if (answer == ui::RecoverAnswer::None) return;
    const std::vector<Recovery::Kept> kept = std::exchange(recovering_, {});
    const std::vector<ui::UnsavedItem> items = std::exchange(recover_items_, {});
    if (answer == ui::RecoverAnswer::Later) return;
    for (std::size_t i = 0; i < kept.size(); ++i) {
        if (answer == ui::RecoverAnswer::Recover && items[i].save) {
            open(kept[i].path);
            if (workspace_.find(kept[i].path) == nullptr) continue;
            if (const ftk::Result<> put = workspace_.set_text(kept[i].path, kept[i].bytes); !put) note_ = put.error();
        } else {
            recovery_->forget(kept[i].path);
        }
    }
}

} // namespace fjedit
