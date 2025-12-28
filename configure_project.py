import os
import shutil
import sys
import subprocess
from pathlib import Path

from marshmallow.fields import String

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
#

# ------------------------------------------------
# QT_DIR
# ------------------------------------------------
# Full path to your Qt installation for desktop builds.
# Used by CMake and deployment tools to locate Qt libraries,
# plugins, and utilities (e.g., windeployqt, macdeployqt).
#
# Example (Windows, Qt 6.8.3, MSVC 2022 64-bit):
#   QT_DIR=C:\\Qt\\6.8.3\\msvc2022_64
#
# Example (Linux):
#   QT_DIR=/opt/Qt/6.8.3/gcc_64
#

# ------------------------------------------------
# DISABLE_DEBUG
# ------------------------------------------------
# Controls whether the Debug configuration is built.
#
# If set to any of the following values (case-insensitive):
#   1 / true / yes / on
# the Debug build step will be skipped and only Release will be built.
#
# If set to:
#   0 / false / no / off
# both Debug and Release configurations will be built.
#
# Example:
#   DISABLE_DEBUG=true
#

# ------------------------------------------------
# BUILD_ANDROID
# ------------------------------------------------
# Enables Android-specific build steps.
#
# If set to:
#   1 / true / yes / on
# Android toolchains, Qt for Android, and the Android NDK
# will be used during the build.
#
# If unset or set to:
#   0 / false / no / off
# Android-related variables are ignored.
#
# Example:
#   BUILD_ANDROID=true
#

# ------------------------------------------------
# QT_DIR_ANDROID
# ------------------------------------------------
# Full path to the Qt installation built for Android.
# Required only when BUILD_ANDROID is enabled.
#
# Example:
#   QT_DIR_ANDROID=C:\\Qt\\6.8.3\\android_arm64_v8a
#
# The selected Qt Android ABI must match the Android NDK
# and the target ABI configured in your build scripts.
#

# ------------------------------------------------
# ANDROID_NDK_DIR
# ------------------------------------------------
# Full path to the Android NDK installation.
# Required only when BUILD_ANDROID is enabled.
#
# Example:
#   ANDROID_NDK_DIR=C:\\Android\\Sdk\\ndk\\26.1.10909125
#
# Make sure the NDK version is compatible with both
# your Qt for Android version and your CMake toolchain.
#

# ==============================================
# Fill in the values below and save this file.
# ==============================================

# ===================
# Common variables
# ===================

QT_DIR=
DISABLE_DEBUG=

# ================
# Android only
# ================

BUILD_ANDROID=
QT_DIR_ANDROID=
ANDROID_NDK_DIR=
DISABLE_ANDROID_DEBUG=
ANDROID_ARCH=

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

check_env_var("QT_DIR")
check_env_var("DISABLE_DEBUG")
check_env_var("BUILD_ANDROID")

if os.environ.get("BUILD_ANDROID"):
    check_env_var("QT_DIR_ANDROID")
    check_env_var("ANDROID_NDK_DIR")
    check_env_var("DISABLE_ANDROID_DEBUG")
    check_env_var("ANDROID_ARCH")
    check_env_var("ANDROID_CLANG_VERSION")
    check_env_var("ANDROID_OS_API_LEVEL")

build_android = os.environ.get("BUILD_ANDROID", "").strip().lower() in ["1", "true", "yes", "on"]
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
        print("Please ensure the required build packages are installed manually.")
        return

    try:
        print("Updating package lists...")
        subprocess.run(["sudo", "apt-get", "update"], check=True)

        print("Installing packages...")
        packages = [
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
            "libmysqlclient21",
            "libmysqlclient-dev",
            "unixodbc",
            "unixodbc-dev",
            "libpq-dev"
        ]

        cmd = ["sudo", "apt-get", "install", "-y"] + packages
        subprocess.run(cmd, check=True)
        print("System dependencies installed successfully.")

    except subprocess.CalledProcessError as e:
        print(f"ERROR: Failed to install system dependencies. {e}")
        print("Please try running the 'sudo apt-get ...' commands manually.")
        sys.exit(e.returncode)
    except FileNotFoundError:
        print("ERROR: 'sudo' command not found. Cannot install system dependencies.")
        sys.exit(1)
