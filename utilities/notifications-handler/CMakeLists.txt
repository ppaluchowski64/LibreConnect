if (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE AND NOT ANDROID))
    if (WIN32)
        file(GLOB_RECURSE SOURCE_FILES
                src-windows/*.cpp
                src-windows/*.cxx
                src-windows/*.cc
                src-global/*.cpp
                src-global/*.cxx
                src-global/*.cc
        )
    elseif (APPLE AND NOT IOS)
        file(GLOB_RECURSE SOURCE_FILES
                src-macos/*.mm
                src-macos/*.m
                src-macos/*.cpp
                src-macos/*.cxx
                src-macos/*.cc
                src-global/*.cpp
                src-global/*.cxx
                src-global/*.cc
        )
    elseif (UNIX AND NOT APPLE)
        file(GLOB_RECURSE SOURCE_FILES
                src-linux/*.cpp
                src-linux/*.cxx
                src-linux/*.cc
                src-global/*.cpp
                src-global/*.cxx
                src-global/*.cc
        )
    endif ()

    file(GLOB_RECURSE HEADER_FILES
            inc-desktop/*.h
            inc-desktop/*.hpp
            inc-global/*.h
            inc-global/*.hpp
    )

    add_library(notifications-handler STATIC
            ${SOURCE_FILES}
            ${HEADER_FILES}
    )

    if (APPLE AND NOT IOS)
        set_source_files_properties(src-macos/NotificationEmitter.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    endif ()

    target_link_libraries(notifications-handler PUBLIC
            network-component-pc
    )

    if (WIN32)
        target_link_libraries(notifications-handler PUBLIC
                WinToast
                magic_enum::magic_enum
        )
    elseif (APPLE AND NOT IOS)
        target_link_libraries(notifications-handler PUBLIC
                "-framework Foundation"
                "-framework UserNotifications"
        )
    elseif (UNIX AND NOT APPLE)
        target_link_libraries(notifications-handler PUBLIC
                Qt6::DBus
                Qt6::Core
                Qt6::Gui
        )
    endif ()

    target_include_directories(notifications-handler PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/inc-desktop/
    )

endif ()

if (ANDROID)
    file(GLOB_RECURSE SOURCE_FILES
            src-android/*.cpp
            src-android/*.cxx
            src-android/*.cc
            src-global/*.cpp
            src-global/*.cxx
            src-global/*.cc
    )

    file(GLOB_RECURSE HEADER_FILES
            inc-android/*.h
            inc-android/*.hpp
            inc-global/*.h
            inc-global/*.hpp
    )

    add_library(notifications-handler STATIC
            ${SOURCE_FILES}
            ${HEADER_FILES}
    )

    target_link_libraries(notifications-handler PUBLIC
            Qt6::Core
            android-context
    )

    target_include_directories(notifications-handler PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/inc-android/
    )

endif ()

if (NOT IOS)
    target_link_libraries(notifications-handler PUBLIC
            Debug-Log
            system-component-pc
            network-component-pc
    )

    target_include_directories(notifications-handler PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/inc-global/
    )
endif ()
