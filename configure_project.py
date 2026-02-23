import os
import shutil
import sys
import subprocess
import hashlib
import multiprocessing
import tempfile
from pathlib import Path

# Environment variables
env_file_path = ".env"
default_env_content = """
# ==============================
# Project Environment Variables
# ==============================
#
# This file defines environment variables used by the build system
# (CMake, Conan, deployment tools, and platform-specific scripts).
#
# Fill in the values below and save this file.
#

# ------------------------------------------------
# BUILD_FOR
# ------------------------------------------------
# Selects the target platform to build for.
#
# Allowed values:
#   Desktop
#   Android
#
# Example:
#   BUILD_FOR=Desktop
#   BUILD_FOR=Android
#
BUILD_FOR=

# ------------------------------------------------
# DISABLE_DEBUG
# ------------------------------------------------
# Controls whether the Debug configuration is built.
#
# true / 1 / yes / on  -> Debug disabled
# false / 0 / no / off -> Debug enabled
#
# Example:
#   DISABLE_DEBUG=false
#
DISABLE_DEBUG=

# ===================
# Desktop only
# ===================

# ------------------------------------------------
# QT_DIR_DESKTOP
# ------------------------------------------------
# Full path to your Qt installation for desktop builds.
#
# Example (Windows, Qt 6.8.3, MSVC 2022 64-bit):
#   QT_DIR_DESKTOP=C:\\Qt\\6.8.3\\msvc2022_64
#
# Example (Linux):
#   QT_DIR_DESKTOP=/opt/Qt/6.8.3/gcc_64
#
QT_DIR_DESKTOP=

# ===================
# Android only
# ===================

# ------------------------------------------------
# QT_DIR_ANDROID
# ------------------------------------------------
# Full path to the Qt installation built for Android.
#
# Example:
#   QT_DIR_ANDROID=C:\\Qt\\6.8.3\\android_arm64_v8a
#
QT_DIR_ANDROID=

# ------------------------------------------------
# ANDROID_NDK_DIR
# ------------------------------------------------
# Full path to the Android NDK installation.
#
# Example:
#   ANDROID_NDK_DIR=C:\\Android\\AndroidNDK\\android-ndk-r27c
#
ANDROID_NDK_DIR=

# ------------------------------------------------
# ANDROID_SDK_DIR
# ------------------------------------------------
# Full path to the Android SDK installation.
#
# Example:
#   ANDROID_SDK_DIR=C:\\Android\\android-sdk
#
ANDROID_SDK_DIR=

# ------------------------------------------------
# ANDROID_ARCH
# ------------------------------------------------
# Target Android ABI.
#
# Common values:
#   armv8   (arm64-v8a)
#   armv7   (armeabi-v7a)
#   x86_64
#
ANDROID_ARCH=

# ------------------------------------------------
# ANDROID_CLANG_VERSION
# ------------------------------------------------
# Clang version used by the Android NDK toolchain.
#
# Example:
#   ANDROID_CLANG_VERSION=18
#
ANDROID_CLANG_VERSION=

# ------------------------------------------------
# ANDROID_OS_API_LEVEL
# ------------------------------------------------
# Android API level to target.
#
# Example:
#   ANDROID_OS_API_LEVEL=24
#
ANDROID_OS_API_LEVEL=
"""


# Check for environment variables file 
if not os.path.exists(env_file_path):
    with open(env_file_path, "w") as f:
        f.write(default_env_content)
    print(f"Fill env properties in: \"{env_file_path}\".")
    input("Press Enter to continue...")
    exit(-1)

# Load .env
with open(env_file_path, "r") as f:
    for line in f:
        line = line.strip()
        if line and not line.startswith("#"):
            key, _, value = line.partition("=")
            os.environ[key.strip()] = value.strip()

def check_env_var(name: str):
    if not os.environ.get(name):
        print(f"${name} is not set in \"{env_file_path}\"! Fill it before running this script.")
        input("Press Enter to continue...")
        exit(-1)

check_env_var("DISABLE_DEBUG")
check_env_var("BUILD_FOR")

if os.environ.get("BUILD_FOR") == "Desktop":
    check_env_var("QT_DIR_DESKTOP")
elif os.environ.get("BUILD_FOR") == "Android":
    check_env_var("QT_DIR_ANDROID")
    check_env_var("ANDROID_NDK_DIR")
    check_env_var("ANDROID_SDK_DIR")
    check_env_var("ANDROID_ARCH")
    check_env_var("ANDROID_CLANG_VERSION")
    check_env_var("ANDROID_OS_API_LEVEL")
