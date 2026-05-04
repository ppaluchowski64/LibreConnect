BuildProgram(desktop-daemon .
        project_defaults
        Debug-Log
        network-component-pc
        system-component-pc
        p2p-network-component-pc
)

BuildStaticLibrary(desktop-daemon-utilities utilities project_defaults Debug-Log network-component-pc system-component-pc p2p-network-component-pc)