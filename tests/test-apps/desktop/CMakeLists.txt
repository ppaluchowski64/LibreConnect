BuildQTExecutable(appLibreConnect_test_desktop
        ${CMAKE_CURRENT_SOURCE_DIR}
        LibreConnect.desktop
        network-component-pc
        system-component-pc
        p2p-network-component-pc
        Qt6::Multimedia
        Qt6::Core
        modules-manager
)

LinkVirtualCameraLibs(appLibreConnect_test_desktop)
LinkFFMPEGLibs(appLibreConnect_test_desktop)
