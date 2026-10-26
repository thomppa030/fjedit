#include "fjedit/program/program.hpp"

#include "fjedit/core/commands.hpp"
#include "fjedit/core/language.hpp"
#include "fjedit/core/layout.hpp"
#include "fjedit/core/text_file.hpp"
#include "fjedit/core/vim/motions.hpp"
#include "fjedit/core/vim/notation.hpp"
#include "fjedit/lsp/client.hpp"
#include "fjedit/program/icons.hpp"
#include "fjedit/widget/file_kind.hpp"

#include "ftk/base/log.hpp"
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
#include <tuple>
#include <utility>
#include <variant>

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

// What a keys.vim made by Preferences' Open begins with.
constexpr std::string_view KEYS_FILE_START = R"(" Your maps for Vim's keys, in Vim's map syntax. Saved here in fjedit,
" they apply at once; a line that can't be read shows on the status bar.
"
"   nnoremap <leader>w <Cmd>w<CR>
"   xnoremap < <gv
"   bind normal <leader>fg files.find
)";

// The standard keys, with the program's own in its modes.
Keymap program_keys() {
    Keymap keys = Keymap::standard();
    const auto ctrl = [](Key key) { return Chord{.key = key, .modifiers = {.ctrl = true}}; };
    const std::pair<Chord, std::string_view> program[] = {
        {ctrl(Key::S), "file.save"},
        {ctrl(Key::W), "file.close"},
        {ctrl(Key::Q), "file.quit"},
        {ctrl(Key::Backslash), "view.split"},
        {{.key = Key::F, .modifiers = {.ctrl = true, .shift = true}}, "files.find"},
        {ctrl(Key::PageDown), "tab.next"},
        {ctrl(Key::PageUp), "tab.previous"},
        {ctrl(Key::Comma), "edit.preferences"},
    };
    for (const auto& [chord, command] : program) {
        keys.bind(Program::PROGRAM, {chord}, std::string(command));
        // With Vim on, Ctrl W deletes the word before the caret in insert
        // mode and begins the window commands in normal mode; a tab closes
        // by its button or the File menu.
        if (command != "file.close") keys.bind(Program::VIM_PROGRAM, {chord}, std::string(command));
    }
    return keys;
}

// The label a file's tab is drawn and known by: its name, and its path
// after ## so two files of one name each have their own.
std::string tab_label(const std::filesystem::path& path) {
    return path.filename().string() + "##" + path.generic_string();
}

// A view showing its code as `preferences` have it, but where Vim's :set
// changed it for the session; Vim on it sharing `registers` and `settings`
// with the other views, which are told what the views show.
void show_as_preferred(EditorView& view, const Preferences& preferences,
                       const std::shared_ptr<vim::Registers>& registers, const std::shared_ptr<vim::Settings>& settings,
                       const vim::PathLister& paths, const std::shared_ptr<const std::vector<vim::Mapping>>& maps) {
    view.minimap = preferences.minimap;
    view.text_size = static_cast<float>(preferences.text_size);
    view.line_numbers = settings->relative_numbers
                            ? (*settings->relative_numbers ? LineNumbers::Relative : LineNumbers::Absolute)
                            : preferences.line_numbers;
    view.around_caret = settings->around_caret.value_or(static_cast<std::size_t>(preferences.around_caret));
    settings->relative_numbers_shown = view.line_numbers == LineNumbers::Relative;
    settings->around_caret_shown = view.around_caret;
    // Vim turned on starts each view in its normal mode, listing paths
    // after `:e` as the program does.
    if (preferences.vim && !view.vim) {
        view.vim.emplace(registers, settings);
        view.vim->list_paths_with(paths);
    }
    view.maps.use(maps);
    if (!preferences.vim) view.vim.reset();
    // The program's own commands, by their titles, for the keys that can
    // come next.
    if (!view.command_title) {
        view.command_title = [](std::string_view name) {
            const auto command = std::ranges::find(Program::commands(), name, &Program::Command::name);
            return command == Program::commands().end() ? std::string() : std::string(command->title);
        };
    }
}

// How the status bar names Vim's mode, and the tone of its pill (sheet 47,
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

} // namespace

