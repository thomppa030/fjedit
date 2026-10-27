#include "fjedit/editor/preferences.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace fjedit;

TEST_CASE("The preferences are kept in the settings file and read back held to their range", "[editor][preferences]") {
    Preferences kept{.text_size = 17,
                     .minimap = false,
                     .line_numbers = LineNumbers::Relative,
                     .around_caret = 20,
                     .vim = true,
                     .clangd = "/opt/llvm/bin/clangd",
                     .fjls = "/home/user/fjell/build/fjls"};
    const ftk::ui::SettingsSection section = preferences_section(kept);
    const std::vector<ftk::ui::SettingsEntry> written = section.write();
    REQUIRE(written.size() == 1);

    Preferences read;
    ftk::ui::SettingsSection reading = preferences_section(read);
    for (const std::string& line : written.front().lines) reading.read(written.front().name, line);
    REQUIRE(read.text_size == 17);
    REQUIRE_FALSE(read.minimap);
    REQUIRE(read.line_numbers == LineNumbers::Relative);
    REQUIRE(read.around_caret == 20);
    REQUIRE(read.vim);
    REQUIRE(read.clangd == "/opt/llvm/bin/clangd");
    REQUIRE(read.fjls == "/home/user/fjell/build/fjls");

    reading.read(written.front().name, "TextSize=400");
    REQUIRE(read.text_size == TEXT_SIZE_MAX);
    reading.read(written.front().name, "AroundCaret=500");
    REQUIRE(read.around_caret == AROUND_CARET_MAX);
    // A way of numbering it doesn't know leaves the one it has.
    reading.read(written.front().name, "LineNumbers=Sideways");
    REQUIRE(read.line_numbers == LineNumbers::Relative);
    // An empty path leaves the server where it was looked for.
    reading.read(written.front().name, "Clangd=");
    REQUIRE(read.clangd == "/opt/llvm/bin/clangd");
    // Another entry's lines are not its.
    reading.read("Other", "TextSize=12");
    REQUIRE(read.text_size == TEXT_SIZE_MAX);
}
