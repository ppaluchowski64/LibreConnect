function(BuildStaticLibrary StaticLibraryName RootPath)
    file(GLOB_RECURSE SOURCE_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cpp
    )

    file(GLOB_RECURSE HEADER_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.hpp
    )

    add_library(${StaticLibraryName} STATIC ${SOURCE_FILES} ${HEADER_FILES})
    target_link_libraries(${StaticLibraryName} PUBLIC ${ARGN})
    target_include_directories(${StaticLibraryName} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/)
endfunction()


function(BuildTestProgram ExecutableName Path)
    add_executable(${ExecutableName} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${Path})
    target_link_libraries(${ExecutableName} PRIVATE ${ARGN})
endfunction()

function(BuildProgram ExecutableName RootPath)
    file(GLOB_RECURSE SOURCE_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cpp
    )

    file(GLOB_RECURSE HEADER_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.hpp
    )

    add_executable(${ExecutableName} ${SOURCE_FILES} ${HEADER_FILES})
    target_include_directories(${ExecutableName} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc)
    target_link_libraries(${ExecutableName} PRIVATE ${ARGN})
endfunction()

function(BuildQTProgram ExecutableName RootPath)
    file(GLOB_RECURSE SOURCE_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cpp
    )

    file(GLOB_RECURSE HEADER_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.hpp
    )

    qt_add_executable(${ExecutableName} ${SOURCE_FILES} ${HEADER_FILES})
    target_include_directories(${ExecutableName} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc)
    target_link_libraries(${ExecutableName} PRIVATE ${ARGN})

    if(WIN32)
        add_custom_command(TARGET ${ExecutableName} POST_BUILD
                COMMAND "$ENV{QT_DIR}/bin/windeployqt6.exe" --qmldir "$ENV{QT_DIR}/qml" "$<TARGET_FILE:${ExecutableName}>"
                COMMENT "Deploying Qt dependencies for ${ExecutableName}..."
                VERBATIM
        )
    elseif(APPLE)
        set_target_properties(${ExecutableName} PROPERTIES
                MACOSX_BUNDLE TRUE
        )

        add_custom_command(TARGET ${ExecutableName} POST_BUILD
                COMMAND "$ENV{QT_DIR}/bin/macdeployqt6" "$<TARGET_BUNDLE_DIR:${ExecutableName}>" -qmldir=$ENV{QT_DIR}/qml -dmg
                COMMENT "Running macdeployqt on ${ExecutableName}..."
                VERBATIM
        )
    elseif(UNIX)
        find_program(LINUXDEPLOYQT_EXECUTABLE linuxdeployqt)

        if(NOT LINUXDEPLOYQT_EXECUTABLE)
            message(FATAL_ERROR "linuxdeployqt not found. Please ensure it's in your PATH or set its path manually.")
        endif()

        add_custom_command(TARGET ${ExecutableName} POST_BUILD
                COMMAND ${LINUXDEPLOYQT_EXECUTABLE} "$<TARGET_FILE:${ExecutableName}>"
                -qmldir=$ENV{QT_DIR}/qml
                -qmake=$ENV{QT_DIR}/bin/qmake
                -exclude-libs=libqsqlmimer
                COMMENT "Deploying Qt dependencies for ${ExecutableName}..."
                VERBATIM
        )
    endif()
endfunction()