def run_conan_install(build_type: str):
    cmd_parts = [
        "conan",
        "install",
        ".",
        common_build_missing,
        common_generator_flags,
        f"-s compiler.cppstd={cppstd}",
        f"-s build_type={build_type}",
        "-of", f"build/desktop/{build_type}"
    ]

    if extra_flags:
        cmd_parts.append(extra_flags)

    cmd = " ".join(cmd_parts)
    print(f"Running: {cmd}")

    result = subprocess.run(cmd, shell=True)
    if result.returncode != 0:
        print(f"conan install failed for build_type={build_type} (exit {result.returncode})")
        sys.exit(result.returncode)

def run_conan_install_android(build_type: str):
    arch_type = os.environ.get("ANDROID_ARCH")
    ndk = os.environ.get("ANDROID_NDK_DIR")
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
    addr2line_path = bin_dir / f"addr2line{exe_ext}"

    if not addr2line_path.exists():
        addr2line_path = bin_dir / f"llvm-addr2line{exe_ext}"

    addr2line = str(addr2line_path).replace("\\", "/")

    arch_map = {
        "armv7": "armv7a-linux-androideabi",
        "armeabi-v7a": "armv7a-linux-androideabi",
        "arm64-v8a": "aarch64-linux-android",
        "aarch64": "aarch64-linux-android",
        "x86": "i686-linux-android",
        "x86_64": "x86_64-linux-android"
    }

    triple = arch_map.get(arch_type)
    if not triple:
        print(f"ERROR: Unknown or unsupported ANDROID_ARCH: {arch_type}")
        print(f"Supported values: {list(arch_map.keys())}")
        sys.exit(1)

    target_flag = f"--target={triple}{api_level}"

    cmd_args = [
        "conan",
        "install",
        ".",
        "--build=missing",
        "-g", "CMakeToolchain",
        "-g", "CMakeDeps",
        "-s", f"build_type={build_type}",
        "-s:h", "os=Android",
        "-s:h", f"os.api_level={api_level}",
        "-s:h", f"arch={arch_type}",
        "-s:h", "compiler=clang",
        "-s:h", f"compiler.version={clang_version}",
        "-s:h", f"compiler.cppstd={cppstd}",
        "-s:h", "compiler.libcxx=c++_static",
        "-c", "tools.cmake.cmaketoolchain:generator=Ninja",

        # Pass these as raw Python-style lists; subprocess will handle the quoting
        "-c", f'tools.build:cxxflags=["{target_flag}"]',
        "-c", f'tools.build:cflags=["{target_flag}"]',
        "-c", f'tools.build:exelinkflags=["{target_flag}"]',
        "-c", f'tools.build:sharedlinkflags=["{target_flag}"]',

        "-o", f"boost/*:extra_b2_flags=cflags={target_flag} cxxflags={target_flag}",

        "-pr:b", "default",
        "-s:b", f"compiler.cppstd={cppstd}",
        "-c", f"tools.android:ndk_path={ndk}",
        "-o", f"boost/*:addr2line_location={addr2line}",
        "-of", f"build/android/{build_type}"
    ]

    if extra_flags:
        cmd_args.extend(extra_flags.split())

    print(f"Running: {' '.join(cmd_args)}")

    env = os.environ.copy()
    env["PATH"] = str(bin_dir) + os.pathsep + env["PATH"]
    env["CFLAGS"] = target_flag
    env["CXXFLAGS"] = target_flag

    result = subprocess.run(cmd_args, shell=False, env=env)

    if result.returncode != 0:
        print(f"conan install failed for build_type={build_type} (exit {result.returncode})")
        sys.exit(result.returncode)


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

if not disable_debug:
    run_conan_install("Debug")
run_conan_install("Release")

if build_android:
    disable_debug_android = os.environ.get("DISABLE_DEBUG_ANDROID", "").strip().lower() in ["1", "true", "yes", "on"]

    if not disable_debug_android:
        run_conan_install_android("Debug")

    run_conan_install_android("Release")
