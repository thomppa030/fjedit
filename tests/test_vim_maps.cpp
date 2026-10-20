#include "fjedit/core/vim/maps.hpp"
#include "fjedit/core/vim/notation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace fjedit;
using namespace fjedit::vim;

namespace {

// Maps from a keys.vim, and what they played: each key in Vim's notation,
// a <Cmd> line after a `:`, an error after a `!`; and the keys typed as the
// maps took them, each time they did.
struct Played {
    Maps maps;
    MapModes mode{MapModes::Normal};
    bool wants{false};
    std::vector<std::string> out;
    std::vector<std::string> told;
    // What a key played does to the mode, as the engine would.
    std::function<void(const Chord&)> on_key = [](const Chord&) {};

    explicit Played(std::string_view keys_vim) {
        const KeysFile file = read_keys_file(keys_vim);
        REQUIRE(file.errors.empty());
        maps.use(std::make_shared<const std::vector<Mapping>>(file.maps));
        maps.wait_for(1.0);
    }

    [[nodiscard]] MapPlayer player() {
        return {.mode = [this] { return mode; },
                .wants_character = [this] { return wants; },
                .key =
                    [this](const Chord& key) {
                        out.push_back(notation(key));
                        on_key(key);
                    },
                .command = [this](std::string_view line) { out.push_back(":" + std::string(line)); },
                .error = [this](std::string_view error) { out.push_back("!" + std::string(error)); },
                .typed =
                    [this](std::span<const Chord> keys) {
                        std::string written;
                        for (const Chord& key : keys) written += notation(key);
                        told.push_back(std::move(written));
                    }};
    }

    void type(std::string_view keys, double now = 0.0) {
        const MapPlayer to = player();
        const std::vector<Chord> typed = *parse_notation(keys);
        for (const Chord& key : typed) maps.take(key, now, to);
    }

    std::vector<std::string> taken() { return std::exchange(out, {}); }
};

using Out = std::vector<std::string>;

} // namespace

TEST_CASE("A map's keys play in its keys' place; keys no map makes play as typed", "[vim][maps]") {
    Played p("nnoremap x dd\nnmap a b\nnmap b c\nnnoremap q a\n");
    p.type("xz");
    CHECK(p.taken() == Out{"d", "d", "z"});
    // A map plays its keys through the maps again; a noremap doesn't.
    p.type("a");
    CHECK(p.taken() == Out{"c"});
    p.type("q");
    CHECK(p.taken() == Out{"a"});
}

TEST_CASE("A map that begins with its own keys doesn't map that key again; one that maps itself stops", "[vim][maps]") {
    Played p("nmap j jzz\nnmap a b\nnmap b a\n");
    p.type("j");
    CHECK(p.taken() == Out{"j", "z", "z"});
    p.type("a");
    CHECK(p.taken() == Out{"!E223: recursive mapping"});
}

TEST_CASE("Maps play in the mode each key is typed in, the mode changing on the way", "[vim][maps]") {
    Played p("inoremap jk <Esc>\nnmap x ihi<Esc>\nimap h H\n");
    p.type("jk");
    CHECK(p.taken() == Out{"j", "k"});
    p.mode = MapModes::Insert;
    p.type("jk");
    CHECK(p.taken() == Out{"<Esc>"});
    p.mode = MapModes::Normal;
    p.on_key = [&](const Chord& key) {
        if (key.text == "i") p.mode = MapModes::Insert;
        if (key.key == ftk::ui::Key::Escape) p.mode = MapModes::Normal;
    };
    p.type("x");
    CHECK(p.taken() == Out{"i", "H", "i", "<Esc>"});
    // A character a command waits for is no map's.
    p.on_key = [](const Chord&) {};
    p.wants = true;
    p.type("x");
    CHECK(p.taken() == Out{"x"});
}

TEST_CASE("Keys that begin a longer map wait for it, timeoutlen at most, then go as they stand", "[vim][maps]") {
    Played p("nnoremap <leader>f A\nnnoremap <leader>fg B\nnnoremap <nowait> <leader>q Q\nnnoremap <leader>qq W\n");
    p.type("<Space>", 0.0);
    CHECK(p.taken().empty());
    CHECK(p.maps.waiting() == *parse_notation("<Space>"));
    CHECK(p.maps.deadline() == std::optional<double>{1.0});
    CHECK(p.maps.longer(MapModes::Normal, *parse_notation("<Space>")).size() == 4);
    // Each key waits anew.
    p.type("f", 0.5);
    CHECK(p.taken().empty());
    CHECK(p.maps.deadline() == std::optional<double>{1.5});
    p.maps.time_out(p.player());
    CHECK(p.taken() == Out{"A"});
    CHECK_FALSE(p.maps.deadline());
    p.type("<Space>fg");
    CHECK(p.taken() == Out{"B"});
    // Making no map, they go as typed.
    p.type("<Space>");
    p.maps.time_out(p.player());
    CHECK(p.taken() == Out{"<Space>"});
    p.type("<Space>z");
    CHECK(p.taken() == Out{"<Space>", "z"});
    // <nowait> runs at once.
    p.type("<Space>q");
    CHECK(p.taken() == Out{"Q"});
}

TEST_CASE("A map's <Cmd> lines run between its keys, in order", "[vim][maps]") {
    Played p("nnoremap <leader>s <Cmd>w<CR>ihi<Esc><Cmd>noh<CR>\n");
    p.type("<Space>s");
    CHECK(p.taken() == Out{":w", "i", "h", "i", "<Esc>", ":noh"});
}

TEST_CASE("The keys typed are told as the maps take them, a map's together; a macro plays ahead through the maps",
          "[vim][maps][macros]") {
    Played p("nnoremap <leader>s ihi<Esc>\nnmap a b\n");
    p.type("x<Space>sa");
    CHECK(p.told == Out{"x", "<Space>s", "a"});
    p.told.clear();
    (void)p.taken();
    // Played when @ is: ahead of the z typed after it, through the maps,
    // and none of it told as typed.
    p.on_key = [&](const Chord& key) {
        if (key.text == "@") p.maps.play_ahead(*parse_notation("a<Space>s"), 0.0, p.player());
    };
    p.type("@z");
    CHECK(p.taken() == Out{"@", "b", "i", "h", "i", "<Esc>", "z"});
    CHECK(p.told == Out{"@", "z"});
    // A command failing lets go of what the macro had yet to play, not of
    // what was typed.
    p.on_key = [&](const Chord& key) {
        if (key.text == "@") p.maps.play_ahead(*parse_notation("xyz"), 0.0, p.player());
        if (key.text == "y") p.maps.drop_played();
    };
    p.type("@w");
    CHECK(p.taken() == Out{"@", "x", "y", "w"});
}

TEST_CASE("A frame plays so many of a macro's keys, the rest in the frames after", "[vim][maps][macros]") {
    Played p("");
    p.on_key = [&](const Chord& key) {
        if (key.text == "@") p.maps.play_ahead(std::vector<Chord>(5000, Chord::typed("x")), 0.0, p.player());
    };
    p.type("@");
    CHECK(p.taken().size() == 2000);
    REQUIRE(p.maps.more());
    p.maps.play_on(0.0, p.player());
    CHECK(p.taken().size() == 2000);
    p.maps.play_on(0.0, p.player());
    CHECK(p.taken().size() == 1001);
    CHECK_FALSE(p.maps.more());
    // Let go, the rest plays nowhere.
    p.type("@");
    p.maps.drop_played();
    CHECK_FALSE(p.maps.more());
}
