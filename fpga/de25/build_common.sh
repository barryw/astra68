#!/usr/bin/env bash

prepare_de25_tree() {
    local tree=$1
    rm -rf "$tree"
    mkdir -p "$(dirname "$tree")"
    "$QUARTUS_BIN/quartus_sh" --restore -output "$tree" "$GHRD"
    python3 "$ROOT/fpga/de25/check_platform.py" \
        --quartus-root "$QUARTUS_ROOT" --ghrd "$GHRD" --license "$LICENSE" \
        --restored "$tree"

    # The vendor archive incorrectly stores this required input among outputs.
    mv "$tree/output_files/u-boot-spl-dtb.hex" "$tree/u-boot-spl-dtb.hex"
    sed -i 's|"output_files/u-boot-spl-dtb.hex"|"u-boot-spl-dtb.hex"|' \
        "$tree/golden_top.qsf"
    sed -i '/SOURCE_FILE output_files\/program_qspi_flash/d' \
        "$tree/golden_top.qsf"
    rm -rf "$tree/output_files" "$tree/db" "$tree/dni" \
        "$tree/incremental_db" "$tree/qdb"
}

require_zero_high_severity() {
    local report=$1
    test -s "$report"
    awk -F';' '
        function trim(value) {
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
            return value
        }
        trim($3) == "High" {
            seen = 1
            violations = trim($4)
            if (violations !~ /^[0-9]+$/ || violations != 0) {
                print "high-severity Design Assistant violation: " trim($2) \
                      " (" violations ")" > "/dev/stderr"
                failed = 1
            }
        }
        END {
            if (!seen) {
                print "Design Assistant report has no high-severity rows" \
                      > "/dev/stderr"
                exit 2
            }
            exit failed
        }
    ' "$report"
}

compile_de25_tree() {
    local tree=$1
    (
        cd "$tree"
        LM_LICENSE_FILE="$LICENSE" "$QUARTUS_BIN/quartus_sh" \
            --ip_upgrade -mode all golden_top
        find . -type f -name '*.BAK.*' -delete
        LM_LICENSE_FILE="$LICENSE" "$QUARTUS_BIN/quartus_sh" \
            --flow compile golden_top
        LM_LICENSE_FILE="$LICENSE" "$QUARTUS_BIN/quartus_pfg" -c \
            -o hps_path=u-boot-spl-dtb.hex output_files/golden_top.sof \
            output_files/golden_top_hps.sof
    )

    local fit="$tree/output_files/golden_top.fit.summary"
    local sta="$tree/output_files/golden_top.sta.rpt"
    grep -Fq 'Fitter Status : Successful' "$fit"
    grep -Fq 'Quartus Prime Version : 26.1.1 Build 130 08/06/2026 SC Pro Edition' "$fit"
    grep -Fq 'Device : A5EB013BB23BE4SCS' "$fit"
    grep -Fq 'Design is fully constrained for setup requirements' "$sta"
    grep -Fq 'Design is fully constrained for hold requirements' "$sta"
    grep -Fq 'Timing requirements were met' "$sta"
    require_zero_high_severity \
        "$tree/output_files/golden_top.tq.drc.signoff.rpt"
    test -s "$tree/output_files/golden_top.sof"
    test -s "$tree/output_files/golden_top_hps.sof"
}

record_de25_build() {
    local tree=$1
    shift
    (
        cd "$tree"
        sha256sum "$GHRD" u-boot-spl-dtb.hex "$@" \
            output_files/golden_top.sof output_files/golden_top_hps.sof \
            >BUILD_SHA256SUMS
        sha256sum -c BUILD_SHA256SUMS
    )
}

publish_de25_tree() {
    local incoming=$1 out=$2
    rm -rf "$out"
    mv "$incoming" "$out"
    (cd "$out" && sha256sum -c BUILD_SHA256SUMS)
    cat "$out/BUILD_SHA256SUMS"
}
