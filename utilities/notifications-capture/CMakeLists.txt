if (NOT IOS)
    file(GLOB_RECURSE SOURCE_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/common/src/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/common/src/*.cxx
            ${CMAKE_CURRENT_SOURCE_DIR}/common/src/*.cc
    )

    file(GLOB_RECURSE HEADER_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/common/inc/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/common/inc/*.hpp
    )

    add_library(notifications-capture STATIC
            ${SOURCE_FILES}
            ${HEADER_FILES}
    )

    target_include_directories(notifications-capture PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/common/inc/
    )

    if (ANDROID)
        file(GLOB_RECURSE KOTLIN_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/android/kotlin/*)
        list(APPEND KOTLIN_SOURCE_FILES ${KOTLIN_SOURCES})

        file(GLOB_RECURSE SOURCE_FILES
                ${CMAKE_CURRENT_SOURCE_DIR}/android/src/*.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/android/src/*.cxx
                ${CMAKE_CURRENT_SOURCE_DIR}/android/src/*.cc
        )

        file(GLOB_RECURSE HEADER_FILES
                ${CMAKE_CURRENT_SOURCE_DIR}/android/inc/*.h
                ${CMAKE_CURRENT_SOURCE_DIR}/android/inc/*.hpp
        )

        target_sources(notifications-capture PUBLIC
                ${SOURCE_FILES}
                ${HEADER_FILES}
        )

        target_include_directories(notifications-capture PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/android/inc/
        )

    elseif (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE))
        file(GLOB_RECURSE SOURCE_FILES
                ${CMAKE_CURRENT_SOURCE_DIR}/desktop/src/*.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/desktop/src/*.cxx
                ${CMAKE_CURRENT_SOURCE_DIR}/desktop/src/*.cc
        )

        file(GLOB_RECURSE HEADER_FILES
                ${CMAKE_CURRENT_SOURCE_DIR}/desktop/inc/*.h
                ${CMAKE_CURRENT_SOURCE_DIR}/desktop/inc/*.hpp
        )

        target_sources(notifications-capture PUBLIC
                ${SOURCE_FILES}
                ${HEADER_FILES}
        )

        target_include_directories(notifications-capture PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/desktop/inc/
        )

    endif ()
endif ()