#!/bin/bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
image="${ESP_IDF_IMAGE:-espressif/idf:v5.5.4}"
build_dir=build
docker_extra=()

if [[ -n "${ASTRA_PROVISION_ROM:-}" ]]; then
    provision_dir="$(cd "$(dirname "$ASTRA_PROVISION_ROM")" && pwd)"
    provision_rom="$provision_dir/$(basename "$ASTRA_PROVISION_ROM")"
    [[ -f "$provision_rom" ]] || {
        echo "provisioning ROM not found: $provision_rom" >&2
        exit 1
    }
    build_dir=build-provision
    docker_extra+=(
        -v "$provision_rom:/project/main/astra68_provision.rom:ro"
    )
fi

docker run --rm \
    -e HOME=/tmp \
    -e HOST_UID="$(id -u)" \
    -e HOST_GID="$(id -g)" \
    -e ASTRA_BUILD_DIR="$build_dir" \
    -e ASTRA_FPGA_SPI_HZ="${ASTRA_FPGA_SPI_HZ:-}" \
    -e ASTRA_PROVISION_REPLACE="${ASTRA_PROVISION_REPLACE:-}" \
    -v "$script_dir:/project" \
    "${docker_extra[@]}" \
    -w /project \
    "$image" bash -lc '
        set -euo pipefail
        ffconf="$IDF_PATH/components/fatfs/src/ffconf.h"
        grep -Eq "^#define[[:space:]]+FF_FS_EXFAT[[:space:]]+0$" "$ffconf" || {
            echo "unexpected ESP-IDF FatFs exFAT configuration" >&2
            exit 1
        }
        grep -Eq "^#define[[:space:]]+FF_USE_LABEL[[:space:]]+CONFIG_FATFS_USE_LABEL$" "$ffconf" || {
            echo "unexpected ESP-IDF FatFs volume-label configuration" >&2
            exit 1
        }
        sed -Ei "s/^#define[[:space:]]+FF_FS_EXFAT[[:space:]]+0$/#define FF_FS_EXFAT 1/" "$ffconf"
        sed -Ei "s/^#define[[:space:]]+FF_USE_LABEL[[:space:]]+CONFIG_FATFS_USE_LABEL$/#define FF_USE_LABEL 0/" "$ffconf"
        grep -Eq "^#define[[:space:]]+FF_FS_EXFAT[[:space:]]+1$" "$ffconf"
        grep -Eq "^#define[[:space:]]+FF_USE_LABEL[[:space:]]+0$" "$ffconf"

        cmake_args=()
        if [[ "$ASTRA_BUILD_DIR" == "build-provision" ]]; then
            cmake_args+=("-D" "ASTRA_PROVISION_ROM_ENABLED=ON")
        else
            cmake_args+=("-D" "ASTRA_PROVISION_ROM_ENABLED=OFF")
        fi
        if [[ -n "$ASTRA_FPGA_SPI_HZ" ]]; then
            cmake_args+=("-D" "ASTRA_FPGA_SPI_HZ=$ASTRA_FPGA_SPI_HZ")
        fi
        if [[ "$ASTRA_PROVISION_REPLACE" == "1" ]]; then
            cmake_args+=("-D" "ASTRA_PROVISION_REPLACE=ON")
        else
            cmake_args+=("-D" "ASTRA_PROVISION_REPLACE=OFF")
        fi
        idf.py -B "$ASTRA_BUILD_DIR" "${cmake_args[@]}" build
        chown -R "$HOST_UID:$HOST_GID" "/project/$ASTRA_BUILD_DIR"
        for generated in /project/sdkconfig /project/sdkconfig.old; do
            [ ! -e "$generated" ] || chown "$HOST_UID:$HOST_GID" "$generated"
        done
    '
