#pragma once

// A code area on a folder of the test's own, alone in a window as a tab of
// fjell's holds it: no program of its own around it, one command of the
// program it is in ("game.play", on F5), and find in files floating over the
// whole window. Its work is kept in .recovery and keys.vim read from
// .config, in the folder, where no test looks for its files.
//
//     Area a({{"a.cpp", "int a;\n"}});
//     a.code->open(a.folder.path / "a.cpp");
//     a.h.step(2);

#include "fjedit/editor/code_area.hpp"

#include "temp_folder.hpp"

#include "ftk/test/kit_harness.hpp"
#include "ftk/ui/kit/viewport_toolbar.hpp"
#include "ftk/ui/kit/window.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct Area {
    TempFolder folder;
    fjedit::Preferences preferences;
    std::unique_ptr<fjedit::CodeFiles> files;
    std::unique_ptr<fjedit::CodeArea> code;
    ftk::test::KitHarness h{{1280.0f, 800.0f}};
    // What the area asked of the program it is in.
    std::vector<std::string> ran;
    int left{0};
    // The results find in files opened.
    int found_opened{0};

    /// The folder holding `written`, `before` run on it before the area is
    /// made; its language services `services`, none when not given.
    explicit Area(std::initializer_list<std::pair<const char*, const char*>> written,
                  const std::function<void(const TempFolder&)>& before = {},
                  std::vector<fjedit::ServiceMaker> services = {}) {
        for (const auto& [name, bytes] : written) folder.write(name, bytes);
        if (before) before(folder);
        files = std::make_unique<fjedit::CodeFiles>(
            folder.path, preferences,
            fjedit::CodeFilesOptions{.recovery = folder.path / ".recovery", .services = std::move(services)});
        code = std::make_unique<fjedit::CodeArea>(
            *files, preferences,
            fjedit::CodeAreaOptions{
                .keys = folder.path / ".config" / "keys.vim",
                .commands = {{.name = "game.play", .title = "Play", .key = fjedit::Chord{.key = ftk::ui::Key::F5}}},
                .run = [this](std::string_view command) {
                    ran.emplace_back(command);
                    return true;
                },
                .leave = [this] { ++left; },
                .program_name = "fjell"});
        h.set_ui([this] {
            code->take_keys();
            ftk::ui::next_window_place({0.0f, 0.0f}, {1280.0f, 800.0f});
            (void)ftk::ui::begin_host_window("##code", {.scrollbar = false, .to_front = false});
            code->draw();
            if (code->draw_find_in_files({.min = {0.0f, 0.0f}, .max = {1280.0f, 800.0f}})) ++found_opened;
            ftk::ui::end_host_window();
            code->draw_prompts();
        });
        h.step(3);
    }

    [[nodiscard]] std::filesystem::path at(std::string_view name) const {
        return files->workspace().name(folder.path / name);
    }

    // The file `name` opened in front, the keyboard in it.
    void open(std::string_view name) {
        code->open(folder.path / name);
        h.step(2);
        REQUIRE(code->front() == at(name));
    }

    // Vim's `:` line, typed and entered.
    void ex(std::string_view line) {
        h.type(std::format(":{}", line));
        h.press(ftk::ui::Key::Enter);
        h.step(2);
    }
};
