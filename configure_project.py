import os
import shutil
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
    install_linux_dependencies()
    tools_dir = Path("~/.local/bin").expanduser()
    tools_dir.mkdir(parents=True, exist_ok=True)
    target = tools_dir / "linuxdeployqt"

    if not target.exists():
        print("Installing linuxdeployqt globally...")
        url = "https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage"
        subprocess.run(["wget", "-q", "-O", str(target), url], check=True)
        subprocess.run(["chmod", "+x", str(target)], check=True)

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