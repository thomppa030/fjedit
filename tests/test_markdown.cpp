#include "fjedit/widget/markdown.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace fjedit;
using Kind = MarkdownBlock::Kind;

TEST_CASE("clangd's hover reads as a heading, rules, a paragraph and fenced code", "[markdown]") {
    const std::vector<MarkdownBlock> blocks = read_markdown("### field `spin_speed`  \n\n---\nType: `float`  \n\n---\n"
                                                            "```cpp\n// In CoinCollectible\npublic: float spin_speed = 90.0f\n```");
    REQUIRE(blocks.size() == 5);
    REQUIRE(blocks[0] == MarkdownBlock{.kind = Kind::Heading,
                                       .runs = {{.text = "field "}, {.text = "spin_speed", .code = true}}});
    REQUIRE(blocks[1].kind == Kind::Rule);
    REQUIRE(blocks[2] == MarkdownBlock{.kind = Kind::Paragraph, .runs = {{.text = "Type: "}, {.text = "float", .code = true}}});
    REQUIRE(blocks[3].kind == Kind::Rule);
    REQUIRE(blocks[4].kind == Kind::Code);
    REQUIRE(blocks[4].language == "cpp");
    REQUIRE(blocks[4].lines == std::vector<std::string>{"// In CoinCollectible", "public: float spin_speed = 90.0f"});
}

TEST_CASE("A paragraph's lines join, and lists, strong text, links and escapes read as Markdown has them", "[markdown]") {
    const std::vector<MarkdownBlock> blocks =
        read_markdown("Rotates a quaternion\nfrom an axis.\n\n- `angle` in **radians**\n2. see [the docs](https://x)\n\\*not strong\\*");
    REQUIRE(blocks.size() == 4);
    REQUIRE(blocks[0] == MarkdownBlock{.kind = Kind::Paragraph, .runs = {{.text = "Rotates a quaternion from an axis."}}});
    REQUIRE(blocks[1] == MarkdownBlock{.kind = Kind::Item,
                                       .runs = {{.text = "angle", .code = true},
                                                {.text = " in "},
                                                {.text = "radians", .strong = true}}});
    REQUIRE(blocks[2] == MarkdownBlock{.kind = Kind::Item, .runs = {{.text = "see the docs"}}});
    REQUIRE(blocks[3] == MarkdownBlock{.kind = Kind::Paragraph, .runs = {{.text = "*not strong*"}}});
}

TEST_CASE("An unclosed fence holds the rest as code, and a lone backtick is itself", "[markdown]") {
    const std::vector<MarkdownBlock> blocks = read_markdown("a ` b\n```\nint x;");
    REQUIRE(blocks.size() == 2);
    REQUIRE(blocks[0].runs == std::vector<MarkdownRun>{{.text = "a ` b"}});
    REQUIRE(blocks[1].lines == std::vector<std::string>{"int x;"});
}
