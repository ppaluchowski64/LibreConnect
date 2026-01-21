from conan import ConanFile
from conan.tools.cmake import cmake_layout, CMakeToolchain, CMakeDeps

class ConanApplication(ConanFile):
    package_type = "application"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"

    default_options = {
        "boost/*:without_system": False,
        "boost/*:without_exception": False,
        "boost/*:without_cobalt": True,
        "boost/*:without_context": True,
        "boost/*:without_thread": True,
        "boost/*:without_atomic": True,
        "boost/*:without_chrono": True,
        "boost/*:without_date_time": True,
        "boost/*:without_container": True,
        "boost/*:without_filesystem": True,
        "boost/*:without_charconv": True,
        "boost/*:without_contract": True,
        "boost/*:without_coroutine": True,
        "boost/*:without_endian": False,
        "boost/*:without_fiber": True,
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
        "boost/*:without_test": True,
        "boost/*:without_timer": True,
        "boost/*:without_type_erasure": True,
        "boost/*:without_unit_test_framework": True,
        "boost/*:without_url": True,
        "boost/*:without_wave": True,

        "ffmpeg/*:gpl": True,
        "ffmpeg/*:nonfree": True,
        "ffmpeg/*:version3": True,
        "ffmpeg/*:with_libx264": True,
        "ffmpeg/*:with_libx265": True,
        "ffmpeg/*:with_libvpx": True,
        "ffmpeg/*:with_libopus": True,
        "ffmpeg/*:with_libmp3lame": True,
        "ffmpeg/*:with_nvenc": True,
        "ffmpeg/*:with_cuda": True,
        "ffmpeg/*:with_libnpp": True,
        "ffmpeg/*:with_vpl": True,
        "ffmpeg/*:with_qsv": True,
        "ffmpeg/*:with_amf": True,
        "ffmpeg/*:with_vaapi": True,
        "ffmpeg/*:with_vdpau": True,
        "ffmpeg/*:with_videotoolbox": True,
        "ffmpeg/*:shared": True,
    }

    def layout(self):
        cmake_layout(self)

    def generate(self):
        pass

    def requirements(self):
        data = self.conan_data.get("requirements", {})

        for req in data.get("common", []):
            self.requires(req)

        if self.settings.os == "Windows":
            for req in data.get("windows", []):
                self.requires(req)
