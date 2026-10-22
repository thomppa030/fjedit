# fjedit, the program: a folder open in a window of its own, with the kit's
# fonts staged beside it.
add_executable(fjedit main.cpp)
target_link_libraries(fjedit PRIVATE fjedit::program ftk::app ftk::app-editor ftk::app-ui ftk::platform)
ftk_target_defaults(fjedit)
ftk_stage_editor_data(fjedit)
ftk_check_editor_ui(fjedit ROOTS .)
