import os
import sys
import subprocess
from pathlib import Path

# Environment variables
env_file_path = ".env"
default_env_content = """
# ==============================
# Project Environment Variables
# ==============================
#
# QT_DIR:
#   Full path to your Qt installation.
#   Used by CMake and deployment tools to locate Qt libraries, plugins, and utilities (e.g., windeployqt).
#   Example (Windows, Qt 6.8.3, MSVC 2022 64-bit):
#       QT_DIR=C:\\Qt\\6.8.3\\msvc2022_64
#
# DISABLE_DEBUG:
#   Set this to disable the Conan "Debug" configuration step in the build script.
#   Accepts: 1 / true / yes / on (case-insensitive) to skip Debug build.
#   Leave empty or set to 0 / false / no / off to enable both Debug and Release builds.
#
# Example:
#   DISABLE_DEBUG=false
#
# ==============================================
# Fill in the values below and save this file.
# ==============================================

QT_DIR=
DISABLE_DEBUG=
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

# Check if QT_DIR is set
if not os.environ.get("QT_DIR"):
    print(f"QT_DIR is not set in \"{env_file_path}\"! Fill it before running this script.")
    input("Press Enter to continue...")
    exit(-1)

# Check if DISABLE_DEBUG is set
if not os.environ.get("DISABLE_DEBUG"):
    print(f"DISABLE_DEBUG is not set in \"{env_file_path}\"! Fill it before running this script.")
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

    result = subprocess.run(cmd, shell=True)
    if result.returncode != 0:
        print(f"conan install failed for build_type={build_type} (exit {result.returncode})")
        sys.exit(result.returncode)

disable_debug = os.environ.get("DISABLE_DEBUG", "").strip().lower() in ["1", "true", "yes", "on"]

if platform.startswith("linux"):
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
