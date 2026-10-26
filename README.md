<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="brand/fjedit-lockup-dark.svg">
    <img src="brand/fjedit-lockup-light.svg" alt="fjedit" width="360">
  </picture>
</p>

A code editor built on [ftk](https://github.com/thomppa030/ftk). It edits C++
with help from clangd, and the Fjell engine's own files: shaders, stylesheets,
widgets. It comes as a library, which is the editor and which Fjell builds in,
and a program that opens a folder in it.

It is early. What exists today is the text the editor works on and the view
that draws and edits it; a program that opens one comes next.

## Building

You need what ftk needs: CMake 4.3 or newer, Ninja, a C++23 compiler (GCC 13
or MSVC 2022) and the Vulkan SDK. Everything else is fetched at configure.

    cmake --preset debug
    cmake --build --preset debug
    ctest --preset debug

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

A program that already builds ftk itself (Fjell does) can add fjedit beside it:
fjedit then uses that program's ftk targets instead of fetching its own.
