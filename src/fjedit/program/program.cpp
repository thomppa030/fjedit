#include "fjedit/program/program.hpp"

#include "fjedit/core/vim/notation.hpp"
#include "fjedit/program/icons.hpp"

#include "ftk/base/log.hpp"
#include "ftk/platform/platform.hpp"

#include "ftk/ui/core/context.hpp"
#include "ftk/ui/kit/button.hpp"
#include "ftk/ui/kit/dialog.hpp"
#include "ftk/ui/kit/icons.hpp"
#include "ftk/ui/kit/layout.hpp"
#include "ftk/ui/kit/menu.hpp"
#include "ftk/ui/kit/pane.hpp"
#include "ftk/ui/kit/region.hpp"
#include "ftk/ui/kit/settings_file.hpp"
#include "ftk/ui/kit/status_bar.hpp"
#include "ftk/ui/kit/text.hpp"
#include "ftk/ui/kit/viewport_toolbar.hpp"
#include "ftk/ui/kit/window.hpp"
#include "ftk/ui/theme.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace fjedit {

namespace {

namespace ui = ftk::ui;
namespace theme = ftk::theme;
using ftk::ui::Key;

// The side bar's width (sheet 39).
constexpr float SIDE_WIDTH = 260.0f;

// The program's own commands, beside its code area's.
std::span<const CodeCommand> own_commands() {
    static const CodeCommand COMMANDS[] = {
        {.name = "file.open_folder", .title = "Open folder"},
        {.name = "file.quit", .title = "Quit", .key = Chord{.key = Key::Q, .modifiers = {.ctrl = true}}},
        {.name = "edit.preferences", .title = "Preferences", .key = Chord{.key = Key::Comma, .modifiers = {.ctrl = true}}},
    };
    return COMMANDS;
}

// fjedit's logomark from its brand files `brand`, for a group with no file
// open; none when there are none or it doesn't read.
std::optional<ftk::SvgPicture> load_mark(const std::filesystem::path& brand) {
    if (brand.empty()) return std::nullopt;
    ftk::Result<ftk::SvgPicture> mark = ftk::SvgPicture::load(brand / "fjedit-mark-dark.svg");
    if (!mark) {
        FTK_CORE_WARN("fjedit shows the file icon for its mark: {}", mark.error());
        return std::nullopt;
    }
    return std::move(*mark);
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

} // namespace

Program::Program(std::filesystem::path root, Preferences& preferences, ProgramOptions options)
    : files_(std::move(root), preferences,
             {.recovery = std::move(options.recovery), .services = std::move(options.services), .wake = std::move(options.wake)}),
      code_(files_, preferences,
            {.keys = std::move(options.keys),
             .commands = {own_commands().begin(), own_commands().end()},
             .run = [this](std::string_view command) { return run(command); },
             .leave = [this] { quitting_ = true; },
             .mark = load_mark(options.brand),
             .device = options.device,
             .program_name = "fjedit"}),
      tree_(files_.root()), preferences_(preferences), pick_folder_(std::move(options.pick_folder)),
      desktop_entry_(std::move(options.desktop_entry)), desktop_data_home_(std::move(options.desktop_data_home)) {}

void Program::update_files(double now) {
    ui::core::current().request_frame_at(std::max(files_.next_check(), now));
    const CodeFiles::Update update = files_.update(now);
    if (update.looked) tree_.forget_listings();
    code_.files_updated(update);
}

LanguageServerShown Program::clangd_shown() const {
    LanguageServerShown shown{.status = files_.services().status_of("clangd"), .reads = {}};
    // Where clangd looks for them in the folder open, as it does from a
    // file at its top.
    for (const char* name : {"compile_commands.json", "build/compile_commands.json", "compile_flags.txt"}) {
        std::error_code error;
        if (std::filesystem::exists(files_.root() / name, error)) {
            shown.reads = name;
            break;
        }
    }
    return shown;
}

LanguageServerShown Program::fjls_shown() const {
    return {.status = files_.services().status_of("fjls"), .reads = {}};
}

ftk::DesktopEntry Program::desktop_entry(const std::filesystem::path& program, const std::filesystem::path& brand) {
    return {.id = "fjedit",
            .name = "fjedit",
            .comment = "Code editor",
            .categories = {"Development", "TextEditor"},
            .program = program,
            .icon = brand / "fjedit-tile.svg"};
}

void Program::create_desktop_entry() {
    const ftk::Result<std::filesystem::path> written = ftk::create_desktop_entry(*desktop_entry_, desktop_data_home_);
    if (written) {
        entry_answer_ =
            EntryAnswer{.written = true, .text = "fjedit is in your app launcher", .where = written->string()};
    } else {
        entry_answer_ = EntryAnswer{.written = false, .text = "Couldn't write the desktop entry: " + written.error()};
    }
}

std::span<const CodeCommand> Program::commands() {
    static const std::vector<CodeCommand> COMMANDS = [] {
        std::vector<CodeCommand> all(CodeArea::commands().begin(), CodeArea::commands().end());
        all.insert(all.end(), own_commands().begin(), own_commands().end());
        return all;
    }();
    return COMMANDS;
}

std::filesystem::path Program::recovery_folder(const std::filesystem::path& root) {
    return Recovery::folder_for(ftk::platform::state_dir() / "fjedit" / "recovery", Workspace(root).root());
}

bool Program::run(std::string_view command) {
    if (command == "file.open_folder") {
        if (pick_folder_) pick_folder_();
    } else if (command == "file.quit") {
        (void)ask_to_quit();
    } else if (command == "edit.preferences") {
        preferences_window_.open();
    } else {
        return code_.run(command);
    }
    return true;
}

bool Program::ask_to_quit() {
    if (quitting_) return true;
    leaving_for_.reset();
    if (!files_.first_unsaved()) {
        quitting_ = true;
        return true;
    }
    quit_asked_ = true;
    return false;
}

void Program::open_folder(std::filesystem::path folder) {
    if (!files_.first_unsaved()) {
        folder_wanted_ = std::move(folder);
        return;
    }
    leaving_for_ = std::move(folder);
    quit_asked_ = true;
}

void Program::draw() {
    update_files(ui::core::current().time());
    // What was just done is said until the next key.
    if (!ui::core::current().keystrokes().empty()) entry_answer_.reset();
    code_.take_keys();
    draw_menus();
    const ui::WorkArea work = ui::work_area();
    const float height = work.size.y - theme::STATUS_BAR;
    ui::next_window_place(work.pos, {work.size.x, height});
    (void)ui::begin_host_window("##fjedit", {.scrollbar = false, .to_front = false});
    if (auto side = ui::Pane("##side", {SIDE_WIDTH, height})) draw_side();
    ui::same_line(0.0f);
    if (auto editor = ui::Region("##editor", {0.0f, height}, {.wheel_scrolls = false})) {
        const glm::vec2 code_min = ui::cursor();
        const glm::vec2 code_max = code_min + ui::space_left();
        code_.draw();
        // Find in files floats over the code, under the tabs.
        code_.draw_find_in_files({.min = {code_min.x, code_.prompts_at().y}, .max = code_max});
    }
    ui::end_host_window();
    draw_status();
    if (preferences_window_.draw(preferences_, clangd_shown(), fjls_shown(), code_.keys_path()).open_keys) {
        code_.open_keys();
    }
    code_.draw_prompts();
    ask_quit();
}

void Program::draw_menus() {
    const bool open_file = code_.front().has_value();
    const auto entry = [&](std::string label, std::string_view command, bool enabled = true) {
        return ui::MenuEntry{.label = std::move(label),
                             .shortcut = code_.shortcut(command),
                             .enabled = enabled,
                             .chosen = [this, command] { (void)run(command); }};
    };
    const std::vector<ui::Menu> menus{
        {.label = "File",
         .entries = [&] {
             std::vector<ui::MenuEntry> file{entry("Open folder", "file.open_folder", static_cast<bool>(pick_folder_)),
                                             ui::MenuEntry::separator(),
                                             entry("Save", "file.save", open_file), entry("Save all", "file.save_all"),
                                             ui::MenuEntry::separator(), entry("Close", "file.close", open_file),
                                             ui::MenuEntry::separator()};
             // On a system with desktop entries, a group of its own.
             if (desktop_entry_ && !desktop_data_home_.empty()) {
                 file.push_back({.label = "Create desktop entry",
                                 .icon = icon::desktop_entry,
                                 .chosen = [this] { create_desktop_entry(); }});
                 file.push_back(ui::MenuEntry::separator());
             }
             file.push_back(entry("Quit", "file.quit"));
             return file;
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
    if (ui::icon_button("##find_in_files", ui::icon::search, "Find in files (Ctrl Shift F)")) (void)code_.run("files.find");
    ui::set_cursor(below);
    ui::separator();
    const std::optional<std::filesystem::path> shown = code_.front();
    if (const std::optional<std::filesystem::path> pressed = tree_.draw(shown.value_or(std::filesystem::path{}))) {
        code_.open(*pressed);
    }
}

void Program::draw_status() {
    if (auto bar = ui::StatusBar()) {
        const EditorView* view = code_.front_view();
        const std::optional<std::filesystem::path> front = code_.front();
        // With Vim on, the mode of the view in front first, then the keys of
        // a command half typed in it, in the mono face.
        if (view != nullptr && view->vim) {
            const vim::Engine& engine = *view->vim;
            const ModeShown mode = mode_shown(engine.mode());
            ui::status_pill(nullptr, mode.name, mode.tone);
            if (const std::optional<char> recording = engine.recording()) {
                ui::status_item(nullptr, std::format("recording @{}", *recording));
            }
            std::string keys = engine.pending();
            for (const Chord& key : view->pending) keys += vim::notation(key);
            for (const Chord& key : view->maps.waiting()) keys += vim::notation(key);
            if (!keys.empty()) {
                const ui::FontScope mono(theme::mono_font(), theme::SMALL_TEXT);
                ui::status_item(nullptr, keys);
            }
            // What the last key had to say, where Vim says it.
            if (const vim::Message& said = engine.message(); !said.text.empty()) {
                ui::status_message(said.error ? ui::StatusTone::Error : ui::StatusTone::Plain, said.text);
            }
            // The first line of keys.vim that couldn't be used.
            if (const std::vector<vim::KeysError>& keys_errors = code_.keys_errors(); !keys_errors.empty()) {
                const vim::KeysError& first = keys_errors.front();
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
        LanguageServices& services = files_.services();
        for (const std::filesystem::path& path : services.files()) {
            for (const Diagnostic& problem : services.diagnostics(path)) {
                errors += problem.severity == Severity::Error ? 1 : 0;
                warnings += problem.severity == Severity::Warning ? 1 : 0;
                if (!front || path != *front || problem.severity > Severity::Warning) continue;
                if (first == nullptr || problem.severity < first->severity) first = &problem;
            }
        }
        const bool served = view != nullptr && view->service != nullptr;
        if (served || errors + warnings > 0) {
            ui::status_item(ui::icon::error, std::to_string(errors));
            ui::status_item(ui::icon::warning, std::to_string(warnings));
        }
        if (first != nullptr) {
            const std::string_view message = first->message;
            ui::status_note(message.substr(0, message.find('\n')),
                            std::format("{}:{}", front->filename().string(), first->span.from.line + 1));
        }
        if (!files_.note().empty()) ui::status_item(ui::icon::warning, files_.note());
        if (!code_.told().empty()) ui::status_message(ui::StatusTone::Plain, code_.told());
        // While a colour is picked from its swatch, what Esc and Enter do.
        if (view != nullptr) {
            if (const std::optional<std::string> hint = picking_hint(*view)) {
                ui::status_message(ui::StatusTone::Plain, *hint);
            }
        }
        if (entry_answer_) {
            ui::status_note(entry_answer_->written ? ui::StatusTone::Success : ui::StatusTone::Error, entry_answer_->text,
                            entry_answer_->where);
        }
        bar.right();
        const Document* document = front ? files_.workspace().find(*front) : nullptr;
        if (document == nullptr) return;
        const Buffer& buffer = document->buffer();
        const Position caret = view->selections.primary().caret;
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
        for (const ServiceShown& service : services.shown(*front)) {
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

void Program::ask_quit() {
    if (quit_asked_) {
        quit_asked_ = false;
        quit_paths_.clear();
        quit_items_.clear();
        for (const std::filesystem::path& path : files_.unsaved_files()) {
            quit_paths_.push_back(path);
            quit_items_.push_back(file_item(path, files_.root()));
        }
        if (!quit_items_.empty()) ui::open_dialog("##quit", {code_.prompts_at(), 0.5f});
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
            code_.save(paths[i]);
            // A file that couldn't be saved keeps the program open.
            if (files_.unsaved(paths[i])) {
                leaving_for_.reset();
                return;
            }
        } else {
            files_.forget_kept(paths[i]);
        }
    }
    if (leaving_for_) {
        folder_wanted_ = std::exchange(leaving_for_, std::nullopt);
    } else {
        quitting_ = true;
    }
}

} // namespace fjedit
