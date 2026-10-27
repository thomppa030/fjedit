#include "fjedit/editor/code_area.hpp"

#include "fjedit/core/commands.hpp"
#include "fjedit/core/text_file.hpp"
#include "fjedit/core/vim/motions.hpp"
#include "fjedit/core/vim/notation.hpp"
#include "fjedit/editor/file_tree.hpp"
#include "fjedit/editor/icons.hpp"
#include "fjedit/widget/file_kind.hpp"

#include "ftk/platform/platform.hpp"

#include "ftk/ui/core/context.hpp"
#include "ftk/ui/draw_list.hpp"
#include "ftk/ui/kit/button.hpp"
#include "ftk/ui/kit/feedback.hpp"
#include "ftk/ui/kit/icons.hpp"
#include "ftk/ui/kit/layout.hpp"
#include "ftk/ui/kit/region.hpp"
#include "ftk/ui/kit/scope.hpp"
#include "ftk/ui/kit/settings_file.hpp"
#include "ftk/ui/kit/status_bar.hpp"
#include "ftk/ui/kit/tabs.hpp"
#include "ftk/ui/kit/window.hpp"
#include "ftk/ui/theme.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <variant>

namespace fjedit {

namespace {

namespace ui = ftk::ui;
namespace theme = ftk::theme;
using ftk::ui::Key;

// The buttons at the end of a tab row, in from its right edge.
constexpr float TAB_END_PAD = 4.0f;

// What a keys.vim made by Preferences' Open begins with.
constexpr std::string_view KEYS_FILE_START = R"(" Your maps for Vim's keys, in Vim's map syntax. Saved here in fjedit,
" they apply at once; a line that can't be read shows on the status bar.
"
"   nnoremap <leader>w <Cmd>w<CR>
"   xnoremap < <gv
"   bind normal <leader>fg files.find
)";

Chord ctrl(Key key) {
    return {.key = key, .modifiers = {.ctrl = true}};
}

// The standard keys, with the area's commands and its program's in their
// modes.
Keymap area_keys(std::span<const CodeCommand> own, std::span<const CodeCommand> program) {
    Keymap keys = Keymap::standard();
    for (const std::span<const CodeCommand> commands : {own, program}) {
        for (const CodeCommand& command : commands) {
            if (!command.key) continue;
            keys.bind(CodeArea::PROGRAM, {*command.key}, std::string(command.name));
            // With Vim on, Ctrl W deletes the word before the caret in insert
            // mode and begins the window commands in normal mode; a tab closes
            // by its button or the File menu.
            if (command.name != "file.close") keys.bind(CodeArea::VIM_PROGRAM, {*command.key}, std::string(command.name));
        }
    }
    return keys;
}

// How a status bar names Vim's mode, and the tone of its pill (sheet 47,
// M1).
struct ModeShown {
    const char* name;
    ui::StatusTone tone;
};

ModeShown mode_shown(vim::Mode mode) {
    switch (mode) {
        case vim::Mode::Insert: return {"Insert", ui::StatusTone::On};
        case vim::Mode::Replace: return {"Replace", ui::StatusTone::Warning};
        case vim::Mode::Visual: return {"Visual", ui::StatusTone::Selected};
        case vim::Mode::VisualLine: return {"Visual line", ui::StatusTone::Selected};
        case vim::Mode::VisualBlock: return {"Visual block", ui::StatusTone::Selected};
        case vim::Mode::Command: return {"Command", ui::StatusTone::Plain};
        case vim::Mode::Normal: break;
    }
    return {"Normal", ui::StatusTone::Plain};
}

// The label a file's tab is drawn and known by: its name, and its path
// after ## so two files of one name each have their own.
std::string tab_label(const std::filesystem::path& path) {
    return path.filename().string() + "##" + path.generic_string();
}

// Whether Vim reads `key` in its own modes: what types, with Shift or
// without, the keys that move and edit, and Ctrl with a letter. The F keys,
// Ctrl with Shift or with a key that is no letter, and Alt, it gives no
// meaning: they are a host's.
bool vim_reads(const Chord& key) {
    if (!key.text.empty()) return true;
    const ui::Modifiers held = key.modifiers;
    if (held.alt || held.super) return false;
    if (key.key >= Key::F1 && key.key <= Key::F12) return false;
    if (held.ctrl) return !held.shift && key.key >= Key::A && key.key <= Key::Z;
    return true;
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

} // namespace

ui::UnsavedItem file_item(const std::filesystem::path& path, const std::filesystem::path& root, std::string_view more) {
    const ui::AssetKind kind = file_kind(path);
    std::string folder = path.parent_path().lexically_relative(root).generic_string();
    if (folder == ".") folder.clear();
    std::string detail = folder;
    if (!more.empty()) detail += detail.empty() ? std::string(more) : "  " + std::string(more);
    return {.icon = kind.icon,
            .icon_colour = ui::kind_colour(kind),
            .name = path.filename().string(),
            .detail = std::move(detail)};
}

CodeArea::CodeArea(CodeFiles& files, Preferences& preferences, CodeAreaOptions options)
    : files_(files), preferences_(preferences), program_commands_(std::move(options.commands)),
      run_program_(std::move(options.run)), leave_(std::move(options.leave)),
      keymap_(area_keys(commands(), program_commands_)), groups_(1), program_name_(std::move(options.program_name)),
      keys_path_(std::move(options.keys)), mark_(std::move(options.mark)), device_(options.device) {
    read_keys();
}

std::filesystem::path CodeArea::keys_file() {
    return ftk::platform::config_dir() / "fjedit" / "keys.vim";
}

std::span<const CodeCommand> CodeArea::commands() {
    static const CodeCommand COMMANDS[] = {
        {.name = "file.save", .title = "Save", .key = ctrl(Key::S)},
        {.name = "file.save_all", .title = "Save all"},
        {.name = "file.close", .title = "Close", .key = ctrl(Key::W)},
        {.name = "view.split", .title = "Split", .key = ctrl(Key::Backslash)},
        {.name = "files.find",
         .title = "Find in files",
         .key = Chord{.key = Key::F, .modifiers = {.ctrl = true, .shift = true}}},
        {.name = "tab.next", .title = "Next tab", .key = ctrl(Key::PageDown)},
        {.name = "tab.previous", .title = "Previous tab", .key = ctrl(Key::PageUp)},
    };
    return COMMANDS;
}

bool CodeArea::program_command(std::string_view name) const {
    return std::ranges::contains(program_commands_, name, &CodeCommand::name);
}

bool CodeArea::has_command(std::string_view name) const {
    return std::ranges::contains(commands(), name, &CodeCommand::name) || program_command(name) ||
           find_command(name) != nullptr || is_view_command(name);
}

std::string CodeArea::command_title(std::string_view name) const {
    for (const std::span<const CodeCommand> commands : {CodeArea::commands(), std::span<const CodeCommand>(program_commands_)}) {
        const auto command = std::ranges::find(commands, name, &CodeCommand::name);
        if (command != commands.end()) return std::string(command->title);
    }
    return {};
}

void CodeArea::read_keys() {
    keymap_ = area_keys(commands(), program_commands_);
    maps_ = std::make_shared<const std::vector<vim::Mapping>>();
    keys_errors_.clear();
    std::error_code error;
    if (keys_path_.empty() || !std::filesystem::exists(keys_path_, error)) return;
    const ftk::Result<std::string> text = read_text_file(keys_path_);
    if (!text) {
        keys_errors_.push_back({.line = 0, .message = text.error()});
        return;
    }
    vim::KeysFile file = vim::read_keys_file(*text);
    keys_errors_ = std::move(file.errors);
    // Each bind into the keymap mode it names, over the area's own.
    for (const vim::Bind& bind : file.binds) {
        if (!has_command(bind.command)) {
            keys_errors_.push_back({.line = bind.line, .message = std::format("No command called {}", bind.command)});
            continue;
        }
        const std::string_view mode = bind.mode == "normal"       ? Keymap::VIM_NORMAL
                                      : bind.mode == "visual"     ? Keymap::VIM_VISUAL
                                      : bind.mode == "insert"     ? Keymap::VIM_INSERT
                                      : bind.mode == "command"    ? Keymap::VIM_COMMAND
                                      : bind.mode == "completing" ? Keymap::VIM_COMPLETING
                                      : bind.mode == "finding"    ? Keymap::FINDING
                                                                  : VIM_PROGRAM;
        keymap_.bind(mode, bind.keys, bind.command);
    }
    for (vim::KeysError& said : vim::apply_settings(file.settings, *vim_settings_, *registers_)) {
        keys_errors_.push_back(std::move(said));
    }
    std::ranges::stable_sort(keys_errors_, {}, &vim::KeysError::line);
    maps_ = std::make_shared<const std::vector<vim::Mapping>>(std::move(file.maps));
}

void CodeArea::open_keys() {
    if (keys_path_.empty()) return;
    std::error_code error;
    if (!std::filesystem::exists(keys_path_, error)) {
        std::filesystem::create_directories(keys_path_.parent_path(), error);
        const ftk::Result<> made = write_text_file(keys_path_, KEYS_FILE_START);
        if (!made) {
            files_.set_note(made.error());
            return;
        }
    }
    open(keys_path_);
}

void CodeArea::show_as_preferred(EditorView& view) {
    vim::Settings& settings = *vim_settings_;
    view.minimap = preferences_.minimap;
    view.close_pairs = preferences_.close_pairs;
    view.bracket_colours = preferences_.bracket_colours;
    view.text_size = static_cast<float>(preferences_.text_size);
    view.line_numbers = settings.relative_numbers
                            ? (*settings.relative_numbers ? LineNumbers::Relative : LineNumbers::Absolute)
                            : preferences_.line_numbers;
    view.around_caret = settings.around_caret.value_or(static_cast<std::size_t>(preferences_.around_caret));
    settings.relative_numbers_shown = view.line_numbers == LineNumbers::Relative;
    settings.around_caret_shown = view.around_caret;
    // Vim turned on starts each view in its normal mode, listing paths
    // after `:e` as the area does.
    if (preferences_.vim && !view.vim) {
        view.vim.emplace(registers_, vim_settings_);
        view.vim->list_paths_with(path_lister_);
    }
    view.maps.use(maps_);
    if (!preferences_.vim) view.vim.reset();
    // The area's commands and its program's, by their titles, for the keys
    // that can come next.
    if (!view.command_title) {
        view.command_title = [this](std::string_view name) { return command_title(name); };
    }
}

std::unique_ptr<EditorView> CodeArea::make_view(const std::filesystem::path& path, Document& document) {
    auto view = std::make_unique<EditorView>(document);
    show_as_preferred(*view);
    LanguageServices& services = files_.services();
    if (services.serves(path)) {
        view->service = &services;
        view->path = path;
        services.open(path, document);
    }
    return view;
}

void CodeArea::let_go(const std::filesystem::path& path) {
    if (const Document* document = files_.workspace().find(path)) {
        for (Group& group : groups_) group.jumps.closed(files_.workspace().name(path), *document);
    }
    files_.let_go(path);
}

void CodeArea::files_updated(const CodeFiles::Update& update) {
    if (update.looked) path_listings_.clear();
    if (!update.restarted) return;
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

void CodeArea::go_to(const std::filesystem::path& path, Span span) {
    keep_jump();
    open(path);
    Tab* tab = front_tab();
    const Document* document = tab != nullptr ? files_.workspace().find(tab->path) : nullptr;
    if (document == nullptr) return;
    const Buffer& buffer = document->buffer();
    tab->view->selections = Selections({.anchor = buffer.clamp(span.from), .caret = buffer.clamp(span.to)});
    tab->view->reveal = true;
}

std::optional<std::filesystem::path> CodeArea::front() const {
    const Tab* tab = front_tab();
    if (tab == nullptr) return std::nullopt;
    return tab->path;
}

std::vector<GroupFiles> CodeArea::shown() const {
    std::vector<GroupFiles> shown;
    for (const Group& group : groups_) {
        GroupFiles files{.files = {}, .front = group.front};
        for (const Tab& tab : group.tabs) files.files.push_back(tab.path);
        shown.push_back(std::move(files));
    }
    return shown;
}

void CodeArea::show(std::span<const GroupFiles> groups) {
    if (std::ranges::any_of(groups_, [](const Group& group) { return !group.tabs.empty(); })) return;
    std::vector<Group> showing;
    for (const GroupFiles& kept : groups) {
        Group group;
        std::optional<std::size_t> front;
        for (std::size_t i = 0; i < kept.files.size(); ++i) {
            // A file gone since is left out, saying nothing.
            std::error_code error;
            if (!std::filesystem::is_regular_file(kept.files[i], error)) continue;
            const ftk::Result<Document*> document = files_.open(kept.files[i]);
            if (!document) continue;
            const std::filesystem::path named = files_.workspace().name(kept.files[i]);
            if (std::ranges::contains(group.tabs, named, &Tab::path)) continue;
            if (i == kept.front) front = group.tabs.size();
            group.tabs.push_back({.path = named, .view = make_view(named, **document)});
        }
        if (group.tabs.empty()) continue;
        group.front = front.value_or(0);
        group.select = group.front;
        showing.push_back(std::move(group));
    }
    if (showing.empty()) return;
    groups_ = std::move(showing);
    active_ = 0;
}

bool CodeArea::binds(const Chord& key) const {
    const std::array<Chord, 1> keys{key};
    const auto bound = [&](std::string_view mode) {
        const KeyMatch match = keymap_.find(mode, keys);
        return match.command.has_value() || match.waiting;
    };
    if (bound(preferences_.vim ? VIM_PROGRAM : PROGRAM)) return true;
    const Tab* tab = front_tab();
    if (tab == nullptr) return false;
    const EditorView& view = *tab->view;
    if (view.vim) {
        return bound(vim::keymap_mode(view.vim->mode())) ||
               (view.completing.open && bound(Keymap::VIM_COMPLETING)) || vim_reads(key);
    }
    return bound(view.mode) || (view.completing.open && bound(Keymap::COMPLETING)) ||
           (view.find.open && bound(Keymap::FINDING));
}

CodeStatus CodeArea::status() {
    CodeStatus status;
    const Tab* tab = front_tab();
    if (tab != nullptr && tab->view->vim) {
        const vim::Engine& engine = *tab->view->vim;
        const ModeShown mode = mode_shown(engine.mode());
        std::string keys = engine.pending();
        for (const Chord& key : tab->view->pending) keys += vim::notation(key);
        for (const Chord& key : tab->view->maps.waiting()) keys += vim::notation(key);
        status.vim = CodeStatus::Vim{.mode = mode.name,
                                     .tone = mode.tone,
                                     .recording = engine.recording(),
                                     .keys = std::move(keys),
                                     .said = engine.message(),
                                     .keys_error = keys_errors_.empty() ? std::nullopt : std::optional(keys_errors_.front())};
    }

    // The problems in the files open, and the first of the worst in the
    // file in front, with the file and line it is at.
    std::size_t errors = 0;
    std::size_t warnings = 0;
    const Diagnostic* first = nullptr;
    LanguageServices& services = files_.services();
    for (const std::filesystem::path& path : services.files()) {
        for (const Diagnostic& problem : services.diagnostics(path)) {
            errors += problem.severity == Severity::Error ? 1 : 0;
            warnings += problem.severity == Severity::Warning ? 1 : 0;
            if (tab == nullptr || path != tab->path || problem.severity > Severity::Warning) continue;
            if (first == nullptr || problem.severity < first->severity) first = &problem;
        }
    }
    const bool served = tab != nullptr && tab->view->service != nullptr;
    if (served || errors + warnings > 0) status.problems = CodeStatus::Problems{.errors = errors, .warnings = warnings};
    if (first != nullptr) {
        const std::string_view message = first->message;
        status.first_problem = CodeStatus::Problem{
            .message = std::string(message.substr(0, message.find('\n'))),
            .where = std::format("{}:{}", tab->path.filename().string(), first->span.from.line + 1)};
    }

    status.note = files_.note();
    status.told = told_;
    if (tab != nullptr) status.hint = picking_hint(*tab->view);

    const Document* document = tab != nullptr ? files_.workspace().find(tab->path) : nullptr;
    if (document == nullptr) return status;
    const Buffer& buffer = document->buffer();
    const Position caret = tab->view->selections.primary().caret;
    const Indent indent = document->indent();
    const std::size_t column = column_of(buffer.line(caret.line), caret.byte, std::max<std::size_t>(indent.width, 1));
    status.place = CodeStatus::Place{.line = caret.line + 1,
                                     .column = column + 1,
                                     .indent = indent,
                                     .byte_order_mark = buffer.byte_order_mark(),
                                     .language = std::string(document->language().name),
                                     .services = services.shown(tab->path)};
    return status;
}

CodeArea::Tab* CodeArea::front_tab() {
    Group& group = groups_[active_];
    return group.front < group.tabs.size() ? &group.tabs[group.front] : nullptr;
}

const CodeArea::Tab* CodeArea::front_tab() const {
    const Group& group = groups_[active_];
    return group.front < group.tabs.size() ? &group.tabs[group.front] : nullptr;
}

void CodeArea::open(const std::filesystem::path& path) {
    const ftk::Result<Document*> document = files_.open(path);
    if (!document) return;
    Group& group = groups_[active_];
    const std::filesystem::path named = files_.workspace().name(path);
    for (Group& each : groups_) each.jumps.opened(named, (*document)->version());
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

bool CodeArea::run(std::string_view command) {
    Group& group = groups_[active_];
    Tab* tab = front_tab();
    if (command == "file.save") {
        if (tab != nullptr) save(tab->path);
    } else if (command == "file.save_all") {
        for (const std::filesystem::path& path : files_.unsaved_files()) save(path);
    } else if (command == "file.close") {
        close_front();
    } else if (command == "view.split") {
        split(active_);
    } else if (command == "files.find") {
        open_find_in_files();
    } else if (command == "tab.next" || command == "tab.previous") {
        if (group.tabs.empty()) return true;
        const std::size_t count = group.tabs.size();
        group.select = command == "tab.next" ? (group.front + 1) % count : (group.front + count - 1) % count;
    } else if (find_command(command) != nullptr || is_view_command(command)) {
        // The view in front's: it runs at the view's next draw.
        if (tab != nullptr) tab->view->queued.emplace_back(command);
    } else if (program_command(command)) {
        return run_program_ && run_program_(command);
    } else {
        return false;
    }
    return true;
}

void CodeArea::save(const std::filesystem::path& path) {
    if (!files_.save(path)) return;
    // keys.vim saved here applies at once.
    const Workspace& workspace = files_.workspace();
    if (!keys_path_.empty() && workspace.name(path) == workspace.name(keys_path_)) read_keys();
}

void CodeArea::close_front() {
    const Tab* tab = front_tab();
    if (tab == nullptr) return;
    // Another tab showing the file keeps its changes: closing this one
    // loses nothing.
    if (tabs_showing(tab->path) == 1 && files_.unsaved(tab->path)) {
        closing_ = tab->path;
        return;
    }
    close_tab(groups_[active_], groups_[active_].front);
}

std::size_t CodeArea::tabs_showing(const std::filesystem::path& path) const {
    std::size_t showing = 0;
    for (const Group& group : groups_) showing += static_cast<std::size_t>(std::ranges::count(group.tabs, path, &Tab::path));
    return showing;
}

std::vector<vim::PathEntry> CodeArea::list_paths(std::string_view folder, bool hidden) {
    const std::filesystem::path at = files_.root() / std::filesystem::path(folder);
    const auto listed = path_listings_.find({at, hidden});
    if (listed != path_listings_.end()) return listed->second;
    std::vector<vim::PathEntry> entries;
    for (const FolderEntry& entry : read_folder(at, hidden)) entries.push_back({.name = entry.name, .folder = entry.folder});
    return path_listings_.emplace(std::pair{at, hidden}, std::move(entries)).first->second;
}

void CodeArea::run_ex(const vim::ExCommand& command) {
    Tab* tab = front_tab();
    if (tab == nullptr) return;
    const std::string& name = command.name;
    const std::filesystem::path path = tab->path;
    const auto refuse = [&](std::string text) {
        if (tab->view->vim) tab->view->vim->say({.text = std::move(text), .error = true});
    };
    constexpr std::string_view NO_WRITE = "E37: No write since last change (add ! to override)";
    // Closing the tab loses changes when its file has them and no other tab
    // shows it.
    const bool loses = files_.unsaved(path) && tabs_showing(path) == 1;
    const auto close = [&] { close_tab(groups_[active_], groups_[active_].front); };
    if (name == "write") {
        save(path);
    } else if (name == "wall") {
        (void)run("file.save_all");
    } else if (name == "quit" || name == "close") {
        if (loses && !command.bang) return refuse(std::string(NO_WRITE));
        close();
    } else if (name == "wq" || name == "xit") {
        // :x writes only a file with changes. One that couldn't be written
        // keeps its tab, the note saying why.
        if (name == "wq" || files_.unsaved(path)) save(path);
        if (files_.unsaved(path)) return;
        close();
    } else if (name == "qall") {
        const std::optional<std::filesystem::path> unsaved = files_.first_unsaved();
        if (unsaved && !command.bang) {
            return refuse(std::format("E162: No write since last change for buffer \"{}\"", unsaved->filename().string()));
        }
        for (const std::filesystem::path& file : files_.unsaved_files()) files_.forget_kept(file);
        if (leave_) leave_();
    } else if (name == "edit" && !command.argument.empty()) {
        // A path from the folder open, as Vim's from the folder it works in;
        // a jump, as in Vim.
        keep_jump();
        open(files_.root() / std::filesystem::path(command.argument));
    } else if (name == "edit") {
        // The file read again; with !, its changes dropped. Undone, they
        // come back.
        if (files_.unsaved(path) && !command.bang) return refuse(std::string(NO_WRITE));
        (void)files_.reload(path);
    } else if (name == "bnext" || name == "bprevious") {
        keep_jump();
        (void)run(name == "bnext" ? "tab.next" : "tab.previous");
    } else if (name == "split" || name == "vsplit") {
        // The groups stand side by side, so both split beside.
        split(active_);
    } else if (name == "wincmd") {
        // w goes round the groups; h and l stop at the first and the last.
        const std::size_t count = groups_.size();
        if (command.argument == "w") focus_group((active_ + 1) % count);
        if (command.argument == "h" && active_ > 0) focus_group(active_ - 1);
        if (command.argument == "l" && active_ + 1 < count) focus_group(active_ + 1);
    }
}

void CodeArea::answer_vim(const vim::Asked& asked) {
    if (const auto* command = std::get_if<vim::ExCommand>(&asked)) return run_ex(*command);
    if (const auto* set = std::get_if<vim::MarkSet>(&asked)) return keep_file_mark(set->name);
    if (const auto* wanted = std::get_if<vim::MarkWanted>(&asked)) return go_to_file_mark(*wanted);
    if (const auto* from = std::get_if<vim::JumpFrom>(&asked)) return keep_jump(*from);
    step_jumps(std::get<vim::JumpStep>(asked).count);
}

void CodeArea::keep_jump() {
    const Tab* tab = front_tab();
    const Document* document = tab != nullptr ? files_.workspace().find(tab->path) : nullptr;
    if (document == nullptr) return;
    keep_jump({.at = tab->view->selections.primary().caret, .version = document->version()});
}

void CodeArea::keep_jump(const vim::JumpFrom& from) {
    const Tab* tab = front_tab();
    if (tab == nullptr) return;
    groups_[active_].jumps.jumped({.path = tab->path, .at = from.at, .version = from.version});
}

void CodeArea::step_jumps(long count) {
    const Tab* tab = front_tab();
    const Document* document = tab != nullptr ? files_.workspace().find(tab->path) : nullptr;
    if (document == nullptr) return;
    JumpList& jumps = groups_[active_].jumps;
    jumps.catch_up(files_.workspace());
    const JumpList::Place here{.path = tab->path, .at = tab->view->selections.primary().caret, .version = document->version()};
    const std::optional<JumpList::Place> to = jumps.step(count, here);
    if (!to) return;
    if (to->path != tab->path) open(to->path);
    Tab* now = front_tab();
    const Document* there = now != nullptr ? files_.workspace().find(now->path) : nullptr;
    // Not opened: what went wrong is noted.
    if (there == nullptr || now->path != to->path) return;
    now->view->selections = Selections(Selection::at(there->buffer().clamp(to->at)));
    now->view->reveal = true;
}

void CodeArea::keep_file_mark(char name) {
    const Tab* tab = front_tab();
    if (tab == nullptr) return;
    files_.keep_file_mark(name, tab->path);
}

void CodeArea::go_to_file_mark(const vim::MarkWanted& wanted) {
    const std::optional<std::filesystem::path> holder = files_.file_with_mark(wanted.name);
    if (!holder) {
        if (Tab* tab = front_tab(); tab != nullptr && tab->view->vim) {
            tab->view->vim->say({.text = "E20: Mark not set", .error = true});
        }
        return;
    }
    keep_jump();
    open(*holder);
    Tab* tab = front_tab();
    const Document* document = tab != nullptr ? files_.workspace().find(tab->path) : nullptr;
    const std::optional<Position> at = document != nullptr ? document->mark(wanted.name) : std::nullopt;
    // Not opened: what went wrong is noted.
    if (!at) return;
    const Position to = wanted.exact ? *at : vim::first_non_blank(document->buffer(), at->line);
    tab->view->selections = Selections(Selection::at(to));
    tab->view->reveal = true;
}

void CodeArea::focus_group(std::size_t index) {
    active_ = index;
    if (Tab* tab = front_tab()) tab->view->focus = true;
}

void CodeArea::close_tab(Group& group, std::size_t index) {
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
        files_.forget_kept(path);
    }
    if (group.tabs.empty() && groups_.size() > 1) {
        const auto gone = static_cast<std::size_t>(&group - groups_.data());
        groups_.erase(groups_.begin() + static_cast<std::ptrdiff_t>(gone));
        if (active_ >= gone && active_ > 0) --active_;
        // The view that had the keyboard went with the group: it goes to the
        // group in front now.
        if (Tab* front = front_tab()) front->view->focus = true;
    }
}

void CodeArea::split(std::size_t index) {
    if (index >= groups_.size()) return;
    const Group& from = groups_[index];
    if (from.front >= from.tabs.size()) return;
    const Tab& shown = from.tabs[from.front];
    Document* document = files_.workspace().find(shown.path);
    if (document == nullptr) return;
    // Its jumps go with it, as Vim's :split copies a window's.
    Group beside{.tabs = {}, .front = 0, .select = {}, .jumps = from.jumps};
    auto view = make_view(shown.path, *document);
    view->selections = shown.view->selections;
    view->scroll = shown.view->scroll;
    view->focus = true;
    beside.tabs.push_back({.path = shown.path, .view = std::move(view)});
    beside.select = 0;
    groups_.insert(groups_.begin() + static_cast<std::ptrdiff_t>(index + 1), std::move(beside));
    active_ = index + 1;
}

void CodeArea::open_find_in_files() {
    std::string seed;
    if (const Tab* tab = front_tab()) {
        const Selection& primary = tab->view->selections.primary();
        const Span span = primary.span();
        const Document* document = files_.workspace().find(tab->path);
        if (document != nullptr && !primary.empty() && span.from.line == span.to.line) {
            seed = document->buffer().text(span);
        }
    }
    find_in_files_.open(seed);
}

bool CodeArea::take_find_in_files(const FindInFilesAsk& ask) {
    for (const std::filesystem::path& path : ask.shown) {
        const bool in_a_tab = std::ranges::any_of(groups_, [&](const Group& group) {
            return std::ranges::contains(group.tabs, path, &Tab::path);
        });
        if (in_a_tab) continue;
        // Changed in the preview, it gets a tab to be saved from; otherwise
        // it goes.
        if (files_.unsaved(path)) {
            Group& group = groups_[active_];
            group.tabs.push_back({.path = path, .view = make_view(path, *files_.workspace().find(path))});
        } else {
            let_go(path);
        }
    }
    if (!ask.go) return false;
    go_to(ask.go->path, ask.go->span);
    return true;
}

void CodeArea::take_keys() {
    ui::core::Context& context = ui::core::current();
    // What was just done is said until the next key.
    if (!context.keystrokes().empty()) told_.clear();
    const ui::Modifiers held = context.modifiers();
    for (std::size_t k = 0; k < ui::KEY_COUNT; ++k) {
        const auto key = static_cast<Key>(k);
        if (!context.key_pressed(key)) continue;
        const KeyMatch match =
            keymap_.find(preferences_.vim ? VIM_PROGRAM : PROGRAM, std::vector<Chord>{{.key = key, .modifiers = held}});
        if (match.command) (void)run(*match.command);
    }
}

void CodeArea::draw() {
    // Questions hang from the top of the code, under the tabs once there
    // are some.
    prompts_at_ = ui::cursor() + glm::vec2{ui::space_left().x * 0.5f, 0.0f};
    keyboard_ = false;
    const float height = ui::space_left().y;
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
    // What the views asked, now that every one of them is drawn.
    if (const std::optional<Location> going = std::exchange(going_, std::nullopt)) go_to(going->path, going->span);
    for (const vim::Asked& asked : std::exchange(vim_asked_, {})) answer_vim(asked);
}

bool CodeArea::draw_find_in_files(ftk::ui::core::Rect over, bool over_windows) {
    const std::optional<std::filesystem::path> shown = front();
    const std::filesystem::path folder = shown ? shown->parent_path() : files_.root();
    return take_find_in_files(find_in_files_.draw(over, folder, keymap_, static_cast<float>(preferences_.text_size),
                                                  preferences_.vim, over_windows));
}

void CodeArea::draw_group(Group& group, std::size_t index) {
    const ui::IdScope scope(static_cast<int>(index));
    if (group.tabs.empty()) {
        const char* title = "No file open";
        const char* what_to_do = "Open one from the files on the left";
        if (mark_) {
            // The mark where the file icon would be, drawn at the pixels it
            // covers.
            const float height = ui::EMPTY_PICTURE_HEIGHT;
            const ui::Texture picture =
                device_ != nullptr ? mark_->texture(*device_, height, ui::core::current().root_scale()) : ui::Texture{};
            (void)ui::empty_state(picture, mark_->size(height), title, what_to_do);
        } else {
            (void)ui::empty_state(ui::icon::file, title, what_to_do);
        }
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
            const ui::AssetKind kind = file_kind(tab.path);
            const ui::DocumentTabResult result = ui::document_tab({.label = label.c_str(),
                                                                   .icon = kind.icon,
                                                                   .hue = kind.hue,
                                                                   .unsaved = files_.unsaved(tab.path),
                                                                   .select = group.select == i});
            if (result.front) group.front = i;
            if (result.closed) closed = i;
        }
    }
    group.select.reset();
    // A tab pressed makes its group the one in front.
    if (group.front != front_before) active_ = index;
    prompts_at_ = {middle, ui::cursor().y};

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
        if (files_.changed_on_disk(tab.path)) {
            const std::string text =
                std::format("{} changed on disk. It has unsaved changes here.", tab.path.filename().string());
            const char* answers[] = {"Keep mine", "Load from disk"};
            if (const std::optional<std::size_t> answer = ui::notice_strip(ui::Severity::Warning, text.c_str(), answers)) {
                // Answered either way, it isn't asked again.
                files_.keep_mine(tab.path);
                if (*answer == 1) (void)files_.reload(tab.path);
            }
        }
        show_as_preferred(*tab.view);
        const std::optional<std::filesystem::path> unsaved = files_.first_unsaved();
        tab.view->file_facts = {.file = tab.path.filename().string(),
                                .closing_loses = files_.unsaved(tab.path) && tabs_showing(tab.path) == 1,
                                .unsaved = unsaved ? unsaved->filename().string() : std::string()};
        Document* document = files_.workspace().find(tab.path);
        const glm::vec2 at = ui::cursor();
        const glm::vec2 room = ui::space_left();
        if (document != nullptr && room.x > 0.0f && room.y > 0.0f) {
            const CodeViewResult result = code_view("Code", *document, *tab.view, keymap_, {.min = at, .max = at + room});
            if (result.focused) {
                active_ = index;
                keyboard_ = true;
            }
            if (result.go) going_ = result.go;
            for (const vim::Asked& asked : result.asked) vim_asked_.push_back(asked);
            if (result.zoom != 0) {
                // Ctrl and the wheel size every view's text, as the
                // preferences do, and the size is kept with them.
                preferences_.text_size =
                    std::clamp(preferences_.text_size + result.zoom, TEXT_SIZE_MIN, TEXT_SIZE_MAX);
                ui::settings_changed();
            }
            // A rename typed in place: the service asked to make it in every
            // file.
            LanguageServices& services = files_.services();
            if (result.rename && services.serves(tab.path)) {
                renaming_ = Renaming{.rename = *result.rename,
                                     .asked = services.rename(tab.path, result.rename->at, result.rename->to)};
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

void CodeArea::draw_prompts() {
    ask_recover();
    ask_close();
    ask_rename();
}

void CodeArea::ask_rename() {
    if (!renaming_ || !renaming_->asked->answered) return;
    const ftk::Result<WorkspaceEdit> edits = renaming_->asked->answer;
    const Rename rename = renaming_->rename;
    renaming_.reset();
    if (!edits) {
        files_.set_note(edits.error());
        return;
    }
    files_.make_edits(*edits);
    told_ = std::format("Renamed {} to {} in {} file{}", rename.from, rename.to, edits->size(), edits->size() == 1 ? "" : "s");
}

void CodeArea::ask_close() {
    if (!closing_) return;
    if (!asking_) {
        ui::open_dialog("##unsaved", {prompts_at_, 0.5f});
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
        if (files_.unsaved(path)) return;
    }
    Group& group = groups_[active_];
    const auto found = std::ranges::find(group.tabs, path, &Tab::path);
    if (found != group.tabs.end()) close_tab(group, static_cast<std::size_t>(std::distance(group.tabs.begin(), found)));
}

void CodeArea::ask_recover() {
    if (!files_.keeps_work()) return;
    if (!recover_looked_) {
        recover_looked_ = true;
        recovering_ = files_.kept_from_before();
        for (const Recovery::Kept& kept : recovering_) {
            const std::optional<std::size_t> changed = lines_changed(kept.path, kept.bytes);
            const std::string more = !changed        ? std::string("no longer on disk")
                                     : *changed == 1 ? std::string("1 line changed")
                                                     : std::format("{} lines changed", *changed);
            recover_items_.push_back(file_item(kept.path, files_.root(), more));
        }
        if (!recovering_.empty()) ui::open_dialog("##recover", {prompts_at_, 0.5f});
    }
    if (recovering_.empty()) return;
    const std::string text = std::format("{} closed with unsaved changes in {}. They were kept on this machine.",
                                         program_name_,
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
            if (files_.workspace().find(kept[i].path) == nullptr) continue;
            (void)files_.put_back(kept[i].path, kept[i].bytes);
        } else {
            files_.discard_kept(kept[i].path);
        }
    }
}

std::string CodeArea::shortcut(std::string_view command) const {
    std::optional<std::vector<Chord>> chords;
    if (!preferences_.vim) {
        chords = keymap_.keys_for(PROGRAM, command);
        if (!chords) chords = keymap_.keys_for(Keymap::EDITING, command);
    } else {
        // A view's Vim starts in normal mode, so with none open, normal's;
        // its mode's keys first, one's own binds among them.
        const Tab* tab = front_tab();
        const vim::Mode mode = tab != nullptr && tab->view->vim ? tab->view->vim->mode() : vim::Mode::Normal;
        chords = keymap_.keys_for(vim::keymap_mode(mode), command);
        if (!chords) chords = keymap_.keys_for(VIM_PROGRAM, command);
        if (!chords) chords = vim::keys_for(command, mode);
    }
    return chords ? chords_text(*chords) : std::string();
}

} // namespace fjedit
