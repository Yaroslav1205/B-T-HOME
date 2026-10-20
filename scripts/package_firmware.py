"""Prepare a firmware release candidate from an ESP-IDF build (no upload)."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def package(build: Path, output: Path) -> Path:
    description = json.loads((build / "project_description.json").read_text(encoding="utf-8"))
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    if not re.search(r"^BTH_BOARD:STRING=esp32c6_rgb8$", cache, re.M):
        raise ValueError("Only esp32c6_rgb8 is supported")
    if not re.search(r"^BTH_WITH_MATTER:BOOL=OFF$", cache, re.M):
        raise ValueError("Matter packages are not supported")
    if description["target"] != "esp32c6" or description["project_name"] != "bt_home":
        raise ValueError("Unexpected project or target")
    version = description["project_version"]
    if not re.fullmatch(r"(0|[1-9][0-9]{0,5})\.(0|[1-9][0-9]{0,5})\.(0|[1-9][0-9]{0,5})", version):
        raise ValueError("Expected a release version such as 0.1.0")
    if description["app_bin"] != "bt_home.bin":
        raise ValueError("Expected the application binary bt_home.bin")
    data = (build / "bt_home.bin").read_bytes()
    config = (build / "sdkconfig").read_text(encoding="utf-8")
    for setting in ("CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y", "CONFIG_BTH_ZIGBEE_ENABLED=y",
                    "CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y"):
        if setting not in config.splitlines():
            raise ValueError(f"Missing required setting: {setting}")
    partitions = (build / "partition_table/partition-table.bin").read_bytes()
    actual = {}
    for offset in range(0, len(partitions) - 31, 32):
        magic, kind, subtype, address, size, label, flags = struct.unpack_from("<HBBII16sI", partitions, offset)
        if magic != 0x50AA:
            break
        actual[label.rstrip(b"\0").decode("ascii")] = (kind, subtype, address, size, flags)
    expected = {"nvs": (1, 2, 0x9000, 0x6000, 0), "phy_init": (1, 1, 0xf000, 0x1000, 0),
                "ota_0": (0, 16, 0x10000, 0x1e0000, 0), "otadata": (1, 0, 0x1f0000, 0x2000, 0),
                "zb_storage": (1, 2, 0x210000, 0x10000, 0), "ota_1": (0, 17, 0x220000, 0x1e0000, 0)}
    if actual != expected or len(data) > 0x1e0000:
        raise ValueError("Wrong OTA partition layout or image exceeds OTA slot")
    # ESP-IDF v5.4: 24-byte image header, 8-byte segment header, app descriptor.
    if len(data) < 288 or data[0] != 0xE9 or struct.unpack_from("<H", data, 12)[0] != 13:
        raise ValueError("Not an ESP32-C6 application image")
    if struct.unpack_from("<I", data, 32)[0] != 0xABCD5432:
        raise ValueError("Missing application descriptor (bootloader/merged image?)")
    def field(offset):
        return data[offset:offset + 32].split(b"\0", 1)[0].decode("utf-8")
    if field(48) != version or field(80) != "bt_home":
        raise ValueError("Binary version/project differs from build metadata; rebuild first")
    name = f"bt-home-esp32c6_rgb8-{version}.bin"
    digest = hashlib.sha256(data).hexdigest()
    manifest = {
        "schema_version": 1,
        "repository": "Yaroslav1205/B-T-HOME",
        "project": "bt_home",
        "board": "esp32c6_rgb8",
        "target": "esp32c6",
        "variant": "zigbee-wifi",
        "version": version,
        "file": name,
        "size": len(data),
        "sha256": digest,
        "ota_ready": True,
        "layout": "c6-4m-ota-v1",
    }
    # Refuse overwriting a previous candidate or packaging into the build itself.
    output.mkdir(parents=True, exist_ok=False)
    (output / name).write_bytes(data)
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (output / "SHA256SUMS").write_text(f"{digest}  {name}\n", encoding="ascii")
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build/esp32c6_rgb8"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        print(package(args.build_dir, args.output))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"Packaging failed: {error}\n")


if __name__ == "__main__":
    main()
