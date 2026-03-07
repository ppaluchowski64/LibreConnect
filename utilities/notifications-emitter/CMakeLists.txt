if (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE AND NOT ANDROID))


    if (WIN32)
        file(GLOB_RECURSE SOURCE_FILES
                src-windows/*.cpp
                src-windows/*.cxx
                src-windows/*.cc
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

    target_link_libraries(notifications-emitter PUBLIC
            Debug-Log
    )

    if (WIN32)
        target_link_libraries(notifications-emitter PUBLIC
            WinToast
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