if (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE))


    if (WIN32)
        file(GLOB_RECURSE SOURCE_FILES
                src-windows/*.cpp
                src-windows/*.cxx
                src-windows/*.cc
        )
    endif ()

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
    endif ()

    target_include_directories(notifications-emitter PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/inc/
    )

endif ()