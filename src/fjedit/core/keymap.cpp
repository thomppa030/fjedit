#include "fjedit/core/keymap.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace fjedit {

namespace {

using ftk::ui::Key;

// Every key's name in chord text, in the order of ftk::ui::Key.
constexpr std::string_view KEY_NAMES[] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V",
    "W", "X", "Y", "Z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "F1", "F2", "F3", "F4", "F5", "F6",
    "F7", "F8", "F9", "F10", "F11", "F12", "Esc", "Enter", "Tab", "Space", "Backspace", "Delete", "Insert",
    "Home", "End", "PageUp", "PageDown", "Left", "Right", "Up", "Down", "-", "=", "[", "]", "\\", ";", "'", ",",
    ".", "/", "`",
};
static_assert(std::size(KEY_NAMES) == ftk::ui::KEY_COUNT, "every key needs its name in chord text");

std::optional<Key> key_named(std::string_view name) {
    for (std::size_t i = 0; i < std::size(KEY_NAMES); ++i) {
        if (KEY_NAMES[i] == name) return static_cast<Key>(i);
    }
    return std::nullopt;
}

Chord chord(Key key, bool ctrl = false, bool shift = false, bool alt = false) {
    return {.key = key, .modifiers = {.ctrl = ctrl, .shift = shift, .alt = alt}};
}

} // namespace

std::string chord_text(const Chord& chord) {
    if (!chord.text.empty()) return chord.text;
    std::string text;
    if (chord.modifiers.ctrl) text += "Ctrl ";
    if (chord.modifiers.shift) text += "Shift ";
    if (chord.modifiers.alt) text += "Alt ";
    if (chord.modifiers.super) text += "Super ";
    text += KEY_NAMES[static_cast<std::size_t>(chord.key)];
    return text;
}

std::string chords_text(std::span<const Chord> chords) {
    std::string text;
    bool typed_before = false;
    for (const Chord& chord : chords) {
        const bool typed = !chord.text.empty();
        if (!text.empty() && !(typed && typed_before)) text += ' ';
        text += chord_text(chord);
        typed_before = typed;
    }
    return text;
}

std::optional<Chord> parse_chord(std::string_view text) {
    Chord chord;
    std::optional<Key> key;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t end = std::min(text.find(' ', at), text.size());
        const std::string_view word = text.substr(at, end - at);
        at = end + 1;
        if (word.empty()) continue;
        if (key) return std::nullopt; // nothing comes after the key

        bool* modifier = word == "Ctrl"    ? &chord.modifiers.ctrl
                         : word == "Shift" ? &chord.modifiers.shift
                         : word == "Alt"   ? &chord.modifiers.alt
                         : word == "Super" ? &chord.modifiers.super
                                           : nullptr;
        if (modifier != nullptr) {
            if (*modifier) return std::nullopt;
            *modifier = true;
            continue;
        }
        key = key_named(word);
        if (!key) return std::nullopt;
    }
    if (!key) return std::nullopt;
    chord.key = *key;
    return chord;
}

void Keymap::bind(std::string_view mode, std::vector<Chord> keys, std::string command) {
    unbind(mode, keys);
    bindings_.push_back({.mode = std::string(mode), .keys = std::move(keys), .command = std::move(command)});
}

void Keymap::unbind(std::string_view mode, std::span<const Chord> keys) {
    std::erase_if(bindings_, [&](const Binding& b) {
        return b.mode == mode && std::equal(b.keys.begin(), b.keys.end(), keys.begin(), keys.end());
    });
}

KeyMatch Keymap::find(std::string_view mode, std::span<const Chord> pressed) const {
    KeyMatch match;
    for (const Binding& binding : bindings_) {
        if (binding.mode != mode || binding.keys.size() < pressed.size()) continue;
        if (!std::equal(pressed.begin(), pressed.end(), binding.keys.begin())) continue;
        if (binding.keys.size() == pressed.size()) {
            match.command = binding.command;
        } else {
            match.waiting = true;
        }
    }
    match.longer = match.command && match.waiting;
    if (match.command) match.waiting = false;
    return match;
}

std::vector<KeyNext> Keymap::next_keys(std::string_view mode, std::span<const Chord> pressed) const {
    std::vector<KeyNext> out;
    for (const Binding& binding : bindings_) {
        if (binding.mode != mode || binding.keys.size() <= pressed.size()) continue;
        if (!std::equal(pressed.begin(), pressed.end(), binding.keys.begin())) continue;
        const Chord& next = binding.keys[pressed.size()];
        auto found = std::ranges::find(out, next, &KeyNext::key);
        if (found == out.end()) {
            out.push_back({.key = next, .command = {}, .longer = {}});
            found = std::prev(out.end());
        }
        if (binding.keys.size() == pressed.size() + 1) {
            found->command = binding.command;
        } else {
            found->longer.push_back(binding.command);
        }
    }
    return out;
}

