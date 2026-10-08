#!/usr/bin/env bash
set -euo pipefail
# Run from the Windows checkout via: wsl -d BTHMatter -- bash scripts/matter-wsl.sh
source_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
linux_root=/root/bth-home
export IDF_TOOLS_PATH=/root/bth-sdk/idf-tools
mkdir -p "$linux_root"
rsync -a --exclude=build --exclude=.git --exclude=.codex --exclude=.agents --exclude=dependencies.lock \
    --exclude=managed_components "$source_root/" "$linux_root/"
set +u
source /root/bth-sdk/esp-idf/export.sh
source /root/bth-sdk/esp-matter/export.sh
set -u
action=${1:-build}
bash "$linux_root/scripts/matter-build.sh" "$action"
# Publish firmware and its matching configuration only after a successful build.
if [[ "$action" != build ]]; then exit 0; fi
echo "Compilation complete. Copying Matter firmware from WSL to Windows..."
output="$source_root/build/matter-esp32c6_rgb8"
mkdir -p "$output/bootloader" "$output/partition_table" "$output/config"
for name in bt_home.bin bt_home.elf bt_home.map sdkconfig flasher_args.json flash_args; do
    echo "Copying $name..."
    cp "$linux_root/build/matter-esp32c6_rgb8/$name" "$output/$name"
done
cp "$linux_root/build/matter-esp32c6_rgb8/bootloader/bootloader.bin" "$output/bootloader/"
cp "$linux_root/build/matter-esp32c6_rgb8/partition_table/partition-table.bin" "$output/partition_table/"
cp "$linux_root/build/matter-esp32c6_rgb8/config/sdkconfig.json" "$output/config/"
echo "Exporting Windows build metadata..."
python "$source_root/scripts/matter-export-description.py" \
    "$linux_root/build/matter-esp32c6_rgb8/project_description.json" \
    "$output/project_description.json" "$(wslpath -w "$source_root")"
echo "Matter firmware copied to $output"
