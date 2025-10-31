function(BuildStaticLibrary StaticLibraryName RootPath)
    file(GLOB_RECURSE SOURCE_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cxx
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cc
    )

    file(GLOB_RECURSE HEADER_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.hpp
    )

    add_library(${StaticLibraryName} STATIC ${SOURCE_FILES} ${HEADER_FILES})
    target_link_libraries(${StaticLibraryName} PUBLIC ${ARGN})
    target_include_directories(${StaticLibraryName} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/)
endfunction()

function(BuildQTModule Target RootPath)
    file(GLOB_RECURSE SOURCE_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cxx
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cc
    )

    file(GLOB_RECURSE HEADER_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.hpp
    )

    file(GLOB_RECURSE QML_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/qml/*.qml
    )

    qt_add_executable(${Target}
            ${SOURCE_FILES}
            ${HEADER_FILES}
    )

    qt_add_resources(${Target} ${ETarget}
            PREFIX "/${Target}"
            BASE ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/qml
            FILES
                ${QML_FILES}
    )

    target_include_directories(${Target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc)
    target_link_libraries(${Target} PRIVATE ${ARGN})
endfunction()


function(BuildTestProgram ExecutableName Path)
    add_executable(${ExecutableName} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${Path})
    target_link_libraries(${ExecutableName} PRIVATE ${ARGN})
endfunction()

function(BuildProgram ExecutableName RootPath)
    file(GLOB_RECURSE SOURCE_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cxx
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cc
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
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cxx
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cc
    )

    file(GLOB_RECURSE HEADER_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.hpp
    )

    file(GLOB_RECURSE QML_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/qml/*.qml
    )

    file(GLOB_RECURSE RESOURCE_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/res/*.png
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/res/*.jpg
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/res/*.jpeg
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/res/*.svg
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/res/*.ttf
    )

    qt_add_executable(${ExecutableName}
        ${SOURCE_FILES}
        ${HEADER_FILES}
    )

    qt_add_resources(${ExecutableName} ${ExecutableName}_qml_resources
            PREFIX "/"
            BASE ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/qml
            FILES
                ${QML_FILES}
    )

    qt_add_resources(${ExecutableName} ${ExecutableName}_app_resources
            PREFIX "/"
            BASE ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/res
            FILES
                ${RESOURCE_FILES}
    )

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
