#!/usr/bin/env python3

import argparse
from fractions import Fraction
import json
from pathlib import Path


# ECP5/ECP5-5G Family Data Sheet v3.4, table 3.23 (September 2025).
PFD_MIN_HZ = 10_000_000
PFD_MAX_HZ = 400_000_000
VCO_MIN_HZ = 400_000_000
VCO_MAX_HZ = 800_000_000


def yosys_int(value: object, context: str) -> int:
    if isinstance(value, bool):
        raise ValueError(f"{context}: boolean is not an integer")
    if isinstance(value, int):
        return value
    if not isinstance(value, str):
        raise ValueError(f"{context}: unsupported integer value {value!r}")

    encoded = value.strip()
    if len(encoded) == 32 and set(encoded) <= {"0", "1"}:
        return int(encoded, 2)
    try:
        return int(encoded, 0)
    except ValueError as error:
        raise ValueError(f"{context}: invalid integer value {value!r}") from error


def format_hz(value: Fraction) -> str:
    if value.denominator == 1:
        return f"{value.numerator} Hz"
    return f"{float(value):.6f} Hz"


def check_pll_spec(design: dict) -> list[dict]:
    cells = [
        (module_name, cell_name, cell)
        for module_name, module in design.get("modules", {}).items()
        for cell_name, cell in module.get("cells", {}).items()
        if cell.get("type") == "EHXPLLL"
    ]
    if not cells:
        raise ValueError("synthesis JSON contains no EHXPLLL cells")

    measurements = []
    failures = []
    for module_name, cell_name, cell in sorted(cells):
        label = f"{module_name}.{cell_name}"
        parameters = cell.get("parameters", {})
        attributes = cell.get("attributes", {})

        try:
            input_hz = yosys_int(
                attributes["ASTRA_PLL_IN_HZ"], f"{label} input frequency"
            )
            requested = [
                yosys_int(
                    attributes[f"ASTRA_PLL_OUT{index}_HZ"],
                    f"{label} output {index} frequency",
                )
                for index in range(4)
            ]
            tolerances = [
                yosys_int(
                    attributes[f"ASTRA_PLL_OUT{index}_TOL_HZ"],
                    f"{label} output {index} tolerance",
                )
                for index in range(4)
            ]
            ref_div = yosys_int(parameters["CLKI_DIV"], f"{label} CLKI_DIV")
            feedback_div = yosys_int(
                parameters["CLKFB_DIV"], f"{label} CLKFB_DIV"
            )
            primary_div = yosys_int(
                parameters["CLKOP_DIV"], f"{label} CLKOP_DIV"
            )
        except KeyError as error:
            failures.append(f"{label}: missing PLL metadata {error.args[0]}")
            continue
        except ValueError as error:
            failures.append(str(error))
            continue

        if min(input_hz, ref_div, feedback_div, primary_div) <= 0:
            failures.append(f"{label}: input frequency and dividers must be positive")
            continue
        if parameters.get("FEEDBK_PATH", "").strip() != "CLKOP":
            failures.append(f"{label}: only CLKOP feedback is supported")
            continue

        pfd_hz = Fraction(input_hz, ref_div)
        primary_hz = Fraction(input_hz * feedback_div, ref_div)
        vco_hz = primary_hz * primary_div
        if not PFD_MIN_HZ <= pfd_hz <= PFD_MAX_HZ:
            failures.append(
                f"{label}: PFD {format_hz(pfd_hz)} is outside "
                f"{PFD_MIN_HZ}..{PFD_MAX_HZ} Hz"
            )
        if not VCO_MIN_HZ <= vco_hz <= VCO_MAX_HZ:
            failures.append(
                f"{label}: VCO {format_hz(vco_hz)} is outside "
                f"{VCO_MIN_HZ}..{VCO_MAX_HZ} Hz"
            )

        actual = [primary_hz]
        enables = [parameters.get("CLKOP_ENABLE", "").strip()]
        for output_name in ("CLKOS", "CLKOS2", "CLKOS3"):
            try:
                divisor = yosys_int(
                    parameters[f"{output_name}_DIV"],
                    f"{label} {output_name}_DIV",
                )
            except (KeyError, ValueError) as error:
                failures.append(f"{label}: invalid {output_name} divisor: {error}")
                divisor = 0
            actual.append(vco_hz / divisor if divisor > 0 else Fraction(0))
            enables.append(parameters.get(f"{output_name}_ENABLE", "").strip())

        for index, (wanted, tolerance, measured, enabled) in enumerate(
            zip(requested, tolerances, actual, enables)
        ):
            expected_enable = "ENABLED" if wanted > 0 else "DISABLED"
            if enabled != expected_enable:
                failures.append(
                    f"{label}: output {index} is {enabled or 'unspecified'}, "
                    f"expected {expected_enable}"
                )
            if wanted > 0 and abs(measured - wanted) > tolerance:
                failures.append(
                    f"{label}: output {index} is {format_hz(measured)}, "
                    f"requested {wanted} +/- {tolerance} Hz"
                )

        measurements.append(
            {
                "cell": label,
                "pfd_hz": pfd_hz,
                "vco_hz": vco_hz,
                "outputs_hz": actual,
            }
        )

    if failures:
        raise ValueError("PLL specification gate failed:\n  " + "\n  ".join(failures))
    return measurements


def main() -> int:
    parser = argparse.ArgumentParser(
        description="verify synthesized ECP5 PLL operating ranges and outputs"
    )
    parser.add_argument("synthesis_json", type=Path)
    args = parser.parse_args()

    with args.synthesis_json.open(encoding="utf-8") as source:
        design = json.load(source)

    measurements = check_pll_spec(design)
    for measurement in measurements:
        outputs = ", ".join(
            format_hz(frequency) for frequency in measurement["outputs_hz"]
        )
        print(
            f"PLL {measurement['cell']}: PFD "
            f"{format_hz(measurement['pfd_hz'])}, VCO "
            f"{format_hz(measurement['vco_hz'])}, outputs {outputs}"
        )
    print(f"PLL specification: PASS ({len(measurements)} physical PLLs)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
