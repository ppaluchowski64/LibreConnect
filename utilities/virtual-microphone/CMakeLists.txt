include(../../cmake/Build.cmake)

if (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT ANDROID))
    add_library(virtual-microphone STATIC ${SOURCE_FILES} ${HEADER_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/global-inc/VMicTypes.h)
    target_include_directories(virtual-microphone PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common-inc)
    target_link_libraries(virtual-microphone PUBLIC Debug-Log magic_enum::magic_enum boost::boost)
    target_include_directories(virtual-microphone PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/global-inc/)

    if (WIN32)
        file(GLOB_RECURSE SOURCE_FILES
                ${CMAKE_CURRENT_SOURCE_DIR}/windows/src/*.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/windows/src/*.cxx
                ${CMAKE_CURRENT_SOURCE_DIR}/windows/src/*.cc
                ${CMAKE_CURRENT_SOURCE_DIR}/windows/src/*.def
        )

        target_sources(virtual-microphone PRIVATE ${SOURCE_FILES})
        target_include_directories(virtual-microphone PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/windows/inc/)
    elseif (APPLE)

    elseif (UNIX)
        set(PKG_CONFIG_EXECUTABLE "/usr/bin/pkg-config")
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)

        file(GLOB_RECURSE SOURCE_FILES
                ${CMAKE_CURRENT_SOURCE_DIR}/linux/src/*.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/linux/src/*.cxx
                ${CMAKE_CURRENT_SOURCE_DIR}/linux/src/*.cc
                ${CMAKE_CURRENT_SOURCE_DIR}/linux/src/*.def
        )

        target_sources(virtual-microphone PRIVATE ${SOURCE_FILES})
        target_include_directories(virtual-microphone PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/linux/inc/)

        target_include_directories(virtual-microphone PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
        target_link_libraries(virtual-microphone PRIVATE ${PIPEWIRE_LIBRARIES})
        target_compile_options(virtual-microphone PRIVATE ${PIPEWIRE_CFLAGS_OTHER})
        target_link_directories(virtual-microphone PRIVATE ${PIPEWIRE_LIBRARY_DIRS})
    endif ()

#    elseif (APPLE AND NOT IOS)
#        add_subdirectory(macos)
#    elseif(UNIX)
#        add_subdirectory(linux)
#    endif()

#    file(GLOB_RECURSE SOURCE_FILES
#            ${CMAKE_CURRENT_SOURCE_DIR}/main/src/*.cpp
#            ${CMAKE_CURRENT_SOURCE_DIR}/main/src/*.cxx
#            ${CMAKE_CURRENT_SOURCE_DIR}/main/src/*.cc
#            ${CMAKE_CURRENT_SOURCE_DIR}/main/src/*.def
#    )
#
#    file(GLOB_RECURSE HEADER_FILES
#            ${CMAKE_CURRENT_SOURCE_DIR}/main/inc/*.h
#            ${CMAKE_CURRENT_SOURCE_DIR}/main/inc/*.hpp
#    )
#
else ()
    add_library(virtual-microphone STATIC
            linux/src/VMicAPI.cpp)

    target_include_directories(virtual-microphone PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/global-inc/)
endif ()




