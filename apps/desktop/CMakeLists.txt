if (APPLE AND NOT IOS)
    file(GLOB_RECURSE MACOS_SOURCE_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm
            ${CMAKE_CURRENT_SOURCE_DIR}/src/*.m
    )
    set(EXTRA_SOURCES_appLibreConnect_desktop
            ${MACOS_SOURCE_FILES}
    )
    set(EXCLUDED_SOURCES_appLibreConnect_desktop
            ${CMAKE_CURRENT_SOURCE_DIR}/src/VirtualCameraController.cpp
    )
    set(EXCLUDED_QML_appLibreConnect_desktop
            qml/VirtualCameraPage.qml
    )
endif()

BuildQTExecutable(appLibreConnect_desktop
        ${CMAKE_CURRENT_SOURCE_DIR}
        LibreConnect.desktop
        network-component-pc
        system-component-pc
        p2p-network-component-pc
        Qt6::Quick
        Qt6::Qml
        Qt6::QuickControls2
        Qt6::Multimedia
        modules-manager
)

if (APPLE AND NOT IOS)
    set_target_properties(appLibreConnect_desktop PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/MacOSInfo.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "LibreConnect"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.libreconnect.desktop"
            MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    )
    target_link_libraries(appLibreConnect_desktop PRIVATE
            "-framework AppKit"
    )
endif()

BundleMediaRemoteAdapter(appLibreConnect_desktop)

LinkVirtualCameraLibs(appLibreConnect_desktop)
if (WIN32)
    LinkFFMPEGLibs(appLibreConnect_desktop)
endif()
