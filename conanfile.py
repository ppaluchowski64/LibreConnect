from conan import ConanFile
from conan.tools.cmake import cmake_layout, CMakeToolchain, CMakeDeps

class ConanApplication(ConanFile):
    package_type = "application"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"

    default_options = {
        "boost/*:without_atomic": False,
        "boost/*:without_chrono": False,
        "boost/*:without_container": False,
        "boost/*:without_context": False,
        "boost/*:without_contract": True,
        "boost/*:without_coroutine": True,
        "boost/*:without_date_time": False,
        "boost/*:without_endian": False,
        "boost/*:without_exception": False,
        "boost/*:without_fiber": True,
        "boost/*:without_filesystem": True,
        "boost/*:without_graph": True,
        "boost/*:without_graph_parallel": True,
        "boost/*:without_iostreams": True,
        "boost/*:without_json": True,
        "boost/*:without_locale": True,
        "boost/*:without_log": True,
        "boost/*:without_math": True,
        "boost/*:without_mpi": True,
        "boost/*:without_nowide": True,
        "boost/*:without_process": True,
        "boost/*:without_program_options": True,
        "boost/*:without_random": True,
        "boost/*:without_regex": True,
        "boost/*:without_serialization": True,
        "boost/*:without_stacktrace": False,
        "boost/*:without_system": False,
        "boost/*:without_test": True,
        "boost/*:without_thread": False,
        "boost/*:without_timer": True,
        "boost/*:without_type_erasure": True,
        "boost/*:without_unit_test_framework": True,
        "boost/*:without_wave": True,
        "qt/*:gui": True,                # GUI support
        "qt/*:widgets": True,            # QWidget-based UI
        "qt/*:network": False,           # QtNetwork module
        "qt/*:concurrent": True,         # QtConcurrent
        "qt/*:dbus": False,              # D-Bus (Linux-only usually)
        "qt/*:sql": False,               # Qt SQL module (disable to avoid libpq)
	    "qt/*:with_pq": False,
        "qt/*:svg": True,                # SVG rendering
        "qt/*:xml": True,                # XML parsing
        "qt/*:xmlpatterns": False,       # XPath/XQuery (deprecated, rarely needed)
        "qt/*:testlib": False,           # QtTest (disable if using gtest)
        "qt/*:printsupport": False,      # QPrinter support (needs CUPS)
        "qt/*:opengl": "no",             # OpenGL integration
        "qt/*:quick": True,              # QtQuick / QML (heavy)
        "qt/*:qml": True ,               # QML engine (heavy)
        "qt/*:quickcontrols2": False,    # QML UI toolkit (depends on qtquick)
        "qt/*:positioning": False,       # GPS/Geo APIs
        "qt/*:location": False,          # Mapping / location APIs
        "qt/*:multimedia": False,        # Audio/video
        "qt/*:sensors": False,           # Accelerometer, etc.
        "qt/*:bluetooth": False,         # Bluetooth stack
        "qt/*:serialport": False,        # RS232/serial
        "qt/*:serialbus": False,         # CAN bus, Modbus
        "qt/*:websockets": False,        # WebSocket API
        "qt/*:webchannel": False,        # WebChannel bridge (JS ↔ C++)
        "qt/*:remoteobjects": False,     # Inter-process object replication
        "qt/*:3d": False,                # Qt3D module
        "qt/*:charts": False,            # QtCharts
        "qt/*:datavis3d": False,         # 3D data visualization
        "qt/*:imageformats": True,       # PNG, JPEG, etc.
        "qt/*:wayland": False,           # Wayland compositor
        "qt/*:shadertools": True,        # Required for modern QML/Quick builds
        "qt/*:openssl": False,           # Use OpenSSL for SSL
        "qt/*:with_dbus": False,         # Extra dbus-related bits
        "qt/*:with_harfbuzz": True,      # Font shaping
        "qt/*:with_freetype": True,      # Font rendering
        "qt/*:with_glib": False,         # GLib event loop integration
        "qt/*:with_icu": False,          # Unicode library (large dependency)
        "qt/*:with_pcre2": True,         # Regex engine
        "qt/*:shared": False,            # Build shared libs
        "qt/*:gui_tools": False,         # Disable Qt Creator tools (designer, linguist)
        "qt/*:qttools": False,           # Disable dev tools module
        "qt/*:qttranslations": False,    # Disable translations
        "qt/*:qtdeclarative": False,     # Disable QML engine
        "qt/*:qtwebengine": False,       # Disable WebEngine (Chromium)
        "qt/*:qtwebsockets": True        # Disable WebSockets (redundant, just in case)
    }

    def layout(self):
        cmake_layout(self)

    def generate(self):
        pass

    def requirements(self):
        requirements = self.conan_data.get('requirements', [])
        for requirement in requirements:
            self.requires(requirement)
