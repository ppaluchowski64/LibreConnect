if (APPLE AND NOT IOS)
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

LinkVirtualCameraLibs(appLibreConnect_desktop)
if (WIN32)
    LinkFFMPEGLibs(appLibreConnect_desktop)
endif()