else:
    print(f"BUILD_FOR is not set correctly in \"{env_file_path}\"! Allowed values: [Desktop, Android].")
    input("Press Enter to continue...")
    exit(-1)


platform = sys.platform

if platform == "win32":
    cppstd = "20"
    extra_flags = ""
elif platform.startswith("linux"):
    cppstd = "gnu20"
    extra_flags = "-c tools.system.package_manager:mode=install -c tools.system.package_manager:sudo=True"
elif platform == "darwin":
    cppstd = "20"
    extra_flags = ""
else:
    cppstd = "20"
    extra_flags = ""

common_generator_flags = "-g CMakeToolchain -g CMakeDeps"
common_build_missing = "--build=missing"

def install_linux_dependencies():
    if not shutil.which("apt-get"):
        print("WARNING: 'apt-get' not found. Skipping system dependency installation.")
        return

    try:
        arch = subprocess.check_output(["dpkg", "--print-architecture"], text=True).strip()
        codename = subprocess.check_output(["lsb_release", "-cs"], text=True).strip()
        print(f"Detected architecture: {arch}, Ubuntu codename: {codename}")

        if arch == "amd64":
            print("Setting up LunarG repository for x86_64...")
            subprocess.run(
                "wget -qO - https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo gpg --dearmor --yes -o /usr/share/keyrings/lunarg-signing-key-pub.gpg",
                shell=True, check=True
            )
            vulkan_repo = f"deb [signed-by=/usr/share/keyrings/lunarg-signing-key-pub.gpg] https://packages.lunarg.com/vulkan {codename} main"
            subprocess.run(f'echo "{vulkan_repo}" | sudo tee /etc/apt/sources.list.d/lunarg-vulkan.list', shell=True, check=True)

        subprocess.run("sudo add-apt-repository -y multiverse", shell=True, check=True)

        print("Updating package lists...")
        subprocess.run(["sudo", "apt-get", "update"], check=True)

        print("Installing packages...")

        packages = [
            "libyaml-cpp-dev",
            "libvulkan-dev",
            "vulkan-tools",
            "build-essential",
            "libgl1-mesa-dev",
            "libxkbcommon-x11-0",
            "libdbus-1-dev",
            "libfontconfig1",
            "libxcb-icccm4",
            "libxcb-image0",
            "libxcb-keysyms1",
            "libxcb-randr0",
            "libxcb-render-util0",
            "libxcb-shape0",
            "libxcb-xinerama0",
            "libxcb-xkb1",
            "libxkbcommon-dev",
            "libmysqlclient-dev",
            "unixodbc-dev",
            "libpq-dev",
            "pkg-config",
            "git",
            "yasm",
            "nasm",
            "libdrm-dev",
            "libva-dev",
            "libvdpau-dev",
            "libx264-dev",
            "libx265-dev",
            "libnuma-dev",
            "libvpx-dev",
            "libfdk-aac-dev",
            "libmp3lame-dev",
            "libopus-dev",
            "libxcb1-dev",
            "libxcb-shm0-dev",
            "libxcb-xfixes0-dev",
            "libwayland-dev",
            "wayland-protocols",
            "ocl-icd-opencl-dev",
            "opencl-headers",
            "libx11-dev",
        ]

        if arch == "amd64":
            packages.extend([
                "vulkan-sdk",
                "libvpl-dev",
                "intel-media-va-driver-non-free",
                "nvidia-cuda-toolkit"
            ])

        cmd = ["sudo", "apt-get", "install", "-y"] + packages
        subprocess.run(cmd, check=True)

        print("System dependencies installed successfully.")

    except subprocess.CalledProcessError as e:
        print(f"ERROR: Failed to install system dependencies. {e}")
        sys.exit(e.returncode)


def run_conan_install(build_type: str):
    cmd_parts = [
        "conan",
        "install",
        ".",
        common_build_missing,
        common_generator_flags,
        f"-s compiler.cppstd={cppstd}",
        f"-s build_type={build_type}"
    ]

    if extra_flags:
        cmd_parts.append(extra_flags)

    cmd = " ".join(cmd_parts)
    print(f"Running: {cmd}")
    print("Python sees cmake:", shutil.which("cmake"))

    result = subprocess.run(cmd, env=os.environ)
    if result.returncode != 0:
        print(f"conan install failed for build_type={build_type} (exit {result.returncode})")
        sys.exit(result.returncode)

