add_library(remote-input-types-internal INTERFACE)
target_include_directories(remote-input-types-internal INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/common/
)

if (ANDROID)
    target_include_directories(remote-input-types-internal INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/inc/mobile/
    )
else ()
    target_include_directories(remote-input-types-internal INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/inc/desktop/
    )
endif ()

add_library(remote-input-types ALIAS remote-input-types-internal)

set(SOURCE_FILES "")
set(GLOBAL_SOURCE_FILES "")

file(GLOB_RECURSE COMMON_SOURCE_FILES
        src/common/*.cpp
        src/common/*.cxx
        src/common/*.cc
)

if (ANDROID)
    file(GLOB_RECURSE SOURCE_FILES
            src/mobile/android/*.cpp
            src/mobile/android/*.cxx
            src/mobile/android/*.cc
    )
elseif (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE))
    file(GLOB_RECURSE GLOBAL_SOURCE_FILES
            src/desktop/global/*.cpp
            src/desktop/global/*.cxx
            src/desktop/global/*.cc
    )

    if (UNIX AND NOT APPLE)
        file(GLOB_RECURSE SOURCE_FILES
                src/desktop/linux/*.cpp
                src/desktop/linux/*.cxx
                src/desktop/linux/*.cc
        )
        find_package(ALSA REQUIRED)
    elseif (APPLE AND NOT IOS)
        file(GLOB_RECURSE SOURCE_FILES
                src/desktop/macos/*.mm
                src/desktop/macos/*.m
                src/desktop/macos/*.cpp
                src/desktop/macos/*.cxx
                src/desktop/macos/*.cc
        )
    elseif (WIN32)
        file(GLOB_RECURSE SOURCE_FILES
                src/desktop/windows/*.cpp
                src/desktop/windows/*.cxx
                src/desktop/windows/*.cc
        )
    endif ()
endif ()

file(GLOB_RECURSE HEADER_FILES
        inc/*.h
        inc/*.hpp
)

add_library(remote-input STATIC
        ${COMMON_SOURCE_FILES}
        ${SOURCE_FILES}
        ${GLOBAL_SOURCE_FILES}
        ${HEADER_FILES}
)

target_include_directories(remote-input PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/common/
)

if (ANDROID)
    target_include_directories(remote-input PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/inc/mobile/
    )
else ()
    target_include_directories(remote-input PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/inc/desktop/
    )
endif ()

target_link_libraries(remote-input PUBLIC
        project_defaults
        remote-input-types
        asio::asio
        Debug-Log
        concurrentqueue::concurrentqueue
)

if (ANDROID)
    target_link_libraries(remote-input PUBLIC
            Qt6::Core
            android-context
    )
else ()
    target_link_libraries(remote-input PUBLIC
            network-component-pc
            system-component-pc
            clipboard
    )

    if (UNIX AND NOT APPLE)
        target_link_libraries(remote-input PUBLIC
                ALSA::ALSA
                Qt6::DBus
        )
    elseif (APPLE AND NOT IOS)
        target_link_libraries(remote-input PUBLIC
                "-framework Foundation"
                "-framework ApplicationServices"
                "-framework Carbon"
                "-framework AppKit"
                "-framework CoreAudio"
                "-framework AudioToolbox"
                "-framework MediaPlayer"
        )
    elseif (WIN32)
        target_link_libraries(remote-input PUBLIC
                ole32
                windowsapp
        )
    endif ()
endif ()
