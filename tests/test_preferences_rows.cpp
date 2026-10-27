#include "fjedit/editor/preferences_rows.hpp"

#include "temp_folder.hpp"

#include "ftk/test/kit_harness.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>

using namespace fjedit;
using ftk::ui::core::Role;

namespace {

// The files of a folder of the test's own, with no language services; with
// `own`, the fjls the program brings.
struct Files {
    TempFolder folder;
    Preferences preferences;
    CodeFiles files;

    explicit Files(std::string own = {})
        : files(folder.path, preferences,
                {.services = std::vector<ServiceMaker>{}, .fjls = std::move(own)}) {}
};

} // namespace

TEST_CASE("clangd is shown reading the first compile commands found where it looks", "[editor][preferences]") {
    Files f;
    CHECK(clangd_shown(f.files).reads.empty());
    f.folder.write("build/compile_commands.json", "[]");
    CHECK(clangd_shown(f.files).reads == "build/compile_commands.json");
    // A program that knows where its folder keeps them looks there.
    f.folder.write("scripts/compile_commands.json", "[]");
    constexpr std::array<std::string_view, 1> SCRIPTS{"scripts/compile_commands.json"};
    CHECK(clangd_shown(f.files, SCRIPTS).reads == "scripts/compile_commands.json");
}

TEST_CASE("fjls is shown as the program's own when the program brings it", "[editor][preferences]") {
    CHECK(fjls_shown(Files().files).own.empty());
    Files own("/opt/fjell/fjls");
    const LanguageServerShown shown = fjls_shown(own.files, "Fjell's own");
    CHECK(shown.own == "Fjell's own");
    CHECK(shown.own_path == "/opt/fjell/fjls");
}

TEST_CASE("A program's own fjls has its row with nothing to edit, clangd its path", "[editor][preferences]") {
    ftk::test::KitHarness h;
    Preferences preferences;
    const LanguageServerShown fjls{.own = "Fjell's own", .own_path = "/opt/fjell/fjls"};
    h.set_ui([&] { draw_server_rows(preferences, {}, fjls); });
    h.step(2);
    // One field, clangd's; fjls's row only says what it is.
    std::vector<std::string> fields;
    for (const auto& node : h.ui().semantics()) {
        if (node.role == Role::TextField) fields.emplace_back(node.value);
    }
    CHECK(fields == std::vector<std::string>{preferences.clangd});
}