std::optional<std::vector<Chord>> Keymap::keys_for(std::string_view mode, std::string_view command) const {
    for (const Binding& binding : bindings_) {
        if (binding.mode == mode && binding.command == command) return binding.keys;
    }
    return std::nullopt;
}

Keymap Keymap::standard() {
    struct Default {
        Chord keys;
        std::string_view command;
    };
    constexpr bool CTRL = true;
    constexpr bool SHIFT = true;
    constexpr bool ALT = true;
    const Default defaults[] = {
        {chord(Key::Left), "caret.left"},
        {chord(Key::Right), "caret.right"},
        {chord(Key::Up), "caret.up"},
        {chord(Key::Down), "caret.down"},
        {chord(Key::Left, CTRL), "caret.word_left"},
        {chord(Key::Right, CTRL), "caret.word_right"},
        {chord(Key::Home), "caret.line_start"},
        {chord(Key::End), "caret.line_end"},
        {chord(Key::PageUp), "caret.page_up"},
        {chord(Key::PageDown), "caret.page_down"},
        {chord(Key::Home, CTRL), "caret.text_start"},
        {chord(Key::End, CTRL), "caret.text_end"},

        {chord(Key::Left, false, SHIFT), "select.left"},
        {chord(Key::Right, false, SHIFT), "select.right"},
        {chord(Key::Up, false, SHIFT), "select.up"},
        {chord(Key::Down, false, SHIFT), "select.down"},
        {chord(Key::Left, CTRL, SHIFT), "select.word_left"},
        {chord(Key::Right, CTRL, SHIFT), "select.word_right"},
        {chord(Key::Home, false, SHIFT), "select.line_start"},
        {chord(Key::End, false, SHIFT), "select.line_end"},
        {chord(Key::PageUp, false, SHIFT), "select.page_up"},
        {chord(Key::PageDown, false, SHIFT), "select.page_down"},
        {chord(Key::Home, CTRL, SHIFT), "select.text_start"},
        {chord(Key::End, CTRL, SHIFT), "select.text_end"},
        {chord(Key::A, CTRL), "select.all"},
        {chord(Key::L, CTRL), "select.line"},
        {chord(Key::D, CTRL), "select.next_match"},
        {chord(Key::L, CTRL, SHIFT), "select.all_matches"},

        {chord(Key::Up, CTRL, false, ALT), "cursor.add_above"},
        {chord(Key::Down, CTRL, false, ALT), "cursor.add_below"},
        {chord(Key::Escape), "cursor.single"},

        {chord(Key::Enter), "edit.newline"},
        {chord(Key::Tab), "edit.tab"},
        {chord(Key::Tab, false, SHIFT), "edit.outdent"},
        {chord(Key::Backspace), "edit.backspace"},
        {chord(Key::Backspace, false, SHIFT), "edit.backspace"},
        {chord(Key::Delete), "edit.delete"},
        {chord(Key::Backspace, CTRL), "edit.delete_word_left"},
        {chord(Key::Delete, CTRL), "edit.delete_word_right"},
        {chord(Key::K, CTRL, SHIFT), "edit.delete_lines"},
        {chord(Key::Up, false, false, ALT), "edit.move_lines_up"},
        {chord(Key::Down, false, false, ALT), "edit.move_lines_down"},
        {chord(Key::Up, false, SHIFT, ALT), "edit.copy_lines_up"},
        {chord(Key::Down, false, SHIFT, ALT), "edit.copy_lines_down"},
        {chord(Key::Enter, CTRL), "edit.insert_line_below"},
        {chord(Key::Enter, CTRL, SHIFT), "edit.insert_line_above"},
        {chord(Key::X, CTRL), "edit.cut"},
        {chord(Key::C, CTRL), "edit.copy"},
        {chord(Key::V, CTRL), "edit.paste"},
        // A terminal's copy and paste, after Ctrl C and V, which the menus
        // show.
        {chord(Key::C, CTRL, SHIFT), "edit.copy"},
        {chord(Key::V, CTRL, SHIFT), "edit.paste"},
        {chord(Key::Z, CTRL), "edit.undo"},
        {chord(Key::Y, CTRL), "edit.redo"},
        {chord(Key::Z, CTRL, SHIFT), "edit.redo"},

        {chord(Key::LeftBracket, CTRL, SHIFT), "fold.close"},
        {chord(Key::RightBracket, CTRL, SHIFT), "fold.open"},

        {chord(Key::F, CTRL), "find.open"},
        {chord(Key::H, CTRL), "find.replace"},
        {chord(Key::F3), "find.next"},
        {chord(Key::F3, false, SHIFT), "find.previous"},
        {chord(Key::G, CTRL), "goto.open"},
    };
    // Ctrl K, then a second key.
    const Default after_ctrl_k[] = {
        {chord(Key::L, CTRL), "fold.toggle"},
        {chord(Key::Digit0, CTRL), "fold.close_all"},
        {chord(Key::J, CTRL), "fold.open_all"},
    };

    Keymap keymap;
    for (const Default& binding : defaults) keymap.bind(EDITING, {binding.keys}, std::string(binding.command));
    for (const Default& binding : after_ctrl_k) {
        keymap.bind(EDITING, {chord(Key::K, CTRL), binding.keys}, std::string(binding.command));
    }
    keymap.bind(FINDING, {chord(Key::Escape)}, "find.close");
    keymap.bind(EDITING, {chord(Key::Space, CTRL)}, "completion.open");
    keymap.bind(EDITING, {chord(Key::F12)}, "definition.go");
    keymap.bind(EDITING, {chord(Key::F2)}, "symbol.rename");
    keymap.bind(EDITING, {chord(Key::I, CTRL, SHIFT)}, "format.document");
    const Default completing[] = {
        {chord(Key::Up), "completion.previous"},     {chord(Key::Down), "completion.next"},
        {chord(Key::PageUp), "completion.page_up"},  {chord(Key::PageDown), "completion.page_down"},
        {chord(Key::Enter), "completion.accept"},    {chord(Key::Tab), "completion.accept"},
        {chord(Key::Escape), "completion.close"},
    };
    for (const Default& binding : completing) {
        keymap.bind(COMPLETING, {binding.keys}, std::string(binding.command));
        if (binding.command != "completion.close") keymap.bind(VIM_COMPLETING, {binding.keys}, std::string(binding.command));
    }
    // Neovim's own keys for what a language server answers, in normal mode,
    // bound first so the menus show them.
    const auto typed = [](std::string_view text) {
        std::vector<Chord> keys;
        for (const char c : text) keys.push_back(Chord::typed(std::string_view(&c, 1)));
        return keys;
    };
    keymap.bind(VIM_NORMAL, typed("gd"), "definition.go");
    keymap.bind(VIM_NORMAL, {chord(Key::RightBracket, CTRL)}, "definition.go");
    keymap.bind(VIM_NORMAL, typed("grn"), "symbol.rename");
    keymap.bind(VIM_NORMAL, typed("K"), "hover.show");
    // The keys Vim gives no meaning that stay with it on (decision 4 of the
    // Vim plan).
    const Default with_vim[] = {
        {chord(Key::Space, CTRL), "completion.open"},
        {chord(Key::F12), "definition.go"},
        {chord(Key::F2), "symbol.rename"},
        {chord(Key::I, CTRL, SHIFT), "format.document"},
    };
    for (const Default& binding : with_vim) {
        keymap.bind(VIM_NORMAL, {binding.keys}, std::string(binding.command));
        keymap.bind(VIM_INSERT, {binding.keys}, std::string(binding.command));
    }
    // A terminal's copy and paste, which Vim gives no meaning either: they
    // run as Vim's keys for Copy and Paste in the mode ("+yy, "+P, Ctrl-R +).
    for (const std::string_view mode : {VIM_NORMAL, VIM_VISUAL}) {
        keymap.bind(mode, {chord(Key::C, CTRL, SHIFT)}, "edit.copy");
    }
    for (const std::string_view mode : {VIM_NORMAL, VIM_VISUAL, VIM_INSERT}) {
        keymap.bind(mode, {chord(Key::V, CTRL, SHIFT)}, "edit.paste");
    }
    // Insert mode's Ctrl-N and Ctrl-P, Vim's completion keys: they open the
    // list, then step down and up it.
    keymap.bind(VIM_INSERT, {chord(Key::N, CTRL)}, "completion.open");
    keymap.bind(VIM_INSERT, {chord(Key::P, CTRL)}, "completion.open");
    keymap.bind(VIM_COMPLETING, {chord(Key::N, CTRL)}, "completion.next");
    keymap.bind(VIM_COMPLETING, {chord(Key::P, CTRL)}, "completion.previous");
    // And Ctrl-J and Ctrl-K, as Neovim's completion is often set to.
    keymap.bind(VIM_COMPLETING, {chord(Key::J, CTRL)}, "completion.next");
    keymap.bind(VIM_COMPLETING, {chord(Key::K, CTRL)}, "completion.previous");
    return keymap;
}

} // namespace fjedit