def run_conan_install_android(build_type: str):
    arch_type = os.environ.get("ANDROID_ARCH")
    ndk = Path(os.environ.get("ANDROID_NDK_DIR")).expanduser()
    clang_version = os.environ.get("ANDROID_CLANG_VERSION")
    api_level = os.environ.get("ANDROID_OS_API_LEVEL")

    ndk_path = Path(ndk)
    toolchain_base = ndk_path / "toolchains" / "llvm" / "prebuilt"

    try:
        host_tag = next(toolchain_base.iterdir()).name
    except (StopIteration, FileNotFoundError):
        print(f"ERROR: NDK structure invalid at {toolchain_base}")
        sys.exit(1)

    exe_ext = ".exe" if sys.platform == "win32" else ""
    bin_dir = toolchain_base / host_tag / "bin"

    addr2line_path = bin_dir / f"llvm-addr2line{exe_ext}"
    if not addr2line_path.exists():
        addr2line_path = bin_dir / f"addr2line{exe_ext}"
    addr2line = str(addr2line_path).replace("\\", "/")

    ndk_arch_prefix = ""
    if arch_type == "armv8":
        ndk_arch_prefix = f"aarch64-linux-android{api_level}-"
    elif arch_type == "armv7":
        ndk_arch_prefix = f"armv7a-linux-androideabi{api_level}-"
    elif arch_type == "x86_64":
        ndk_arch_prefix = f"x86_64-linux-android{api_level}-"
    elif arch_type == "x86":
        ndk_arch_prefix = f"i686-linux-android{api_level}-"

    compiler_suffix = ".cmd" if sys.platform == "win32" else ""
    c_compiler = f"{ndk_arch_prefix}clang{compiler_suffix}"
    cpp_compiler = f"{ndk_arch_prefix}clang++{compiler_suffix}"

    strip_tool = str(bin_dir / f"llvm-strip{exe_ext}").replace("\\", "/")
    ar_tool = str(bin_dir / f"llvm-ar{exe_ext}").replace("\\", "/")
    nm_tool = str(bin_dir / f"llvm-nm{exe_ext}").replace("\\", "/")
    ranlib_tool = str(bin_dir / f"llvm-ranlib{exe_ext}").replace("\\", "/")

    profile_content = f"""
        include(default)
        [buildenv]
        STRIP={strip_tool}
        AR={ar_tool}
        NM={nm_tool}
        RANLIB={ranlib_tool}
    """

    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix=".jinja") as tmp_profile:
        tmp_profile.write(profile_content)
        tmp_profile_path = tmp_profile.name

    try:
        cmd_args = [
            "conan", "install", ".",
            "--build=missing",
            "-pr:h", tmp_profile_path,
            "-pr:b", "default",

            "-g", "CMakeToolchain",
            "-g", "CMakeDeps",

            "-s", f"build_type={build_type}",
            "-s:h", "os=Android",
            "-s:h", f"os.api_level={api_level}",
            "-s:h", f"arch={arch_type}",
            "-s:h", "compiler=clang",
            "-s:h", f"compiler.version={clang_version}",
            "-s:h", "compiler.libcxx=c++_static",
            "-s:h", f"compiler.cppstd=20",

            "-o", "boost/*:with_stacktrace_backtrace=False",
            "-o", "ffmpeg/*:with_mediacodec=True",
            "-o", "ffmpeg/*:with_jni=True",
            "-o", "ffmpeg/*:with_libx264=True",
            "-o", "ffmpeg/*:with_libx265=True",
            "-o", "ffmpeg/*:shared=False",
            "-o", "ffmpeg/*:with_asm=False",

            "-o", "ffmpeg/*:fPIC=True",
            "-o", "*:fPIC=True",
            "-o", "ffmpeg/*:extra_cflags=-fPIC",
            "-o", "ffmpeg/*:extra_ldflags=-fPIC",
            "-c", "tools.build:cflags=['-fPIC']",
            "-c", "tools.build:cxxflags=['-fPIC']",

            "-c", "tools.cmake.cmaketoolchain:generator=Ninja",
            "-c", f"tools.build:compiler_executables={{'c': '{c_compiler}', 'cpp': '{cpp_compiler}'}}",
            "-c", f"tools.android:ndk_path={ndk}",
            "-o", f"boost/*:addr2line_location={addr2line}",
            "-o", "boost/*:without_stacktrace=True"
        ]

        env = os.environ.copy()
        env["PATH"] = str(bin_dir) + os.pathsep + env["PATH"]

        print(f"Running conan install with profile: {tmp_profile_path}")
        result = subprocess.run(cmd_args, env=env)

        if result.returncode != 0:
            print(f"conan install failed for build_type={build_type} (exit {result.returncode})")
            sys.exit(result.returncode)

    finally:
        if os.path.exists(tmp_profile_path):
            os.remove(tmp_profile_path)

