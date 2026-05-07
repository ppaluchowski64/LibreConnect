include(../../cmake/Build.cmake)

if (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT ANDROID))
    add_library(virtual-microphone STATIC ${SOURCE_FILES} ${HEADER_FILES}  ${CMAKE_CURRENT_SOURCE_DIR}/global-inc/VMicTypes.h)
    target_include_directories(virtual-microphone PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common-inc)
    target_link_libraries(virtual-microphone PUBLIC Debug-Log magic_enum::magic_enum boost::boost)

    if (WIN32)
        file(GLOB_RECURSE SOURCE_FILES
                ${CMAKE_CURRENT_SOURCE_DIR}/windows/src/*.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/windows/src/*.cxx
                ${CMAKE_CURRENT_SOURCE_DIR}/windows/src/*.cc
                ${CMAKE_CURRENT_SOURCE_DIR}/windows/src/*.def
        )

        target_sources(virtual-microphone PUBLIC ${SOURCE_FILES})
        target_include_directories(virtual-microphone PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/windows/inc/)
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
    add_library(virtual-microphone STATIC)
endif ()

target_include_directories(virtual-microphone PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/global-inc/)