Program::Program(std::filesystem::path root, Preferences& preferences, ProgramOptions options)
    : workspace_(std::move(root)), keymap_(program_keys()), tree_(workspace_.root()), groups_(1),
      preferences_(preferences), pick_folder_(std::move(options.pick_folder)), wake_(std::move(options.wake)),
      services_(noting_paths(options.services ? std::move(*options.services)
                                              : std::vector<ServiceMaker>{clangd(), fjls()}),
                workspace_.root()),
      keys_path_(std::move(options.keys)), device_(options.device) {
    if (!options.recovery.empty()) recovery_.emplace(std::move(options.recovery));
    read_keys();
    if (!options.brand.empty()) {
        if (ftk::Result<ftk::SvgPicture> mark = ftk::SvgPicture::load(options.brand / "fjedit-mark-dark.svg")) {
            mark_ = std::move(*mark);
        } else {
            FTK_CORE_WARN("fjedit shows the file icon for its mark: {}", mark.error());
        }
    }
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
                    .if_missing = "set its path in Preferences",
                });
            }};
}

ServiceMaker Program::fjls() {
    // A game's script headers, beside clangd; fjell's own files and GLSL
    // alone.
    static const Language& GLSL = language_for("shader.glsl");
    static const std::vector<std::string> EXTENSIONS = [] {
        std::vector<std::string> extensions{"hpp", "fjss", "fjwidget", "fjfont", "fjsl"};
        for (const std::string_view extension : extensions_of(GLSL)) extensions.emplace_back(extension);
        return extensions;
    }();
    return {.name = "fjls",
            .extensions = EXTENSIONS,
            .make = [this](const std::filesystem::path& root) -> std::unique_ptr<LanguageService> {
                return std::make_unique<lsp::Client>(lsp::ClientOptions{
                    .command = {preferences_.fjls},
                    .root = root,
                    .extensions = EXTENSIONS,
                    .language_id = [](const std::filesystem::path& path) {
                        const std::string extension = path.extension().string();
                        if (extension == ".hpp") return std::string("cpp");
                        // A stage's file and an include alike.
                        if (&language_for(path.filename().string()) == &GLSL) return std::string("glsl");
                        return extension.substr(1);
                    },
                    .arrived = wake_,
                    .if_missing = "set its path in Preferences",
                });
            }};
}

std::vector<ServiceMaker> Program::noting_paths(std::vector<ServiceMaker> makers) {
    for (ServiceMaker& maker : makers) {
        const bool clangd = maker.name == "clangd";
        if ((!clangd && maker.name != "fjls") || !maker.make) continue;
        std::string* started = clangd ? &clangd_started_ : &fjls_started_;
        const std::string* wanted = clangd ? &preferences_.clangd : &preferences_.fjls;
        maker.make = [started, wanted, make = std::move(maker.make)](const std::filesystem::path& root) {
            *started = *wanted;
            return make(root);
        };
    }
    return makers;
}

