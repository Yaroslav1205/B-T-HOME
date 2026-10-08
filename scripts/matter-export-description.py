"""Translate Linux build metadata for the Windows ESP-IDF toolbar."""
import json
import sys
from pathlib import Path


def export_description(source, destination, windows_root):
    root = windows_root.replace("\\", "/")

    def translate(value):
        if isinstance(value, str):
            return value.replace("/root/bth-home", root).replace(
                "/root/bth-sdk", "//wsl.localhost/BTHMatter/root/bth-sdk"
            )
        if isinstance(value, list):
            return [translate(item) for item in value]
        if isinstance(value, dict):
            return {key: translate(item) for key, item in value.items()}
        return value

    description = translate(json.loads(Path(source).read_text(encoding="utf-8")))
    description["monitor_tool_prefix"] = "riscv32-esp-elf-"
    Path(destination).write_text(
        json.dumps(description, indent=2) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    export_description(*sys.argv[1:])
