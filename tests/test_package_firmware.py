"""Release packaging checks, independent of ESP-IDF."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "package_firmware", Path(__file__).resolve().parents[1] / "scripts/package_firmware.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.output = self.root / "package"
        (self.root / "CMakeCache.txt").write_text(
            "BTH_BOARD:STRING=esp32c6_rgb8\nBTH_WITH_MATTER:BOOL=OFF\n")
        (self.root / "project_description.json").write_text(json.dumps({
            "target": "esp32c6", "project_name": "bt_home",
            "project_version": "0.1.0", "app_bin": "bt_home.bin"}))
        self.data = bytearray(288)
        self.data[0] = 0xE9
        struct.pack_into("<H", self.data, 12, 13)
        struct.pack_into("<I", self.data, 32, 0xABCD5432)
        self.data[48:53] = b"0.1.0"
        self.data[80:87] = b"bt_home"
        self.binary = self.root / "bt_home.bin"
        self.binary.write_bytes(self.data)
        (self.root / "sdkconfig").write_text("CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y\n"
            "CONFIG_BTH_ZIGBEE_ENABLED=y\nCONFIG_ESPTOOLPY_FLASHSIZE_4MB=y\n")
        (self.root / "partition_table").mkdir()
        entries = [("nvs", 1, 2, 0x9000, 0x6000), ("phy_init", 1, 1, 0xf000, 0x1000),
                   ("ota_0", 0, 16, 0x10000, 0x1e0000), ("otadata", 1, 0, 0x1f0000, 0x2000),
                   ("zb_storage", 1, 2, 0x210000, 0x10000), ("ota_1", 0, 17, 0x220000, 0x1e0000)]
        self.table = self.root / "partition_table/partition-table.bin"
        self.table.write_bytes(b"".join(struct.pack("<HBBII16sI", 0x50aa, kind, sub, addr, size,
            label.encode(), 0) for label, kind, sub, addr, size in entries))

    def test_package_integrity_and_no_overwrite(self):
        module.package(self.root, self.output)
        manifest = json.loads((self.output / "manifest.json").read_text())
        self.assertEqual((self.output / manifest["file"]).read_bytes(), self.data)
        self.assertEqual(manifest["sha256"], hashlib.sha256(self.data).hexdigest())
        self.assertEqual(manifest["size"], len(self.data))
        self.assertTrue(manifest["ota_ready"])
        with self.assertRaises(FileExistsError):
            module.package(self.root, self.output)

    def test_reject_wrong_image_and_stale_version(self):
        for offset, value in ((0, 0), (12, 5), (32, 0), (48, ord("9"))):
            with self.subTest(offset=offset):
                corrupt = bytearray(self.data)
                corrupt[offset] = value
                self.binary.write_bytes(corrupt)
                with self.assertRaises(ValueError):
                    module.package(self.root, self.output)
                self.assertFalse(self.output.exists())

    def test_reject_other_profile_or_matter(self):
        for cache in ("BTH_BOARD:STRING=generic_esp32c6\nBTH_WITH_MATTER:BOOL=OFF\n",
                      "BTH_BOARD:STRING=esp32c6_rgb8\nBTH_WITH_MATTER:BOOL=ON\n"):
            (self.root / "CMakeCache.txt").write_text(cache)
            with self.assertRaises(ValueError):
                module.package(self.root, self.output)

    def test_reject_missing_rollback_and_wrong_layout(self):
        config = self.root / "sdkconfig"
        saved = config.read_text()
        config.write_text(saved.replace("CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y", ""))
        with self.assertRaises(ValueError):
            module.package(self.root, self.output)
        config.write_text(saved)
        self.table.write_bytes(b"\xff" * 32)
        with self.assertRaises(ValueError):
            module.package(self.root, self.output)


if __name__ == "__main__":
    unittest.main()
