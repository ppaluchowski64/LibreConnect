if (ANDROID)
    file(GLOB_RECURSE KOTLIN_SOURCES kotlin/*)
    list(APPEND KOTLIN_SOURCE_FILES ${KOTLIN_SOURCES})

    file(GLOB_RECURSE SOURCE_FILES
            src/*.cpp
            src/*.cxx
            src/*.cc
    )

    file(GLOB_RECURSE HEADER_FILES
            inc/*.h
            inc/*.hpp
    )

    add_library(notifications-capture STATIC
            ${SOURCE_FILES}
            ${HEADER_FILES}
    )

    target_link_libraries(notifications-capture PUBLIC
            Debug-Log
            system-component-pc
    )

    target_include_directories(notifications-capture PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/inc/
    )


endif ()