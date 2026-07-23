find_package(PkgConfig QUIET)
pkg_check_modules(GTK3 gtk+-3.0)
pkg_check_modules(GTK_LAYER_SHELL gtk-layer-shell-0)

# Build indicator only if both deps are found
if(GTK3_FOUND AND GTK_LAYER_SHELL_FOUND)
    add_executable(autoscroll-indicator autoscroll-indicator.c)
    target_include_directories(autoscroll-indicator PRIVATE
        ${GTK3_INCLUDE_DIRS}
        ${GTK_LAYER_SHELL_INCLUDE_DIRS}
    )
    target_compile_options(autoscroll-indicator PRIVATE
        ${GTK3_CFLAGS_OTHER}
        ${GTK_LAYER_SHELL_CFLAGS_OTHER}
    )
    target_link_libraries(autoscroll-indicator PRIVATE
        ${GTK3_LIBRARIES}
        ${GTK_LAYER_SHELL_LIBRARIES}
        m
    )
    set_target_properties(autoscroll-indicator PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
    )

    install(TARGETS autoscroll-indicator RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES ${CMAKE_SOURCE_DIR}/service/autoscroll-indicator.service
            DESTINATION lib/systemd/user)
else()
    message(STATUS "autoscroll-indicator: skipped (missing gtk+-3.0 or gtk-layer-shell-0)")
endif()
