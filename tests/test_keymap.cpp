#include "fjedit/core/commands.hpp"
#include "fjedit/core/keymap.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string_view>
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
    for (const std::string_view mode : {Keymap::EDITING, Keymap::FINDING, Keymap::COMPLETING, Keymap::VIM_NORMAL,
                                        Keymap::VIM_INSERT, Keymap::VIM_VISUAL, Keymap::VIM_COMPLETING}) {
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

TEST_CASE("Chords one after another are written apart, characters typed together", "[keymap]") {
    CHECK(chords_text(std::vector<Chord>{ctrl(Key::K), ctrl(Key::C)}) == "Ctrl K Ctrl C");
    CHECK(chords_text(std::vector<Chord>{Chord::typed("\""), Chord::typed("+"), Chord::typed("y"), Chord::typed("y")}) ==
          "\"+yy");
    CHECK(chords_text(std::vector<Chord>{ctrl(Key::O), Chord::typed("u")}) == "Ctrl O u");
    CHECK(chords_text(std::vector<Chord>{ctrl(Key::R), Chord::typed("+")}) == "Ctrl R +");
    CHECK(chords_text(std::vector<Chord>{}).empty());
}

TEST_CASE("Vim's Ctrl-N and Ctrl-P open the completion list in insert mode, then step through it", "[keymap]") {
    const Keymap keys = Keymap::standard();
    CHECK(keys.find(Keymap::VIM_INSERT, std::vector<Chord>{ctrl(Key::N)}).command == "completion.open");
    CHECK(keys.find(Keymap::VIM_INSERT, std::vector<Chord>{ctrl(Key::P)}).command == "completion.open");
    CHECK(keys.find(Keymap::VIM_COMPLETING, std::vector<Chord>{ctrl(Key::N)}).command == "completion.next");
    CHECK(keys.find(Keymap::VIM_COMPLETING, std::vector<Chord>{ctrl(Key::P)}).command == "completion.previous");
    // Without Vim, and in its normal mode, they are no binding's.
    CHECK_FALSE(keys.find(Keymap::EDITING, std::vector<Chord>{ctrl(Key::P)}).command);
    CHECK_FALSE(keys.find(Keymap::VIM_NORMAL, std::vector<Chord>{ctrl(Key::N)}).command);
}

TEST_CASE("Ctrl Shift C and V copy and paste as a terminal's, with Vim on and off", "[keymap]") {
    const Keymap keys = Keymap::standard();
    const std::vector<Chord> copy{{.key = Key::C, .modifiers = {.ctrl = true, .shift = true}}};
    const std::vector<Chord> paste{{.key = Key::V, .modifiers = {.ctrl = true, .shift = true}}};
    for (const std::string_view mode : {Keymap::EDITING, Keymap::VIM_NORMAL, Keymap::VIM_VISUAL}) {
        CHECK(keys.find(mode, copy).command == "edit.copy");
    }
    for (const std::string_view mode : {Keymap::EDITING, Keymap::VIM_NORMAL, Keymap::VIM_VISUAL, Keymap::VIM_INSERT}) {
        CHECK(keys.find(mode, paste).command == "edit.paste");
    }
    // Insert mode has nothing selected to copy.
    CHECK_FALSE(keys.find(Keymap::VIM_INSERT, copy).command);
    // Without Vim the menus go on showing Ctrl C and V.
    CHECK(keys.keys_for(Keymap::EDITING, "edit.copy") == std::vector<Chord>{ctrl(Key::C)});
    CHECK(keys.keys_for(Keymap::EDITING, "edit.paste") == std::vector<Chord>{ctrl(Key::V)});
}

TEST_CASE("Vim's normal mode has Neovim's keys for what a language server answers", "[keymap]") {
    const Keymap keys = Keymap::standard();
    const auto typed = [](std::string_view text) {
        std::vector<Chord> out;
        for (const char c : text) out.push_back(Chord::typed(std::string_view(&c, 1)));
        return out;
    };
    CHECK(keys.find(Keymap::VIM_NORMAL, typed("gd")).command == "definition.go");
    CHECK(keys.find(Keymap::VIM_NORMAL, std::vector<Chord>{ctrl(Key::RightBracket)}).command == "definition.go");
    CHECK(keys.find(Keymap::VIM_NORMAL, typed("gr")).waiting);
    CHECK(keys.find(Keymap::VIM_NORMAL, typed("grn")).command == "symbol.rename");
    CHECK(keys.find(Keymap::VIM_NORMAL, typed("K")).command == "hover.show");
    // Vim's g keys go on to Vim.
    CHECK_FALSE(keys.find(Keymap::VIM_NORMAL, typed("gg")).command);
    CHECK_FALSE(keys.find(Keymap::VIM_NORMAL, typed("gg")).waiting);
    // A menu shows these before F12 and F2.
    CHECK(keys.keys_for(Keymap::VIM_NORMAL, "definition.go") == typed("gd"));
    CHECK(keys.keys_for(Keymap::VIM_NORMAL, "symbol.rename") == typed("grn"));
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

TEST_CASE("The keys that can come next say what they run, and what longer bindings go on past them", "[keymap]") {
    Keymap keys;
    const auto typed = [](std::string_view text) {
        std::vector<Chord> out;
        for (const char c : text) out.push_back(Chord::typed(std::string_view(&c, 1)));
        return out;
    };
    keys.bind(Keymap::VIM_NORMAL, typed("gd"), "definition.go");
    keys.bind(Keymap::VIM_NORMAL, typed("grn"), "symbol.rename");
    keys.bind(Keymap::VIM_NORMAL, typed("gr"), "find.open");
    keys.bind(Keymap::VIM_VISUAL, typed("gx"), "edit.copy");
    const std::vector<KeyNext> next = keys.next_keys(Keymap::VIM_NORMAL, typed("g"));
    REQUIRE(next.size() == 2);
    CHECK(next[0].key == Chord::typed("d"));
    CHECK(next[0].command == std::optional<std::string_view>{"definition.go"});
    CHECK(next[0].longer.empty());
    CHECK(next[1].key == Chord::typed("r"));
    CHECK(next[1].command == std::optional<std::string_view>{"find.open"});
    CHECK(next[1].longer == std::vector<std::string_view>{"symbol.rename"});
    // A whole binding a longer one begins with says so.
    const KeyMatch gr = keys.find(Keymap::VIM_NORMAL, typed("gr"));
    CHECK(gr.command == std::optional<std::string_view>{"find.open"});
    CHECK(gr.longer);
    CHECK_FALSE(gr.waiting);
    CHECK(keys.next_keys(Keymap::VIM_NORMAL, typed("gd")).empty());
}
