BuildQTExecutable(appLibreConnect_desktop
        ${CMAKE_CURRENT_SOURCE_DIR}
        LibreConnect.desktop
        network-component-pc
        system-component-pc
        p2p-network-component-pc
        virtual-camera
        Qt6::Quick
        Qt6::Qml
        Qt6::Multimedia
)

LinkVirtualCameraLibs(appLibreConnect_desktop)