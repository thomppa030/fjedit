cmake_minimum_required(VERSION 4.3)

project(fjedit
    VERSION 0.1.0
    DESCRIPTION "A code editor on ftk"
    LANGUAGES C CXX)

option(FJEDIT_BUILD_TESTS "Build fjedit's unit tests" ${PROJECT_IS_TOP_LEVEL})
# A program that takes fjedit's libraries, as fjell does, needs no fjedit
# program of its own.
option(FJEDIT_BUILD_PROGRAM "Build the fjedit program" ${PROJECT_IS_TOP_LEVEL})

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    # fjedit uses no C++ modules. Scanning for them puts GCC's module flags
    # into compile_commands.json, and clangd, which reads it, rejects them.
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
endif()

# ftk: a program that builds fjedit beside its own ftk (fjell) has the
# targets already, and fjedit takes those, so one build holds one ftk.
# Otherwise it is fetched at the commit fjedit builds with: ftk grows along
# with fjedit, so a change that needs a newer ftk moves the hash instead of
# waiting for a release. While working on both, point the fetch at a
# checkout with -DFETCHCONTENT_SOURCE_DIR_FTK=<path>.
if(NOT TARGET ftk::base)
    include(FetchContent)
    FetchContent_Declare(ftk
        GIT_REPOSITORY https://github.com/thomppa030/ftk.git
        GIT_TAG        eb289de413b9d71a2553bf0ae99102dfe7074d61)
    FetchContent_MakeAvailable(ftk)
endif()

add_subdirectory(src)

if(FJEDIT_BUILD_PROGRAM)
    add_subdirectory(app)
endif()

if(FJEDIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
