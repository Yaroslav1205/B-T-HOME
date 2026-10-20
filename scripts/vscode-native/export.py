"""Export native IDF artifacts to the physical Windows IDE build directory."""

import argparse
import ctypes
import json
from pathlib import Path
import shutil


def split_windows_command(command: str):
    split = ctypes.windll.shell32.CommandLineToArgvW
    split.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    split.restype = ctypes.POINTER(ctypes.c_wchar_p)
    release = ctypes.windll.kernel32.LocalFree
    release.argtypes = [ctypes.c_void_p]
    release.restype = ctypes.c_void_p
    count = ctypes.c_int()
    arguments = split(command, ctypes.byref(count))
    if not arguments:
        raise ctypes.WinError()
    try:
        return list(arguments[:count.value])
    finally:
        release(arguments)


def export(destination: Path, configuration_only: bool = False) -> None:
    root = Path(__file__).resolve().parents[2]
    source = root / "build/esp32c6_rgb8"
    destination = destination.resolve()
    if destination == source.resolve():
        raise ValueError("IDE artifacts must not overwrite the native build cache")
    description = json.loads((source / "project_description.json").read_text(encoding="utf-8"))
    native_root = description["project_path"].replace("\\", "/").rstrip("/") + "/"

    def physical(value: str) -> str:
        return value.replace("\\", "/").replace(native_root, root.as_posix() + "/")

    def translate(value):
        if isinstance(value, str):
            return physical(value)
        if isinstance(value, list):
            return [translate(item) for item in value]
        if isinstance(value, dict):
            return {key: translate(item) for key, item in value.items()}
        return value

    def write_json(name, value):
        (destination / name).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")

    files = ["config/sdkconfig.h", "config/sdkconfig.json", "config/kconfig_menus.json"]
    if not configuration_only:
        files += ["bt_home.bin", "bt_home.elf", "bt_home.map", "flash_args",
                  "flash_project_args", "flasher_args.json", "bootloader/bootloader.bin",
                  "bootloader/bootloader.elf", "partition_table/partition-table.bin",
                  "ota_data_initial.bin"]
    for name in files:
        if not (source / name).is_file():
            raise FileNotFoundError(f"Missing native build artifact: {name}")
    destination.mkdir(parents=True, exist_ok=True)
    for name in files:
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, target)

    description = translate(description)
    description["build_dir"] = destination.as_posix()
    description["bootloader_elf"] = (destination / "bootloader/bootloader.elf").as_posix()
    write_json("project_description.json", description)

    environment = translate(json.loads((source / "config.env").read_text(encoding="utf-8")))
    for key, name in [("COMPONENT_KCONFIGS_SOURCE_FILE", "kconfigs.in"),
                      ("COMPONENT_KCONFIGS_PROJBUILD_SOURCE_FILE", "kconfigs_projbuild.in")]:
        (destination / name).write_text(physical((source / name).read_text(encoding="utf-8")),
                                       encoding="utf-8")
        environment[key] = (destination / name).as_posix()
    write_json("config.env", environment)

    commands = json.loads((source / "compile_commands.json").read_text(encoding="utf-8"))
    for entry in commands:
        # Use Windows quoting rules: POSIX shlex would strip compiler backslashes.
        # Argument arrays preserve quoted includes and macro string literals.
        entry["arguments"] = [physical(arg) for arg in split_windows_command(entry.pop("command"))]
        entry["directory"] = physical(entry["directory"])
        entry["file"] = physical(entry["file"])
    write_json("compile_commands.json", commands)
    print("Windows IDE configuration ready." if configuration_only else
          "Zigbee + Wi-Fi AP build complete. Windows IDE artifacts ready.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", required=True, type=Path)
    parser.add_argument("--configuration-only", action="store_true")
    args = parser.parse_args()
    export(args.destination, args.configuration_only)