void Program::follow_server_paths() {
    bool restarted = false;
    for (const auto& [name, started, wanted] : {std::tuple{"clangd", &clangd_started_, &preferences_.clangd},
                                                std::tuple{"fjls", &fjls_started_, &preferences_.fjls}}) {
        if (!services_.status_of(name) || *started == *wanted) continue;
        services_.restart(name);
        restarted = true;
    }
    if (!restarted) return;
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

LanguageServerShown Program::fjls_shown() const {
    return {.status = services_.status_of("fjls"), .reads = {}};
}

std::unique_ptr<EditorView> Program::make_view(const std::filesystem::path& path, Document& document) {
    auto view = std::make_unique<EditorView>(document);
    show_as_preferred(*view, preferences_, registers_, vim_settings_, path_lister_, maps_);
    if (services_.serves(path)) {
        view->service = &services_;
        view->path = path;
        services_.open(path, document);
    }
    return view;
}

void Program::let_go(const std::filesystem::path& path) {
    // Its files' marks and the places jumped from in it wait for it to open
    // again.
    if (const Document* document = workspace_.find(path)) {
        for (const auto& [name, at] : document->marks()) {
            if (name >= 'A' && name <= 'Z') closed_marks_[name] = {.path = workspace_.name(path), .at = at};
        }
        for (Group& group : groups_) group.jumps.closed(workspace_.name(path), *document);
    }
    services_.close(path);
    workspace_.close(path);
}

void Program::go_to(const std::filesystem::path& path, Span span) {
    keep_jump();
    open(path);
    Tab* tab = front_tab();
    const Document* document = tab != nullptr ? workspace_.find(tab->path) : nullptr;
    if (document == nullptr) return;
    const Buffer& buffer = document->buffer();
    tab->view->selections = Selections({.anchor = buffer.clamp(span.from), .caret = buffer.clamp(span.to)});
    tab->view->reveal = true;
}

std::filesystem::path Program::keys_file() {
    return ftk::platform::config_dir() / "fjedit" / "keys.vim";
}

std::span<const Program::Command> Program::commands() {
    static constexpr Command COMMANDS[] = {
        {"file.open_folder", "Open folder"}, {"file.save", "Save"},    {"file.save_all", "Save all"},
        {"file.close", "Close"},             {"file.quit", "Quit"},    {"view.split", "Split"},
        {"files.find", "Find in files"},     {"tab.next", "Next tab"}, {"tab.previous", "Previous tab"},
        {"edit.preferences", "Preferences"},
    };
    return COMMANDS;
}

bool Program::has_command(std::string_view name) const {
    return std::ranges::contains(commands(), name, &Command::name) || find_command(name) != nullptr ||
           is_view_command(name);
}

void Program::read_keys() {
    keymap_ = program_keys();
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
    // Each bind into the keymap mode it names, over the program's own.
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

void Program::open_keys() {
    if (keys_path_.empty()) return;
    std::error_code error;
    if (!std::filesystem::exists(keys_path_, error)) {
        std::filesystem::create_directories(keys_path_.parent_path(), error);
        const ftk::Result<> made = write_text_file(keys_path_, KEYS_FILE_START);
        if (!made) {
            note_ = made.error();
            return;
        }
    }
    open(keys_path_);
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
    // The files' marks it had when it was let go are its again.
    for (auto kept = closed_marks_.begin(); kept != closed_marks_.end();) {
        if (kept->second.path != named) {
            ++kept;
            continue;
        }
        (*document)->set_mark(kept->first, kept->second.at);
        kept = closed_marks_.erase(kept);
    }
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

bool Program::run(std::string_view command) {
    Group& group = groups_[active_];
    Tab* tab = front_tab();
    if (command == "file.open_folder") {
        if (pick_folder_) pick_folder_();
    } else if (command == "file.save") {
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
    // keys.vim saved here applies at once.
    if (!keys_path_.empty() && workspace_.name(path) == workspace_.name(keys_path_)) read_keys();
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
    leaving_for_.reset();
    const std::vector<std::filesystem::path> files = workspace_.files();
    if (std::ranges::none_of(files, [&](const std::filesystem::path& path) { return workspace_.unsaved(path); })) {
        quitting_ = true;
        return true;
    }
    quit_asked_ = true;
    return false;
}

void Program::open_folder(std::filesystem::path folder) {
    if (!first_unsaved()) {
        folder_wanted_ = std::move(folder);
        return;
    }
    leaving_for_ = std::move(folder);
    quit_asked_ = true;
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
    path_listings_.clear();
    keep_unsaved();
}

void Program::close_front() {
    const Tab* tab = front_tab();
    if (tab == nullptr) return;
    // Another tab showing the file keeps its changes: closing this one
    // loses nothing.
    if (tabs_showing(tab->path) == 1 && workspace_.unsaved(tab->path)) {
        closing_ = tab->path;
        return;
    }
    close_tab(groups_[active_], groups_[active_].front);
}

std::size_t Program::tabs_showing(const std::filesystem::path& path) const {
    std::size_t showing = 0;
    for (const Group& group : groups_) showing += static_cast<std::size_t>(std::ranges::count(group.tabs, path, &Tab::path));
    return showing;
}

std::vector<vim::PathEntry> Program::list_paths(std::string_view folder, bool hidden) {
    const std::filesystem::path at = workspace_.root() / std::filesystem::path(folder);
    const auto listed = path_listings_.find({at, hidden});
    if (listed != path_listings_.end()) return listed->second;
    std::vector<vim::PathEntry> entries;
    for (const FolderEntry& entry : read_folder(at, hidden)) entries.push_back({.name = entry.name, .folder = entry.folder});
    return path_listings_.emplace(std::pair{at, hidden}, std::move(entries)).first->second;
}

std::optional<std::filesystem::path> Program::first_unsaved() const {
    for (const std::filesystem::path& path : workspace_.files()) {
        if (workspace_.unsaved(path)) return path;
    }
    return std::nullopt;
}

void Program::run_ex(const vim::ExCommand& command) {
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
    const bool loses = workspace_.unsaved(path) && tabs_showing(path) == 1;
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
        if (name == "wq" || workspace_.unsaved(path)) save(path);
        if (workspace_.unsaved(path)) return;
        close();
    } else if (name == "qall") {
        const std::optional<std::filesystem::path> unsaved = first_unsaved();
        if (unsaved && !command.bang) {
            return refuse(std::format("E162: No write since last change for buffer \"{}\"", unsaved->filename().string()));
        }
        for (const std::filesystem::path& file : workspace_.files()) {
            if (workspace_.unsaved(file)) forget_kept(file);
        }
        quitting_ = true;
    } else if (name == "edit" && !command.argument.empty()) {
        // A path from the folder open, as Vim's from the folder it works in;
        // a jump, as in Vim.
        keep_jump();
        open(workspace_.root() / std::filesystem::path(command.argument));
    } else if (name == "edit") {
        // The file read again; with !, its changes dropped. Undone, they
        // come back.
        if (workspace_.unsaved(path) && !command.bang) return refuse(std::string(NO_WRITE));
        if (const ftk::Result<> reloaded = workspace_.reload(path); !reloaded) {
            note_ = reloaded.error();
            return;
        }
        changed_.erase(path);
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

void Program::answer_vim(const vim::Asked& asked) {
    if (const auto* command = std::get_if<vim::ExCommand>(&asked)) return run_ex(*command);
    if (const auto* set = std::get_if<vim::MarkSet>(&asked)) return keep_file_mark(set->name);
    if (const auto* wanted = std::get_if<vim::MarkWanted>(&asked)) return go_to_file_mark(*wanted);
    if (const auto* from = std::get_if<vim::JumpFrom>(&asked)) return keep_jump(*from);
    step_jumps(std::get<vim::JumpStep>(asked).count);
}

void Program::keep_jump() {
    const Tab* tab = front_tab();
    const Document* document = tab != nullptr ? workspace_.find(tab->path) : nullptr;
    if (document == nullptr) return;
    keep_jump({.at = tab->view->selections.primary().caret, .version = document->version()});
}

void Program::keep_jump(const vim::JumpFrom& from) {
    const Tab* tab = front_tab();
    if (tab == nullptr) return;
    groups_[active_].jumps.jumped({.path = tab->path, .at = from.at, .version = from.version});
}

void Program::step_jumps(long count) {
    const Tab* tab = front_tab();
    const Document* document = tab != nullptr ? workspace_.find(tab->path) : nullptr;
    if (document == nullptr) return;
    JumpList& jumps = groups_[active_].jumps;
    jumps.catch_up(workspace_);
    const JumpList::Place here{.path = tab->path, .at = tab->view->selections.primary().caret, .version = document->version()};
    const std::optional<JumpList::Place> to = jumps.step(count, here);
    if (!to) return;
    if (to->path != tab->path) open(to->path);
    Tab* now = front_tab();
    const Document* there = now != nullptr ? workspace_.find(now->path) : nullptr;
    // Not opened: what went wrong is noted.
    if (there == nullptr || now->path != to->path) return;
    now->view->selections = Selections(Selection::at(there->buffer().clamp(to->at)));
    now->view->reveal = true;
}

void Program::keep_file_mark(char name) {
    const Tab* tab = front_tab();
    if (tab == nullptr) return;
    const Document* own = workspace_.find(tab->path);
    closed_marks_.erase(name);
    for (const std::filesystem::path& path : workspace_.files()) {
        Document* document = workspace_.find(path);
        if (document != nullptr && document != own) document->forget_mark(name);
    }
}

void Program::go_to_file_mark(const vim::MarkWanted& wanted) {
    // The file open that has it, or the one let go with it.
    std::optional<std::filesystem::path> holder;
    for (const std::filesystem::path& path : workspace_.files()) {
        const Document* document = workspace_.find(path);
        if (document != nullptr && document->mark(wanted.name)) {
            holder = path;
            break;
        }
    }
    if (const auto kept = closed_marks_.find(wanted.name); !holder && kept != closed_marks_.end()) {
        holder = kept->second.path;
    }
    if (!holder) {
        if (Tab* tab = front_tab(); tab != nullptr && tab->view->vim) {
            tab->view->vim->say({.text = "E20: Mark not set", .error = true});
        }
        return;
    }
    keep_jump();
    open(*holder);
    Tab* tab = front_tab();
    const Document* document = tab != nullptr ? workspace_.find(tab->path) : nullptr;
    const std::optional<Position> at = document != nullptr ? document->mark(wanted.name) : std::nullopt;
    // Not opened: what went wrong is noted.
    if (!at) return;
    const Position to = wanted.exact ? *at : vim::first_non_blank(document->buffer(), at->line);
    tab->view->selections = Selections(Selection::at(to));
    tab->view->reveal = true;
}

void Program::focus_group(std::size_t index) {
    active_ = index;
    if (Tab* tab = front_tab()) tab->view->focus = true;
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
        // The view that had the keyboard went with the group: it goes to the
        // group in front now.
        if (Tab* front = front_tab()) front->view->focus = true;
    }
}

void Program::split(std::size_t index) {
    if (index >= groups_.size()) return;
    const Group& from = groups_[index];
    if (from.front >= from.tabs.size()) return;
    const Tab& shown = from.tabs[from.front];
    Document* document = workspace_.find(shown.path);
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
        const KeyMatch match =
            keymap_.find(preferences_.vim ? VIM_PROGRAM : PROGRAM, std::vector<Chord>{{.key = key, .modifiers = held}});
        if (match.command) (void)run(*match.command);
    }
}

void Program::draw() {
    follow_server_paths();
    // What the language services found since, before anything shows it.
    services_.update();
    check_disk(ui::core::current().time());
    // What was just done is said until the next key.
    if (!ui::core::current().keystrokes().empty()) told_.clear();
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
                                               static_cast<float>(preferences_.text_size), preferences_.vim));
    }
    if (const std::optional<Location> going = std::exchange(going_, std::nullopt)) go_to(going->path, going->span);
    for (const vim::Asked& asked : std::exchange(vim_asked_, {})) answer_vim(asked);
    ui::end_host_window();
    draw_status();
    if (preferences_window_.draw(preferences_, clangd_shown(), fjls_shown(), keys_path_).open_keys) open_keys();
    draw_prompts();
}

