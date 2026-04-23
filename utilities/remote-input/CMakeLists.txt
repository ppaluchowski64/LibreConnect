if (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE AND NOT ANDROID))

    if (UNIX AND NOT APPLE)
        file(GLOB_RECURSE SOURCE_FILES
                src/linux/*.cpp
                src/linux/*.cxx
                src/linux/*.cc
        )

        find_package(ALSA REQUIRED)

    elseif (APPLE AND NOT IOS)
        file(GLOB_RECURSE SOURCE_FILES
                src/macos/*.mm
                src/macos/*.m
                src/macos/*.cpp
                src/macos/*.cxx
                src/macos/*.cc
        )
    elseif (WIN32)
        file(GLOB_RECURSE SOURCE_FILES
                src/windows/*.cpp
                src/windows/*.cxx
                src/windows/*.cc
        )
    endif ()

    file(GLOB_RECURSE GLOBAL_SOURCE_FILES
            src/global/*.cpp
            src/global/*.cxx
            src/global/*.cc
    )

    file(GLOB_RECURSE HEADER_FILES
            inc/*.h
            inc/*.hpp
    )

    add_library(remote-input STATIC
            ${SOURCE_FILES}
            ${GLOBAL_SOURCE_FILES}
            ${HEADER_FILES}
    )

    target_include_directories(remote-input PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/inc/
    )

    target_link_libraries(remote-input PUBLIC
            project_defaults
            remote-input-types
            asio::asio
            Debug-Log
            network-component-pc
            system-component-pc
            concurrentqueue::concurrentqueue
    )

    if (UNIX AND NOT APPLE)
        target_link_libraries(remote-input PUBLIC
                ALSA::ALSA
        )
    endif ()

    if (APPLE AND NOT IOS)
        target_link_libraries(remote-input PUBLIC
                "-framework ApplicationServices"
                "-framework Carbon"
                "-framework AppKit"
                "-framework CoreAudio"
                "-framework AudioToolbox"
        )
    endif ()

    if (WIN32)
        target_link_libraries(remote-input PUBLIC
                ole32
                windowsapp
        )
    endif ()

endif ()
