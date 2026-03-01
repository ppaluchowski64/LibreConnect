if (ANDROID)
#    file(GLOB_RECURSE KOTLIN_SOURCES kotlin/*)
#    list(APPEND KOTLIN_SOURCE_FILES ${KOTLIN_SOURCES})
#    list(APPEND ANDROID_EXTRA_SOURCE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/kotlin)

    file(GLOB_RECURSE SOURCE_FILES
            src/*.cpp
            src/*.cxx
            src/*.cc
    )

    file(GLOB_RECURSE HEADER_FILES
            inc/*.h
            inc/*.hpp
    )

    add_library(notifications-android STATIC
            ${SOURCE_FILES}
            ${HEADER_FILES}
    )

    target_link_libraries(notifications-android PUBLIC
            Debug-Log
            system-component-pc
            Qt6::Core
    )

    target_include_directories(notifications-android PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/inc/
    )


endif ()