disable_debug = os.environ.get("DISABLE_DEBUG", "").strip().lower() in ["1", "true", "yes", "on"]

if platform.startswith("linux"):
    install_linux_dependencies()
    tools_dir = Path("~/.local/bin").expanduser()
    tools_dir.mkdir(parents=True, exist_ok=True)

    linuxdeploy = tools_dir / "linuxdeploy"
    plugin_qt = tools_dir / "linuxdeploy-plugin-qt"

    arch = os.uname().machine
    if arch == "x86_64":
        linuxdeploy_url = "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage"
        plugin_qt_url = "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage"
    elif arch == "aarch64":
        linuxdeploy_url = "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-aarch64.AppImage"
        plugin_qt_url = "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-aarch64.AppImage"
    else:
        print(f"Unsupported architecture: {arch}")
        sys.exit(1)

    if not linuxdeploy.exists():
        print("Installing linuxdeploy...")
        subprocess.run(["wget", "-q", "-O", str(linuxdeploy), linuxdeploy_url], check=True)
        subprocess.run(["chmod", "+x", str(linuxdeploy)], check=True)

    if not plugin_qt.exists():
        print("Installing linuxdeploy-plugin-qt...")
        subprocess.run(["wget", "-q", "-O", str(plugin_qt), plugin_qt_url], check=True)
        subprocess.run(["chmod", "+x", str(plugin_qt)], check=True)

    path_str = str(tools_dir)
    home = Path.home()
    profiles = [home / ".bashrc", home / ".zshrc", home / ".profile"]
    for profile in profiles:
        if profile.exists():
            content = profile.read_text()
            if path_str in content:
                break
    else:
        export_line = f'\n# Added by Qt setup script\nexport PATH="{path_str}:$PATH"\n'
        target = home / ".bashrc"
        with open(target, "a") as f:
            f.write(export_line)

shutil.rmtree("./build", ignore_errors=True)



