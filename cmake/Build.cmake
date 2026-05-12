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

function(BuildApplicationModule ModuleName CommonPath RootPath)
    file(GLOB_RECURSE SOURCE_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cxx
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cc

            ${CMAKE_CURRENT_SOURCE_DIR}/${CommonPath}/src/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/${CommonPath}/src/*.cxx
            ${CMAKE_CURRENT_SOURCE_DIR}/${CommonPath}/src/*.cc
    )

    file(GLOB_RECURSE HEADER_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.hpp

            ${CMAKE_CURRENT_SOURCE_DIR}/${CommonPath}/inc/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/${CommonPath}/inc/*.hpp
    )

    add_library(${ModuleName} STATIC ${SOURCE_FILES} ${HEADER_FILES})
    target_link_libraries(${ModuleName} PUBLIC ${ARGN})

    target_include_directories(${ModuleName} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/)
    target_include_directories(${ModuleName} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${CommonPath}/inc/)
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

    DeployQT(${ExecutableName})
endfunction()

function(BuildQTExecutable ExecutableName RootPath ModuleURI)
    file(GLOB_RECURSE SOURCE_FILES
            ${RootPath}/src/*.cpp
            ${RootPath}/src/*.cxx
            ${RootPath}/src/*.cc
    )

    set(EXTRA_SOURCES_VAR "EXTRA_SOURCES_${ExecutableName}")
    if (DEFINED ${EXTRA_SOURCES_VAR})
        list(APPEND SOURCE_FILES ${${EXTRA_SOURCES_VAR}})
    endif()

    file(GLOB_RECURSE HEADER_FILES
            ${RootPath}/inc/*.h
            ${RootPath}/inc/*.hpp
    )

    file(GLOB_RECURSE QML_FILES
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
            ${RootPath}/res/*.ttf
            ${RootPath}/res/*.otf
    )

    set(EXCLUDED_SOURCES_VAR "EXCLUDED_SOURCES_${ExecutableName}")
    if (DEFINED ${EXCLUDED_SOURCES_VAR})
        foreach(EXCLUDED_SOURCE ${${EXCLUDED_SOURCES_VAR}})
            list(REMOVE_ITEM SOURCE_FILES "${EXCLUDED_SOURCE}")
            list(REMOVE_ITEM HEADER_FILES "${EXCLUDED_SOURCE}")
        endforeach()
    endif()

    set(EXCLUDED_QML_VAR "EXCLUDED_QML_${ExecutableName}")
    if (DEFINED ${EXCLUDED_QML_VAR})
        foreach(EXCLUDED_QML_FILE ${${EXCLUDED_QML_VAR}})
            list(REMOVE_ITEM QML_FILES "${EXCLUDED_QML_FILE}")
        endforeach()
    endif()

    set(EXCLUDED_RESOURCES_VAR "EXCLUDED_RESOURCES_${ExecutableName}")
    if (DEFINED ${EXCLUDED_RESOURCES_VAR})
        foreach(EXCLUDED_RESOURCE ${${EXCLUDED_RESOURCES_VAR}})
            list(REMOVE_ITEM RESOURCES "${EXCLUDED_RESOURCE}")
        endforeach()
    endif()

    if (WIN32)
        qt_add_executable(${ExecutableName}
                ${SOURCE_FILES}
                ${HEADER_FILES}
        )
        target_link_libraries(${ExecutableName} PRIVATE Qt6::EntryPointPrivate)
    else()
        qt_add_executable(${ExecutableName}
                ${SOURCE_FILES}
                ${HEADER_FILES}
        )
    endif()

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
    if(CMAKE_BUILD_TYPE)
        set(DEPLOY_FOLDER ${CMAKE_BINARY_DIR}/deploy/${CMAKE_BUILD_TYPE}/${ExecutableName})
    else()
        set(DEPLOY_FOLDER ${CMAKE_BINARY_DIR}/deploy/${ExecutableName})
    endif()

    set_target_properties(${ExecutableName} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${DEPLOY_FOLDER}"
    )

    if(APPLE)
        set_target_properties(${ExecutableName} PROPERTIES
                BUNDLE_OUTPUT_DIRECTORY "${DEPLOY_FOLDER}"
        )
    endif()

    if (ANDROID)
        set_property(TARGET ${ExecutableName} PROPERTY
                QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android"
        )

        set_target_properties(${ExecutableName} PROPERTIES
                OUTPUT_NAME "LibreConnectNative"
        )
    endif()

    DeployQT(${ExecutableName})
endfunction()

function(DeployQT Target)
    set(WIN_ICON "${CMAKE_SOURCE_DIR}/apps/desktop/res/libreconnect_logo.ico")
    set(MAC_ICON "${CMAKE_SOURCE_DIR}/apps/desktop/res/libreconnect_logo.icns")

    if(WIN32)
        set(RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
        file(WRITE "${RC_FILE}" "IDI_ICON1 ICON DISCARDABLE \"${WIN_ICON}\"")
        target_sources(${Target} PRIVATE "${RC_FILE}")

        if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
            set_target_properties(${Target} PROPERTIES
                    LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
            )
        endif()

        add_custom_command(TARGET ${Target} POST_BUILD
                COMMAND "$ENV{QT_DIR_DESKTOP}/bin/windeployqt6.exe" --qmldir "$ENV{QT_DIR_DESKTOP}/qml" "$<TARGET_FILE:${Target}>"
                COMMENT "Deploying Qt dependencies for ${Target}..."
                VERBATIM
        )
    elseif(APPLE AND NOT IOS)
        set_source_files_properties(${MAC_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(${Target} PRIVATE ${MAC_ICON})

        set_target_properties(${Target} PROPERTIES
                MACOSX_BUNDLE TRUE
                MACOSX_BUNDLE_ICON_FILE "libreconnect_logo.icns"
        )

        add_custom_command(TARGET ${Target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:${Target}>/Contents/Resources"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MAC_ICON}" "$<TARGET_BUNDLE_DIR:${Target}>/Contents/Resources/libreconnect_logo.icns"
                COMMENT "Copying macOS app icon for ${Target}..."
                VERBATIM
        )

        add_custom_command(TARGET ${Target} POST_BUILD
                COMMAND "$ENV{QT_DIR_DESKTOP}/bin/macdeployqt6" "$<TARGET_BUNDLE_DIR:${Target}>" -qmldir=$ENV{QT_DIR_DESKTOP}/qml -dmg
                COMMENT "Running macdeployqt on ${Target}..."
                VERBATIM
        )

        if (Target STREQUAL "appLibreConnect_desktop")
            add_custom_command(TARGET ${Target} POST_BUILD
                    COMMAND /usr/bin/codesign --force --deep --sign - --identifier com.libreconnect.desktop "$<TARGET_BUNDLE_DIR:${Target}>"
                    COMMENT "Re-signing ${Target} with the app bundle identifier..."
                    VERBATIM
            )
        endif()
    elseif(ANDROID)

    elseif(UNIX)
        find_program(LINUXDEPLOY_EXECUTABLE linuxdeploy)
        find_program(PLUGIN_QT_EXECUTABLE linuxdeploy-plugin-qt)

        if(NOT LINUXDEPLOY_EXECUTABLE)
            message(FATAL_ERROR "linuxdeploy not found. Please ensure it's in your PATH or set its path manually.")
        endif()

        if(NOT PLUGIN_QT_EXECUTABLE)
            message(FATAL_ERROR "linuxdeploy-plugin-qt not found. Please ensure it's in your PATH or set its path manually.")
        endif()

        get_target_property(DEPLOY_DIR ${Target} RUNTIME_OUTPUT_DIRECTORY)

        set(CUSTOM_LD_LIB_PATHS
                "${CMAKE_SOURCE_DIR}/build/ffmpeg/lib"
        )
        set(DESKTOP_FILE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${Target}.desktop")
        set(ICON_FILE_PATH "${CMAKE_SOURCE_DIR}/apps/desktop/res/libreconnect_logo.png")
        set(DESKTOP_ENTRY_NAME "${Target}")
        set(DESKTOP_ENTRY_COMMENT "${Target}")

        if(Target STREQUAL "appLibreConnect_desktop")
            set(DESKTOP_ENTRY_NAME "LibreConnect")
            set(DESKTOP_ENTRY_COMMENT "LibreConnect Desktop")
        endif()

        # On Linux the desktop entry controls whether the GUI app requests a terminal.
        file(WRITE "${DESKTOP_FILE_PATH}"
                "[Desktop Entry]\n"
                "Type=Application\n"
                "Name=${DESKTOP_ENTRY_NAME}\n"
                "Comment=${DESKTOP_ENTRY_COMMENT}\n"
                "Exec=${Target}\n"
                "Icon=libreconnect_logo\n"
                "Terminal=false\n"
                "Categories=Network;Utility;\n"
        )

        set_target_properties(${Target} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY ${DEPLOY_DIR}/usr/bin
        )

        add_custom_command(TARGET ${Target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E env
                QMAKE=$ENV{QT_DIR_DESKTOP}/bin/qmake6
                LD_LIBRARY_PATH=${CUSTOM_LD_LIB_PATHS}
                QML_SOURCES_PATHS=${CMAKE_CURRENT_SOURCE_DIR}
                sh -c "\"${LINUXDEPLOY_EXECUTABLE}\" --appdir \"${DEPLOY_DIR}\" --executable \"$<TARGET_FILE:${Target}>\" --desktop-file \"${DESKTOP_FILE_PATH}\" --icon-file \"${ICON_FILE_PATH}\" --plugin qt || true"
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

function(LinkFFMPEGLibs target)
    if (WIN32)
        file(GLOB FFMPEG_LIBS ${CMAKE_SOURCE_DIR}/build/ffmpeg/lib/*.lib)

        if(NOT FFMPEG_LIBS)
            message(WARNING "No FFmpeg .lib files found in ${CMAKE_SOURCE_DIR}/build/ffmpeg/lib.")
        else()
            target_link_libraries(${target} PUBLIC ${FFMPEG_LIBS})
        endif()

        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/build/ffmpeg/bin
                $<TARGET_FILE_DIR:${target}>
        )
    elseif(UNIX AND NOT ANDROID AND NOT IOS)
        set(FFMPEG_LIB_NAMES avcodec avdevice avfilter avformat avutil postproc swresample swscale)

        foreach(LIB ${FFMPEG_LIB_NAMES})
            find_library(LIB_PATH_${LIB}
                    NAMES ${LIB}
                    PATHS "${CMAKE_SOURCE_DIR}/build/ffmpeg/lib"
                    NO_DEFAULT_PATH)

            if(LIB_PATH_${LIB})
                target_link_libraries(${target} PUBLIC ${LIB_PATH_${LIB}})
            else()
                message(FATAL_ERROR "Could not find FFmpeg library: ${LIB}")
            endif()
        endforeach()
    endif ()
endfunction()
