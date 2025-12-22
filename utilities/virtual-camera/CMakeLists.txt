file(GLOB_RECURSE COMMON_SOURCE_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc
)

file(GLOB_RECURSE COMMON_HEADER_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/*.hpp
)

if (WIN32)
    find_package(wil REQUIRED)

    file(GLOB_RECURSE SOURCE_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/src-win-dll/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src-win-dll/*.cxx
            ${CMAKE_CURRENT_SOURCE_DIR}/src-win-dll/*.cc
    )

    file(GLOB_RECURSE HEADER_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/inc-win-dll/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/inc-win-dll/*.hpp
    )

    add_library(virtual-camera-windows-dll SHARED
            ${SOURCE_FILES}
            ${HEADER_FILES}
    )

    target_link_libraries(virtual-camera-windows-dll PRIVATE WIL::WIL
            windowsapp
            Mfplat.lib
            Mf.lib
            Mfuuid.lib
            Uuid.lib
            Ole32.lib
            Kernel32.lib
            Mfsensorgroup.lib
    )
    target_include_directories(virtual-camera-windows-dll PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc-win-dll/priv)
    target_include_directories(virtual-camera-windows-dll PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc-win-dll/pub)
endif()


add_library(virtual-camera STATIC
        ${COMMON_SOURCE_FILES}
        ${COMMON_HEADER_FILES}
)

target_include_directories(virtual-camera PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/
)

target_link_libraries(virtual-camera PRIVATE Debug-Log)