if os.environ.get("BUILD_FOR") == "Desktop":
    if platform == "win32":
        ffmpeg_root = None

        raw_env_path = os.environ.get("FFMPEG_DIR", "")
        env_ffmpeg_dir = raw_env_path.strip().strip('"').strip("'").replace("/", "\\")

        if env_ffmpeg_dir:
            print(f"Found FFMPEG_DIR in environment: '{env_ffmpeg_dir}'")
            ffmpeg_path = Path(env_ffmpeg_dir)

            if ffmpeg_path.exists():
                ffmpeg_root = ffmpeg_path

        if not ffmpeg_root:
            try:
                print("FFMPEG_DIR not set. Attempting to install ffmpeg via winget...")
                cmd = [
                    "winget",
                    "install",
                    "-e",
                    "--id",
                    "Gyan.FFmpeg.Shared",
                    "--version",
                    "7.1.1",
                    "--source",
                    "winget",
                    "--silent",
                    "--accept-package-agreements",
                    "--accept-source-agreements",
                    "--disable-interactivity"
                ]
                subprocess.run(cmd, check=True, capture_output=True, text=True, shell=True)
                print("FFmpeg installed via winget")
            except (subprocess.CalledProcessError, FileNotFoundError):
                print("Winget installation failed or winget not found. skipping...")


            result = subprocess.run(["where", "ffmpeg"], capture_output=True, text=True)
            for path in result.stdout.splitlines():
                if path:
                    ffmpeg_root = Path(path).parent.parent
                    break

        if ffmpeg_root and ffmpeg_root.exists():
            print(f"Deploying FFmpeg from: {ffmpeg_root}")

            bin_dir = Path("./build/ffmpeg/bin")
            lib_dir = Path("./build/ffmpeg/lib")
            include_dst = Path("./build/ffmpeg/include")

            bin_dir.mkdir(parents=True, exist_ok=True)
            lib_dir.mkdir(parents=True, exist_ok=True)
            include_dst.mkdir(parents=True, exist_ok=True)

            dll_src = ffmpeg_root / "bin"
            lib_src = ffmpeg_root / "lib"
            include_src = ffmpeg_root / "include"

            # Copy DLLs
            if dll_src.exists():
                for file in dll_src.iterdir():
                    if file.is_file() and file.suffix.lower() == ".dll":
                        shutil.copy2(file, bin_dir)
            else:
                print(f"Warning: dll source not found at {dll_src}")

            # Copy LIBs
            if lib_src.exists():
                for file in lib_src.iterdir():
                    if file.is_file() and file.suffix.lower() == ".lib":
                        shutil.copy2(file, lib_dir)

            # Copy includes
            if include_src.exists():
                shutil.copytree(include_src, include_dst, dirs_exist_ok=True)

            print("FFmpeg deployed successfully.")
        else:
            print("Error: Could not locate FFmpeg installation (Checked FFMPEG_DIR and PATH).")
            sys.exit(1)
    elif sys.platform.startswith("linux"):
        build_dir = os.path.abspath("build")
        ffmpeg_src = os.path.join(build_dir, "ffmpeg-src")
        nvcodec_src = os.path.join(build_dir, "nv-codec-headers-src")

        arch = os.uname().machine
        is_arm = (arch == "aarch64" or arch == "arm64")

        os.makedirs(build_dir, exist_ok=True)
        cuda_path = os.environ.get("CUDA_PATH", "/usr/local/cuda")

        configure_cmd = [
            "./configure",
            "--disable-programs",
            "--prefix=../ffmpeg",
            "--enable-shared",
            "--disable-static",
            "--enable-gpl",
            "--enable-nonfree",
            "--enable-swresample",

            "--enable-libx264",
            "--enable-libx265",
            "--enable-libvpx",
            "--enable-libopus",
            "--enable-libmp3lame",
            "--enable-libfdk-aac",
            "--enable-libdrm",

            "--enable-opencl",
            "--enable-libxcb",

            "--extra-ldflags=-Wl,--no-as-needed",
        ]

        if not is_arm:
            configure_cmd.extend([
                "--enable-libvpl",
                "--enable-vaapi",
                "--enable-vdpau",
                "--enable-cuda-nvcc",
                "--enable-cuvid",
                "--enable-nvenc",
                "--enable-libnpp",
                "--enable-vulkan",

                f"--extra-cflags=-I{cuda_path}/include",
                f"--extra-ldflags=-L{cuda_path}/lib64",
            ])

        data = "\n".join(configure_cmd).encode("utf-8")
        sha256 = hashlib.sha256(data).hexdigest()
        cache = os.path.expanduser(f"~/.LibreConnect-cache/ffmpeg-{sha256}")

        def run(cmd, cwd=None):
            subprocess.run(cmd, cwd=cwd, check=True)

        if os.path.exists(cache):
            print("ffmpeg cache hit")
            shutil.copytree(cache, "build/ffmpeg")
        else:
            print("ffmpeg cache miss")

            if not os.path.exists(ffmpeg_src):
                run([
                    "git", "clone",
                    "https://github.com/FFmpeg/FFmpeg.git",
                    ffmpeg_src
                ])

            if not os.path.exists(nvcodec_src):
                run([
                    "git", "clone",
                    "https://git.videolan.org/git/ffmpeg/nv-codec-headers.git",
                    nvcodec_src
                ])

            run(["make"], cwd=nvcodec_src)
            run(["sudo", "make", "install"], cwd=nvcodec_src)
            run(["git", "checkout", "n7.1"], cwd=ffmpeg_src)

            run(configure_cmd, cwd=ffmpeg_src)
            run(["make", f"-j{multiprocessing.cpu_count()}"], cwd=ffmpeg_src)
            run(["sudo", "make", "install"], cwd=ffmpeg_src)

            shutil.copytree("build/ffmpeg", cache, dirs_exist_ok=True)

# TO DO: MACOS, ANDROID, IOS VERSION
# elif platform == "darwin":
#     subprocess.run(["brew", "update"])
#     subprocess.run(["brew", "install", "ffmpeg@7"])
#     subprocess.run(["brew", "link", "--overwrite", "ffmpeg@7"])

if os.environ.get("BUILD_FOR") == "Desktop":
    if not disable_debug:
        run_conan_install("Debug")
    run_conan_install("Release")
elif os.environ.get("BUILD_FOR") == "Android":
    if not disable_debug:
        run_conan_install_android("Debug")
    run_conan_install_android("Release")
