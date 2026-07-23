add_executable(test_scroll_engine test_scroll_engine.c)
target_link_libraries(test_scroll_engine PRIVATE
    ${PROJECT_SOURCE_DIR}/src/scroll_engine.c
    m
    rt
)
target_include_directories(test_scroll_engine PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME scroll_engine COMMAND test_scroll_engine)
