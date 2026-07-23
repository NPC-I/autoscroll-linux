add_executable(autoscroll
    main.c
    config.c
    scroll_engine.c
    evdev_backend.c
)

target_link_libraries(autoscroll PRIVATE rt m)

install(TARGETS autoscroll RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_SOURCE_DIR}/service/autoscroll.service DESTINATION lib/systemd/system)
install(FILES ${CMAKE_SOURCE_DIR}/udev/99-autoscroll.rules DESTINATION lib/udev/rules.d)
install(FILES ${CMAKE_SOURCE_DIR}/man/autoscroll.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
