cmake_minimum_required(VERSION 4.3)

project(fjedit
    VERSION 0.1.0
    DESCRIPTION "A code editor on ftk"
    LANGUAGES C CXX)

option(FJEDIT_BUILD_TESTS "Build fjedit's unit tests" ${PROJECT_IS_TOP_LEVEL})

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# ftk: a program that builds fjedit beside its own ftk (Fjell) has the
# targets already, and fjedit takes those, so one build holds one ftk.
# Otherwise it is fetched. While working on both, point the fetch at a
# checkout with -DFETCHCONTENT_SOURCE_DIR_FTK=<path>.
if(NOT TARGET ftk::base)
    include(FetchContent)
    FetchContent_Declare(ftk
        GIT_REPOSITORY https://github.com/thomppa030/ftk.git
        GIT_TAG        v0.1.0)
    FetchContent_MakeAvailable(ftk)
endif()

add_subdirectory(src)

if(FJEDIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
