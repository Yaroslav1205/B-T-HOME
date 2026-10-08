#!/usr/bin/env bash
set -euo pipefail
# ESP-IDF and ESP-Matter must already be exported in Linux/WSL.
: "${IDF_PATH:?Export ESP-IDF v5.4.1 first}"
: "${ESP_MATTER_PATH:?Export ESP-Matter release/v1.4.2 first}"
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ "$project_root" == *' '* ]]; then
    echo 'Copy the project to a Linux path without spaces before building.' >&2
    exit 1
fi
cd "$project_root"
action=${1:-build}
case "$action" in
    build|menuconfig|size|reconfigure) ;;
    *) echo "Unsupported Matter action: $action" >&2; exit 2 ;;
esac
# Keep the Matter product contract current without discarding other local choices.
matter_sdkconfig=build/matter-esp32c6_rgb8/sdkconfig
if [[ -f "$matter_sdkconfig" ]]; then
    for setting in \
        CONFIG_BTH_MATTER_ENABLED=y \
        CONFIG_BTH_ZIGBEE_ENABLED=n \
        CONFIG_SUPPORT_ELECTRICAL_POWER_MEASUREMENT_CLUSTER=y \
        CONFIG_SUPPORT_POWER_TOPOLOGY_CLUSTER=y \
        CONFIG_SUPPORT_FIXED_LABEL_CLUSTER=y \
        CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT=7; do
        key=${setting%%=*}
        if grep -q "^${key}=" "$matter_sdkconfig"; then
            sed -i "s|^${key}=.*|${setting}|" "$matter_sdkconfig"
        elif grep -q "^# ${key} is not set$" "$matter_sdkconfig"; then
            sed -i "s|^# ${key} is not set$|${setting}|" "$matter_sdkconfig"
        else
            printf '%s\n' "$setting" >> "$matter_sdkconfig"
        fi
    done
fi
# Preserve local sdkconfig and reuse the compiler outputs between builds.
idf.py -B build/matter-esp32c6_rgb8 -D BTH_BOARD=esp32c6_rgb8 -D BTH_WITH_MATTER=ON "$action"
