import os
import sys
from pathlib import Path
import xml.etree.ElementTree as ET


def find_workspace_xml(repo_root: Path) -> Path:
    candidate = repo_root / ".idea" / "workspace.xml"
    if not candidate.exists():
        raise FileNotFoundError(f"workspace.xml not found at: {candidate}")
    return candidate


def ensure_component(root: ET.Element, name: str) -> ET.Element:
    for comp in root.findall("component"):
        if comp.get("name") == name:
            return comp
    comp = ET.SubElement(root, "component")
    comp.set("name", name)
    return comp


def ensure_configurations(cmake_settings: ET.Element) -> ET.Element:
    configs = cmake_settings.find("configurations")
    if configs is None:
        configs = ET.SubElement(cmake_settings, "configurations")
    return configs


def upsert_profile(configs: ET.Element, name: str, generation_dir: str, toolchain_path: str,
                   config_name: str = "Debug", generator_name: str | None = None) -> None:
    target = None
    for cfg in configs.findall("configuration"):
        if cfg.get("PROFILE_NAME") == name:
            target = cfg
            break
    if target is None:
        target = ET.SubElement(configs, "configuration")

    target.set("PROFILE_NAME", name)
    target.set("ENABLED", "true")
    target.set("GENERATION_DIR", generation_dir)
    target.set("CONFIG_NAME", config_name)
    generation_options = [f"-DCMAKE_TOOLCHAIN_FILE={toolchain_path}"]
    if generator_name:
        generation_options.insert(0, "-G Ninja")
    target.attrib["GENERATION_OPTIONS"] = " ".join(generation_options)

    if generator_name is None:
        target.attrib.pop("GENERATOR_NAME", None)
        target.attrib.pop("GENERATOR", None)
        target.set("NO_GENERATOR", "true")
    else:
        target.attrib.pop("NO_GENERATOR", None)
        target.set("GENERATOR_NAME", generator_name)
        target.set("GENERATOR", generator_name)


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    workspace = find_workspace_xml(repo_root)

    tree = ET.parse(workspace)
    root = tree.getroot()

    if root.tag != "project":
        raise ValueError("Invalid workspace.xml: missing <project> root.")

    cmake_settings = ensure_component(root, "CMakeSettings")
    if cmake_settings.get("AUTO_RELOAD") is None:
        cmake_settings.set("AUTO_RELOAD", "true")

    configs = ensure_configurations(cmake_settings)

    def norm(path: Path) -> str:
        return str(path.as_posix())

    toolchain_desktop_debug = norm(repo_root / "build/desktop/Debug/build/Debug/generators/conan_toolchain.cmake")
    toolchain_desktop_release = norm(repo_root / "build/desktop/Release/build/Release/generators/conan_toolchain.cmake")
    toolchain_android_debug = norm(repo_root / "build/android/build/Debug/generators/conan_toolchain.cmake")
    toolchain_android_release = norm(repo_root / "build/android/build/Release/generators/conan_toolchain.cmake")
    android_generator = "Ninja"

    upsert_profile(
        configs,
        name="Desktop-Debug",
        generation_dir="build/desktop/build/Debug",
        toolchain_path=toolchain_desktop_debug,
        generator_name="Ninja",
    )

    upsert_profile(
        configs,
        name="Desktop-Release",
        generation_dir="build/desktop/build/Release",
        toolchain_path=toolchain_desktop_release,
        config_name="Release",
        generator_name="Ninja",
    )

    upsert_profile(
        configs,
        name="Android-Debug",
        generation_dir="build/android/build/Debug",
        toolchain_path=toolchain_android_debug,
        generator_name=android_generator,
    )

    upsert_profile(
        configs,
        name="Android-Release",
        generation_dir="build/android/build/Release",
        toolchain_path=toolchain_android_release,
        config_name="Release",
        generator_name=android_generator,
    )

    tree.write(workspace, encoding="utf-8", xml_declaration=True)
    print(f"CLion CMake profiles updated in {workspace}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