std::string Program::shortcut(std::string_view command) const {
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

void Program::draw_menus() {
    const bool open_file = front_tab() != nullptr;
    const auto entry = [&](std::string label, std::string_view command, bool enabled = true) {
        return ui::MenuEntry{.label = std::move(label),
                             .shortcut = shortcut(command),
                             .enabled = enabled,
                             .chosen = [this, command] { (void)run(command); }};
    };
    const std::vector<ui::Menu> menus{
        {.label = "File",
         .entries = [&] {
             return std::vector<ui::MenuEntry>{entry("Open folder", "file.open_folder", static_cast<bool>(pick_folder_)),
                                               ui::MenuEntry::separator(),
                                               entry("Save", "file.save", open_file), entry("Save all", "file.save_all"),
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
        const char* title = "No file open";
        const char* what_to_do = "Open one from the files on the left";
        if (mark_) {
            // fjedit's logomark where the file icon would be, drawn at the
            // pixels it covers.
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
        show_as_preferred(*tab.view, preferences_, registers_, vim_settings_, path_lister_, maps_);
        const std::optional<std::filesystem::path> unsaved = first_unsaved();
        tab.view->file_facts = {.file = tab.path.filename().string(),
                                .closing_loses = workspace_.unsaved(tab.path) && tabs_showing(tab.path) == 1,
                                .unsaved = unsaved ? unsaved->filename().string() : std::string()};
        Document* document = workspace_.find(tab.path);
        const glm::vec2 at = ui::cursor();
        const glm::vec2 room = ui::space_left();
        if (document != nullptr && room.x > 0.0f && room.y > 0.0f) {
            const CodeViewResult result = code_view("Code", *document, *tab.view, keymap_, {.min = at, .max = at + room});
            if (result.focused) active_ = index;
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
            if (result.rename && services_.serves(tab.path)) {
                renaming_ = Renaming{.rename = *result.rename,
                                     .asked = services_.rename(tab.path, result.rename->at, result.rename->to)};
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
        const Tab* tab = front_tab();
        // With Vim on, the mode of the view in front first, then the keys of
        // a command half typed in it, in the mono face.
        if (tab != nullptr && tab->view->vim) {
            const vim::Engine& engine = *tab->view->vim;
            const ModeShown mode = mode_shown(engine.mode());
            ui::status_pill(nullptr, mode.name, mode.tone);
            if (const std::optional<char> recording = engine.recording()) {
                ui::status_item(nullptr, std::format("recording @{}", *recording));
            }
            std::string keys = engine.pending();
            for (const Chord& key : tab->view->pending) keys += vim::notation(key);
            for (const Chord& key : tab->view->maps.waiting()) keys += vim::notation(key);
            if (!keys.empty()) {
                const ui::FontScope mono(theme::mono_font(), theme::SMALL_TEXT);
                ui::status_item(nullptr, keys);
            }
            // What the last key had to say, where Vim says it.
            if (const vim::Message& said = engine.message(); !said.text.empty()) {
                ui::status_message(said.error ? ui::StatusTone::Error : ui::StatusTone::Plain, said.text);
            }
            // The first line of keys.vim that couldn't be used.
            if (!keys_errors_.empty()) {
                const vim::KeysError& first = keys_errors_.front();
                ui::status_note(ui::StatusTone::Warning, first.message,
                                first.line == 0 ? std::string("keys.vim") : std::format("keys.vim:{}", first.line));
            }
            ui::status_rule();
        }

        // The problems in the files open, and the first of the worst in the
        // file in front, with the file and line it is at.
        std::size_t errors = 0;
        std::size_t warnings = 0;
        const Diagnostic* first = nullptr;
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
        if (!told_.empty()) ui::status_message(ui::StatusTone::Plain, told_);
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
    if (!renaming_ || !renaming_->asked->answered) return;
    const ftk::Result<WorkspaceEdit> edits = renaming_->asked->answer;
    const Rename rename = renaming_->rename;
    renaming_.reset();
    if (!edits) {
        note_ = edits.error();
        return;
    }
    make_edits(*edits);
    told_ = std::format("Renamed {} to {} in {} file{}", rename.from, rename.to, edits->size(), edits->size() == 1 ? "" : "s");
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
    // Another folder opened in this one's place closes this one.
    const ui::UnsavedAnswer answer =
        ui::unsaved_list_dialog("##quit", title.c_str(), quit_items_, leaving_for_ ? "close" : "quit");
    if (answer == ui::UnsavedAnswer::None) return;
    const std::vector<std::filesystem::path> paths = std::exchange(quit_paths_, {});
    const std::vector<ui::UnsavedItem> items = std::exchange(quit_items_, {});
    if (answer == ui::UnsavedAnswer::Cancel) {
        leaving_for_.reset();
        return;
    }
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (answer == ui::UnsavedAnswer::Save && items[i].save) {
            save(paths[i]);
            // A file that couldn't be saved keeps the program open.
            if (workspace_.unsaved(paths[i])) {
                leaving_for_.reset();
                return;
            }
        } else {
            forget_kept(paths[i]);
        }
    }
    if (leaving_for_) {
        folder_wanted_ = std::exchange(leaving_for_, std::nullopt);
    } else {
        quitting_ = true;
    }
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
