if (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE AND NOT ANDROID))


    if (WIN32)
        file(GLOB_RECURSE SOURCE_FILES
                src-windows/*.cpp
                src-windows/*.cxx
                src-windows/*.cc
        )
    elseif (APPLE AND NOT IOS)
        file(GLOB_RECURSE SOURCE_FILES
                src-macos/*.mm
                src-macos/*.m
                src-macos/*.cpp
                src-macos/*.cxx
                src-macos/*.cc
        )
    elseif (UNIX AND NOT APPLE)
        file(GLOB_RECURSE SOURCE_FILES
                src-linux/*.cpp
                src-linux/*.cxx
                src-linux/*.cc
        )
    endif ()

    message("Sources: ${SOURCE_FILES}")

    file(GLOB_RECURSE HEADER_FILES
            inc/*.h
            inc/*.hpp
    )

    add_library(notifications-emitter STATIC
            ${SOURCE_FILES}
            ${HEADER_FILES}
    )

    if (APPLE AND NOT IOS)
        set_source_files_properties(src-macos/NotificationEmitter.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    endif ()

    target_link_libraries(notifications-emitter PUBLIC
            Debug-Log
            network-component-pc
    )

    if (WIN32)
        target_link_libraries(notifications-emitter PUBLIC
                WinToast
                magic_enum::magic_enum
        )
    elseif (APPLE AND NOT IOS)
        target_link_libraries(notifications-emitter PUBLIC
                "-framework Foundation"
                "-framework UserNotifications"
        )
    elseif (UNIX AND NOT APPLE)
        target_link_libraries(notifications-emitter PUBLIC
            Qt6::DBus
        )
    endif ()

    target_include_directories(notifications-emitter PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/inc/
    )

endif ()
