#include "fjedit/core/vim/keys_file.hpp"
#include "fjedit/core/vim/notation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace fjedit;
using namespace fjedit::vim;

namespace {

std::vector<Chord> keys(std::string_view notation) {
    return *parse_notation(notation);
}

MapStep keys_step(std::string_view notation) {
    return {.keys = keys(notation), .command = {}, .is_command = false};
}

MapStep command_step(std::string_view line) {
    return {.keys = {}, .command = std::string(line), .is_command = true};
}

} // namespace

TEST_CASE("keys.vim's maps read with their modes, their keys and what they do", "[vim][keys]") {
    const KeysFile file = read_keys_file(R"(" My maps
nnoremap <C-h> <C-w>h
xnoremap < <gv
nmap Q gq
inoremap jk <Esc>
noremap! <C-a> <Home>
map <F5> :w<CR>
:vn J :m '>+1<CR>gv=gv
)");
    CHECK(file.errors.empty());
    REQUIRE(file.maps.size() == 7);
    CHECK(file.maps[0].modes == MapModes::Normal);
    CHECK(file.maps[0].keys == keys("<C-h>"));
    CHECK(file.maps[0].does == std::vector<MapStep>{keys_step("<C-w>h")});
    CHECK_FALSE(file.maps[0].recursive);
    CHECK(file.maps[0].line == 2);
    CHECK(file.maps[1].modes == MapModes::Visual);
    CHECK(file.maps[1].keys == keys("<lt>"));
    CHECK(file.maps[2].recursive);
    CHECK(file.maps[3].modes == MapModes::Insert);
    CHECK(file.maps[4].modes == (MapModes::Insert | MapModes::Command));
    CHECK(file.maps[5].modes == (MapModes::Normal | MapModes::Visual | MapModes::OperatorPending));
    CHECK(file.maps[5].recursive);
    CHECK(file.maps[6].modes == MapModes::Visual);
    CHECK(file.maps[6].does == std::vector<MapStep>{keys_step(":m '>+1<CR>gv=gv")});
}

TEST_CASE("keys.vim's leader is Space until let mapleader says otherwise, read where each map is", "[vim][keys]") {
    const KeysFile file = read_keys_file(R"(nnoremap <leader>w <Cmd>w<CR>
let mapleader = ","
nnoremap <Leader>q <Cmd>q<CR>
let g:mapleader = "\<Space>"
nnoremap <leader>e ihi<Esc>
let maplocalleader = '\'
nnoremap <localleader>x x
)");
    CHECK(file.errors.empty());
    REQUIRE(file.maps.size() == 4);
    CHECK(file.maps[0].keys == keys("<Space>w"));
    CHECK(file.maps[0].does == std::vector<MapStep>{command_step("w")});
    CHECK(file.maps[1].keys == keys(",q"));
    CHECK(file.maps[2].keys == keys("<Space>e"));
    CHECK(file.maps[3].keys == keys("\\x"));
}

TEST_CASE("A map's right side: keys, <Cmd> lines between them, <Nop>, and its options", "[vim][keys]") {
    const KeysFile file = read_keys_file(R"(nnoremap <silent> <nowait> <leader>s <Cmd>w<CR>ihi<Esc><Cmd>noh<CR>
nnoremap <Up> <Nop>
nnoremap <silent> x "_x
)");
    CHECK(file.errors.empty());
    REQUIRE(file.maps.size() == 3);
    CHECK_FALSE(file.maps[0].waits);
    CHECK(file.maps[0].does ==
          std::vector<MapStep>{command_step("w"), keys_step("ihi<Esc>"), command_step("noh")});
    CHECK(file.maps[1].does.empty());
    CHECK(file.maps[2].waits);
    CHECK(file.maps[2].does == std::vector<MapStep>{keys_step("\"_x")});
}

TEST_CASE("keys.vim's bind and set lines are kept for the program, with their lines", "[vim][keys]") {
    const KeysFile file = read_keys_file(R"(set ignorecase smartcase
bind normal <leader>fg files.find
bind completing <C-l> completion.accept
)");
    CHECK(file.errors.empty());
    REQUIRE(file.settings.size() == 1);
    CHECK(file.settings[0].argument == "ignorecase smartcase");
    CHECK(file.settings[0].line == 1);
    REQUIRE(file.binds.size() == 2);
    CHECK(file.binds[0].mode == "normal");
    CHECK(file.binds[0].keys == keys("<Space>fg"));
    CHECK(file.binds[0].command == "files.find");
    CHECK(file.binds[1].line == 3);
}

TEST_CASE("A line keys.vim can't read is an error with its line, and the rest still read", "[vim][keys]") {
    const KeysFile file = read_keys_file("autocmd BufWrite * echo\r\n"
                                         "nnoremap <expr> j v:count ? 'j' : 'gj'\n"
                                         "nnoremap a b | echo 'c'\n"
                                         "nnoremap <leader>w <Cmd>w\n"
                                         "nnoremap x\n"
                                         "bind nowhere <C-x> files.find\n"
                                         "bind normal x\n"
                                         "let g:loaded = 1\n"
                                         "nmap! a b\n"
                                         "nnoremap ok ko\n");
    CHECK(file.errors == std::vector<KeysError>{
                             {.line = 1, .message = "Not supported here: autocmd"},
                             {.line = 2, .message = "Not supported yet: <expr>"},
                             {.line = 3, .message = "Write | as <Bar> in a map"},
                             {.line = 4, .message = "<Cmd> needs a <CR> to end its command"},
                             {.line = 5, .message = "A map is: the keys, then what they do"},
                             {.line = 6, .message = "No mode called nowhere to bind in"},
                             {.line = 7, .message = "A bind line is: bind <mode> <keys> <command>"},
                             {.line = 8, .message = "Not supported yet: let loaded"},
                             {.line = 9, .message = "Not supported here: nmap!"},
                         });
    REQUIRE(file.maps.size() == 1);
    CHECK(file.maps[0].keys == keys("ok"));
    CHECK(file.maps[0].line == 10);
}
