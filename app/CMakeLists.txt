# fjedit, the program: a folder open in a window of its own, with the kit's
# fonts and its own brand files staged beside it.
add_executable(fjedit main.cpp)
target_link_libraries(fjedit PRIVATE fjedit::program ftk::app ftk::app-editor ftk::app-ui ftk::platform)
ftk_target_defaults(fjedit)
ftk_stage_editor_data(fjedit)

# The brand files the program reads, in brand/ beside it: its tile, the icon
# its windows carry. Copied with every build, changing nothing when nothing
# changed.
set(brand "${CMAKE_CURRENT_SOURCE_DIR}/../brand")
add_custom_target(fjedit-brand
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:fjedit>/brand"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${brand}/fjedit-tile.svg" "$<TARGET_FILE_DIR:fjedit>/brand"
    VERBATIM)
add_dependencies(fjedit fjedit-brand)
# On Windows the .exe carries the tile as its icon.
ftk_app_icon(fjedit "${brand}/fjedit-tile.svg")
ftk_check_editor_ui(fjedit ROOTS .)
