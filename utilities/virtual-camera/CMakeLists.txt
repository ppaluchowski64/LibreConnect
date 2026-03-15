include(../../cmake/Build.cmake)

if (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT ANDROID))
    if (WIN32)
        add_subdirectory(windows)
    elseif (APPLE AND NOT IOS)
        add_subdirectory(macos)
    elseif(UNIX)
        add_subdirectory(linux)
    endif()

    file(GLOB_RECURSE SOURCE_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/main/src/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/main/src/*.cxx
            ${CMAKE_CURRENT_SOURCE_DIR}/main/src/*.cc
            ${CMAKE_CURRENT_SOURCE_DIR}/main/src/*.def
    )

    file(GLOB_RECURSE HEADER_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/main/inc/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/main/inc/*.hpp
    )

    add_library(virtual-camera STATIC ${SOURCE_FILES} ${HEADER_FILES})
    target_include_directories(virtual-camera PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/main/inc)
    target_link_libraries(virtual-camera PUBLIC Debug-Log magic_enum::magic_enum virtual-camera-platform-implementation boost::boost)
else ()

    add_library(virtual-camera STATIC ${CMAKE_CURRENT_SOURCE_DIR}/global-inc/VCamTypes.h)
    target_include_directories(virtual-camera PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/global-inc/)

endif ()
