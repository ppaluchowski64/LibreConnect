import os
import sys
import subprocess

# Environment variables
env_file_path = ".env"
default_env_content = """# ==============================
# Project Environment Variables
# ==============================

# QT_DIR: Full path to your Qt installation
# Example for Windows with Qt 6.8.3 and MSVC 2022 64-bit:
# QT_DIR=C:\\Qt\\6.8.3\\msvc2022_64

QT_DIR=

"""

# Check for environment variables file 
if not os.path.exists(env_file_path):
    with open(env_file_path, "w") as f:
        f.write(default_env_content)
    print(f"Fill env properties in: {env_file_path}.")
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
    print("QT_DIR is not set in .env! Fill it before running this script.")
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
output_folder = "--output-folder=build"

def run_conan_install(build_type: str):
    cmd_parts = [
        "conan",
        "install",
        ".",
        output_folder,
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

run_conan_install("Debug")
run_conan_install("Release")