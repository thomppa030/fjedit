#include "fjedit/core/commands.hpp"
#include "fjedit/core/keymap.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace fjedit;
using ftk::ui::Key;

static Chord ctrl(Key key) {
    return {.key = key, .modifiers = {.ctrl = true}};
}

TEST_CASE("Chords are written as menus show them", "[keymap]") {
    REQUIRE(chord_text({.key = Key::K, .modifiers = {.ctrl = true, .shift = true}}) == "Ctrl Shift K");
    REQUIRE(chord_text({.key = Key::Up, .modifiers = {.alt = true}}) == "Alt Up");
    REQUIRE(chord_text({.key = Key::RightBracket, .modifiers = {.ctrl = true}}) == "Ctrl ]");
    REQUIRE(chord_text({.key = Key::Escape}) == "Esc");
}

TEST_CASE("Every key's text reads back as the same chord", "[keymap]") {
    for (std::size_t i = 0; i < ftk::ui::KEY_COUNT; ++i) {
        const Chord chord{.key = static_cast<Key>(i), .modifiers = {.ctrl = true, .alt = true}};
        REQUIRE(parse_chord(chord_text(chord)) == chord);
    }
}

TEST_CASE("Chord text takes its modifiers in any order", "[keymap]") {
    REQUIRE(parse_chord("Shift Ctrl K") == Chord{.key = Key::K, .modifiers = {.ctrl = true, .shift = true}});
    REQUIRE(parse_chord("  Alt   Down ") == Chord{.key = Key::Down, .modifiers = {.alt = true}});
}

TEST_CASE("Chord text that names no key, or a modifier twice, is refused", "[keymap]") {
    REQUIRE_FALSE(parse_chord("").has_value());
    REQUIRE_FALSE(parse_chord("Ctrl").has_value());
    REQUIRE_FALSE(parse_chord("Ctrl Ctrl K").has_value());
    REQUIRE_FALSE(parse_chord("Ctrl Hyper K").has_value());
    REQUIRE_FALSE(parse_chord("K Ctrl").has_value());
}

TEST_CASE("A key finds its command", "[keymap]") {
    const Keymap keys = Keymap::standard();
    const std::vector<Chord> pressed{ctrl(Key::D)};
    REQUIRE(keys.find(Keymap::EDITING, pressed).command == "select.next_match");
    REQUIRE_FALSE(keys.find(Keymap::EDITING, std::vector<Chord>{ctrl(Key::Q)}).command.has_value());
    REQUIRE_FALSE(keys.find("other mode", pressed).command.has_value());
}

TEST_CASE("Every standard binding runs a command fjedit has", "[keymap]") {
    const Keymap keys = Keymap::standard();
    for (const std::string_view mode : {Keymap::EDITING, Keymap::FINDING}) {
        for (std::size_t key = 0; key < ftk::ui::KEY_COUNT; ++key) {
            for (unsigned held = 0; held < 16; ++held) {
                const Chord chord{.key = static_cast<Key>(key),
                                  .modifiers = {.ctrl = (held & 1) != 0, .shift = (held & 2) != 0,
                                                .alt = (held & 4) != 0, .super = (held & 8) != 0}};
                const KeyMatch match = keys.find(mode, std::vector<Chord>{chord});
                if (match.command) REQUIRE((find_command(*match.command) != nullptr || is_view_command(*match.command)));
            }
        }
    }
    for (const ViewCommand& command : view_commands()) {
        REQUIRE(find_command(command.name) == nullptr);
        REQUIRE_FALSE(command.title.empty());
    }
    for (const char* name : {"caret.left", "edit.undo", "edit.redo", "select.all", "cursor.single"}) {
        REQUIRE(find_command(name) != nullptr);
        REQUIRE(keys.keys_for(Keymap::EDITING, name).has_value());
    }
}

TEST_CASE("Keys that begin a longer binding wait for the next", "[keymap]") {
    Keymap keys;
    keys.bind(Keymap::EDITING, {ctrl(Key::K), ctrl(Key::C)}, "edit.comment");
    const std::vector<Chord> first{ctrl(Key::K)};
    REQUIRE(keys.find(Keymap::EDITING, first).waiting);
    REQUIRE_FALSE(keys.find(Keymap::EDITING, first).command.has_value());

    const std::vector<Chord> both{ctrl(Key::K), ctrl(Key::C)};
    REQUIRE(keys.find(Keymap::EDITING, both).command == "edit.comment");
    REQUIRE_FALSE(keys.find(Keymap::EDITING, both).waiting);

    const std::vector<Chord> wrong{ctrl(Key::K), ctrl(Key::X)};
    const KeyMatch none = keys.find(Keymap::EDITING, wrong);
    REQUIRE_FALSE(none.command.has_value());
    REQUIRE_FALSE(none.waiting);
}

TEST_CASE("A binding can be characters typed, which keys don't match", "[keymap]") {
    Keymap keys;
    keys.bind("vim", {Chord::typed("g"), Chord::typed("d")}, "definition.go");
    REQUIRE(keys.find("vim", std::vector<Chord>{Chord::typed("g")}).waiting);
    REQUIRE(keys.find("vim", std::vector<Chord>{Chord::typed("g"), Chord::typed("d")}).command == "definition.go");
    // The G key itself is another chord than the g it types.
    const Chord g_key{.key = Key::G};
    REQUIRE_FALSE(keys.find("vim", std::vector<Chord>{g_key}).waiting);
    REQUIRE(chord_text(Chord::typed("$")) == "$");
}

TEST_CASE("Modes keep their bindings apart", "[keymap]") {
    Keymap keys;
    const Chord d{.key = Key::D};
    keys.bind("normal", {d, d}, "edit.delete_lines");
    REQUIRE(keys.find("normal", std::vector<Chord>{d, d}).command == "edit.delete_lines");
    REQUIRE_FALSE(keys.find(Keymap::EDITING, std::vector<Chord>{d}).waiting);
}

TEST_CASE("Binding the same keys again replaces the binding, and unbinding takes it away", "[keymap]") {
    Keymap keys = Keymap::standard();
    keys.bind(Keymap::EDITING, {ctrl(Key::D)}, "edit.copy_lines_down");
    REQUIRE(keys.find(Keymap::EDITING, std::vector<Chord>{ctrl(Key::D)}).command == "edit.copy_lines_down");
    keys.unbind(Keymap::EDITING, std::vector<Chord>{ctrl(Key::D)});
    REQUIRE_FALSE(keys.find(Keymap::EDITING, std::vector<Chord>{ctrl(Key::D)}).command.has_value());
}
