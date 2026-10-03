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

std::string chord_text(Chord chord) {
    std::string text;
    if (chord.modifiers.ctrl) text += "Ctrl ";
    if (chord.modifiers.shift) text += "Shift ";
    if (chord.modifiers.alt) text += "Alt ";
    if (chord.modifiers.super) text += "Super ";
    text += KEY_NAMES[static_cast<std::size_t>(chord.key)];
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
    if (match.command) match.waiting = false;
    return match;
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
        {chord(Key::Z, CTRL), "edit.undo"},
        {chord(Key::Y, CTRL), "edit.redo"},
        {chord(Key::Z, CTRL, SHIFT), "edit.redo"},
    };

    Keymap keymap;
    for (const Default& binding : defaults) keymap.bind(EDITING, {binding.keys}, std::string(binding.command));
    return keymap;
}

} // namespace fjedit
