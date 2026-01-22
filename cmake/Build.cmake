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

function(BuildSharedLibrary SharedLibraryName RootPath)
    file(GLOB_RECURSE SOURCE_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cxx
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cc
    )

    file(GLOB_RECURSE HEADER_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.hpp
    )

    add_library(${SharedLibraryName} SHARED
            ${SOURCE_FILES}
            ${HEADER_FILES}
    )

    target_link_libraries(${SharedLibraryName} PUBLIC ${ARGN})
    target_include_directories(${SharedLibraryName} PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/
    )
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

function(BuildQTExecutable ExecutableName RootPath ModuleURI)
    file(GLOB_RECURSE SOURCE_FILES
            ${RootPath}/src/*.cpp
            ${RootPath}/src/*.cxx
            ${RootPath}/src/*.cc
    )

    file(GLOB_RECURSE HEADER_FILES
            ${RootPath}/inc/*.h
            ${RootPath}/inc/*.hpp
    )

    FILE(GLOB_RECURSE QML_FILES
            RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
            ${RootPath}/qml/*.qml
    )

    file(GLOB_RECURSE RESOURCES
            RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
            ${RootPath}/res/*.png
            ${RootPath}/res/*.jpg
            ${RootPath}/res/*.jpeg
            ${RootPath}/res/*.gif
            ${RootPath}/res/*.svg
            ${RootPath}/res/*.ico
            ${RootPath}/res/*.webp
    )

    qt_add_executable(${ExecutableName}
            ${SOURCE_FILES}
            ${HEADER_FILES}
    )

    foreach(FILE ${QML_FILES} ${RESOURCES})
        string(REGEX REPLACE "^[^/]+/" "" REL_PATH "${FILE}")
        set_source_files_properties("${FILE}" PROPERTIES
                QT_RESOURCE_ALIAS "${REL_PATH}"
        )
    endforeach()

    qt_add_qml_module(${ExecutableName}
            URI ${ModuleURI}
            VERSION 1.0
            QML_FILES
                ${QML_FILES}
            RESOURCES
                ${RESOURCES}
            SOURCES
                ${SOURCE_FILES}
                ${HEADER_FILES}
    )

    target_include_directories(${ExecutableName} PUBLIC ${RootPath}/inc)
    target_link_libraries(${ExecutableName} PRIVATE ${ARGN})

    set(DEPLOY_FOLDER ${CMAKE_BINARY_DIR}/deploy/$<CONFIG>/${ExecutableName})

    set_target_properties(${ExecutableName} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${DEPLOY_FOLDER}"
    )

    if(APPLE)
        set_target_properties(${ExecutableName} PROPERTIES
                BUNDLE_OUTPUT_DIRECTORY "${DEPLOY_FOLDER}"
        )
    endif()

    DeployQT(${ExecutableName})
endfunction()

function(DeployQT Target)
    if(WIN32)
        add_custom_command(TARGET ${Target} POST_BUILD
                COMMAND "$ENV{QT_DIR_DESKTOP}/bin/windeployqt6.exe" --qmldir "$ENV{QT_DIR_DESKTOP}/qml" "$<TARGET_FILE:${Target}>"
                COMMENT "Deploying Qt dependencies for ${Target}..."
                VERBATIM
        )
    elseif(APPLE AND NOT IOS)
        set_target_properties(${ExecutableName} PROPERTIES
                MACOSX_BUNDLE TRUE
        )

        add_custom_command(TARGET ${Target} POST_BUILD
                COMMAND "$ENV{QT_DIR_DESKTOP}/bin/macdeployqt6" "$<TARGET_BUNDLE_DIR:${Target}>" -qmldir=$ENV{QT_DIR_DESKTOP}/qml -dmg
                COMMENT "Running macdeployqt on ${Target}..."
                VERBATIM
        )
    elseif(ANDROID)
    elseif(IOS)
    elseif(UNIX)
        find_program(LINUXDEPLOY_EXECUTABLE linuxdeploy)
        find_program(PLUGIN_QT_EXECUTABLE linuxdeploy-plugin-qt)

        if(NOT LINUXDEPLOY_EXECUTABLE)
            message(FATAL_ERROR "linuxdeploy not found. Please ensure it's in your PATH or set its path manually.")
        endif()

        if(NOT PLUGIN_QT_EXECUTABLE)
            message(FATAL_ERROR "linuxdeploy-plugin-qt not found. Please ensure it's in your PATH or set its path manually.")
        endif()

        add_custom_command(TARGET ${Target} POST_BUILD
                COMMAND env
                QMAKE=$ENV{QT_DIR_DESKTOP}/bin/qmake6
                ${LINUXDEPLOY_EXECUTABLE}
                --appdir ${CMAKE_BINARY_DIR}/AppDir
                --executable "$<TARGET_FILE:${Target}>"
                --plugin qt
                COMMENT "Deploying Qt dependencies for ${Target}..."
                VERBATIM
        )
    endif()
endfunction()

function(LinkVirtualCameraLibs target)
    if (WIN32)
        target_link_libraries(${target} PUBLIC virtual-camera-platform-implementation)
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:virtual-camera-platform-implementation>
                $<TARGET_FILE_DIR:${target}>
        )
    endif ()
endfunction()