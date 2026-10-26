<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="brand/fjedit-lockup-dark.svg">
    <img src="brand/fjedit-lockup-light.svg" alt="fjedit" width="360">
  </picture>
</p>

A code editor built on [ftk](https://github.com/thomppa030/ftk). It edits C++
with help from clangd, and the fjell engine's own files (shaders, stylesheets,
widgets, a game's script headers) with help from fjls, fjell's language server.
It comes as libraries, which are the editor and which fjell is to build in, and
a program that opens a folder in it.

The program shows the folder's files down the side and the files open in tabs,
in groups side by side if you split them. It finds in files, goes to a
definition, renames and formats through the language servers, takes Vim's keys
when Preferences turn them on, and keeps unsaved work against a crash.

## Building

You need what ftk needs: CMake 4.3 or newer, Ninja, a C++23 compiler (GCC 13
or MSVC 2022) and the Vulkan SDK. Everything else is fetched at configure.

    cmake --preset debug
    cmake --build --preset debug
    ctest --preset debug

## Running

    build/app/fjedit [folder]

opens the folder, or the one it was started in. clangd and fjls are run from
the PATH unless Preferences give them another path; each starts with the first
file it answers for.

## Working on ftk at the same time

fjedit fetches ftk at a commit. To build against a checkout instead, keep a
`CMakeUserPresets.json` (git ignores it) whose preset sets
`FETCHCONTENT_SOURCE_DIR_FTK` to the checkout:

```json
{
    "version": 6,
    "configurePresets": [
        { "name": "dev", "inherits": "debug",
          "cacheVariables": { "FETCHCONTENT_SOURCE_DIR_FTK": "$env{HOME}/Fjell/ftk" } }
    ],
    "buildPresets": [ { "name": "dev", "configurePreset": "dev" } ],
    "testPresets": [ { "name": "dev", "configurePreset": "dev" } ]
}
```

## Libraries

| Library | What it holds |
|---|---|
| `fjedit::core` | The text and how it is edited, without a window |
| `fjedit::widget` | The code view, drawn with ftk's UI core in the theme's colours |
| `fjedit::lsp` | A language server run as a child process and talked to through ftk's JSON-RPC: what clangd and fjls run as |
| `fjedit::program` | The program on a folder: its file tree, tabs and groups, find in files, Preferences and status bar |

A program that already builds ftk itself (Fjell does) can add fjedit beside it:
fjedit then uses that program's ftk targets instead of fetching its own.
