BuildQTExecutable(appLibreConnect_test_mobile
        ${CMAKE_CURRENT_SOURCE_DIR}
        LibreConnect.mobile
        network-component-pc
        system-component-pc
        p2p-network-component-pc
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Qml
        Qt6::Multimedia
        modules-manager
)

LinkVirtualCameraLibs(appLibreConnect_test_mobile)
LinkFFMPEGLibs(appLibreConnect_test_mobile)