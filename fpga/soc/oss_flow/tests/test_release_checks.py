#!/usr/bin/env python3

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


FLOW_DIR = Path(__file__).resolve().parents[1]


def load_module(name: str):
    spec = importlib.util.spec_from_file_location(name, FLOW_DIR / f"{name}.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


check_timing = load_module("check_timing")
prepare_route_input = load_module("prepare_route_input")
make_route_probe_bitstream = load_module("make_route_probe_bitstream")
check_por = load_module("check_por")
check_pll_spec = load_module("check_pll_spec")
check_post_font_rom = load_module("check_post_font_rom")
check_ecp5_lut_permutation = load_module("check_ecp5_lut_permutation")
refresh_ecp5_lutperm = load_module("refresh_ecp5_lutperm")


class SynthesisFlowTests(unittest.TestCase):
    def test_device_primitives_are_loaded_before_optimization(self):
        flow = (FLOW_DIR / "mkbit.sh").read_text(encoding="utf-8")
        synth = flow.index("synth_ecp5 -top astra_soc")
        retained_gsr = flow.index(
            'setparam -set GSR \\"ENABLED\\" t:TRELLIS_FF', synth
        )
        mapped_scc = flow.index("scc -select", synth)

        self.assertNotIn("proc; opt", flow[:synth])
        self.assertGreater(retained_gsr, synth)
        self.assertLess(retained_gsr, mapped_scc)
        self.assertGreater(mapped_scc, synth)

    def test_split_route_refreshes_and_validates_lut_permutations(self):
        flow = (FLOW_DIR / "mkbit.sh").read_text(encoding="utf-8")
        route = flow.index("--pre-route refresh_ecp5_lutperm.py")
        validate = flow.index('python3 check_ecp5_lut_permutation.py "$ROUTED_JSON"')
        package = flow.index('ecppack astra.config "$BITSTREAM_TMP"')

        self.assertLess(route, validate)
        self.assertLess(validate, package)

    def test_synthesis_checks_exact_post_font_mapping(self):
        flow = (FLOW_DIR / "mkbit.sh").read_text(encoding="utf-8")
        mapped_json = flow.index("write_json astra.json")
        font_gate = flow.index("python3 check_post_font_rom.py astra.json")
        synth_only = flow.index('if [ "${SYNTH_ONLY:-0}" = "1" ]')

        self.assertLess(mapped_json, font_gate)
        self.assertLess(font_gate, synth_only)

    def test_synthesis_checks_physical_pll_specification(self):
        flow = (FLOW_DIR / "mkbit.sh").read_text(encoding="utf-8")
        mapped_json = flow.index("write_json astra.json")
        pll_gate = flow.index("python3 check_pll_spec.py astra.json")
        synth_only = flow.index('if [ "${SYNTH_ONLY:-0}" = "1" ]')

        self.assertLess(mapped_json, pll_gate)
        self.assertLess(pll_gate, synth_only)


class PostFontRomGateTests(unittest.TestCase):
    @staticmethod
    def synthesis(*, width=9, constant_pin=None, duplicate_pin=None, count=1):
        connections = {
            "ADA0": ["0"],
            "ADA1": ["0"],
            "ADA2": ["0"],
            **{f"ADA{bit}": [100 + bit] for bit in range(3, 14)},
        }
        if constant_pin is not None:
            connections[constant_pin] = ["0"]
        if duplicate_pin is not None:
            connections[duplicate_pin] = connections["ADA3"]
        cells = {
            f"{check_post_font_rom.FONT_CELL_PREFIX}{index}.0": {
                "type": "DP16KD",
                "parameters": {"DATA_WIDTH_A": f"{width:032b}"},
                "connections": connections,
            }
            for index in range(count)
        }
        return {"modules": {"astra_soc": {"cells": cells}}}

    def test_accepts_one_exact_depth_font_block(self):
        name = check_post_font_rom.check_post_font_rom(self.synthesis())
        self.assertEqual(name, f"{check_post_font_rom.FONT_CELL_PREFIX}0.0")

    def test_rejects_multiple_font_blocks(self):
        with self.assertRaisesRegex(ValueError, "exactly one physical block RAM"):
            check_post_font_rom.check_post_font_rom(self.synthesis(count=4))

    def test_rejects_constant_logical_address_pin(self):
        with self.assertRaisesRegex(ValueError, "constant logical address pin ADA13"):
            check_post_font_rom.check_post_font_rom(
                self.synthesis(constant_pin="ADA13")
            )

    def test_rejects_duplicate_logical_address_bits(self):
        with self.assertRaisesRegex(ValueError, "11 unique address bits"):
            check_post_font_rom.check_post_font_rom(
                self.synthesis(duplicate_pin="ADA13")
            )

    def test_rejects_wrong_physical_width(self):
        with self.assertRaisesRegex(ValueError, "DATA_WIDTH_A=18, not 9"):
            check_post_font_rom.check_post_font_rom(self.synthesis(width=18))


class LutPermutationRefreshTests(unittest.TestCase):
    class Cell:
        def __init__(self, cell_type, bel, strength):
            self.type = cell_type
            self.bel = bel
            self.belStrength = strength

    class Context:
        def __init__(self, cells):
            self.cells = list(cells.items())
            self.calls = []

        def unbindBel(self, bel):
            self.calls.append(("unbind", bel))

        def bindBel(self, bel, cell, strength):
            self.calls.append(("bind", bel, cell, strength))

    def test_rebinds_only_placed_comb_cells(self):
        carry = self.Cell("TRELLIS_COMB", "X1/Y2/SLICEA.K0", 1)
        ff = self.Cell("TRELLIS_FF", "X1/Y2/SLICEA.FF0", 2)
        unplaced = self.Cell("TRELLIS_COMB", "", 3)
        context = self.Context({"carry": carry, "ff": ff, "unplaced": unplaced})

        count = refresh_ecp5_lutperm.refresh_lut_permutation_policy(context)

        self.assertEqual(count, 1)
        self.assertEqual(
            context.calls,
            [
                ("unbind", "X1/Y2/SLICEA.K0"),
                ("bind", "X1/Y2/SLICEA.K0", carry, 1),
            ],
        )


class Ecp5LutPermutationGateTests(unittest.TestCase):
    @staticmethod
    def design(mode="CCU2", source="B", destination="A"):
        route = (
            f"X1/Y2/{source}0;"
            f"X1/Y2/0_0_{source}0->0_0_{destination}0_SLICE;1"
        )
        return {
            "modules": {
                "top": {
                    "cells": {
                        "protected": {
                            "type": "TRELLIS_COMB",
                            "parameters": {"MODE": mode},
                            "attributes": {"NEXTPNR_BEL": "X1/Y2/SLICEA.K0"},
                            "connections": {destination: [7]},
                        }
                    },
                    "netnames": {
                        "signal": {"bits": [7], "attributes": {"ROUTING": route}}
                    },
                }
            }
        }

    def test_accepts_ccu2_permutation_within_input_pair(self):
        cells, inputs, failures = check_ecp5_lut_permutation.check_lut_permutations(
            self.design(source="B", destination="A")
        )
        self.assertEqual((cells, inputs), (1, 1))
        self.assertEqual(failures, [])

    def test_rejects_ccu2_permutation_across_input_pairs(self):
        _, _, failures = check_ecp5_lut_permutation.check_lut_permutations(
            self.design(source="D", destination="A")
        )
        self.assertEqual(len(failures), 1)
        self.assertIn("invalid CCU2 permutation D0->A0", failures[0])

    def test_rejects_any_distributed_ram_permutation(self):
        _, _, failures = check_ecp5_lut_permutation.check_lut_permutations(
            self.design(mode="DPRAM", source="B", destination="A")
        )
        self.assertEqual(len(failures), 1)
        self.assertIn("invalid DPRAM permutation B0->A0", failures[0])


class PrepareRouteInputTests(unittest.TestCase):
    def test_clears_waiver_and_serialized_router_controls(self):
        design = {
            "modules": {
                "top": {
                    "settings": {
                        "timing/allowFail": "0" * 31 + "1",
                        "target_freq": "12500000.000000",
                        "router": "router1",
                        "router/tmg_ripup": "0 ",
                        "router2/alt-weights": "0 ",
                        "seed": "1" * 64,
                    },
                    "cells": {"retained": {"type": "LUT4"}},
                }
            }
        }
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "placed.json"
            destination = Path(directory) / "route.json"
            source.write_text(json.dumps(design), encoding="utf-8")

            previous, removed = prepare_route_input.prepare_route_input(
                source, destination
            )
            routed = json.loads(destination.read_text(encoding="utf-8"))

        self.assertEqual(previous, "0" * 31 + "1")
        self.assertEqual(
            set(removed), set(prepare_route_input.SERIALIZED_ROUTER_CONTROLS)
        )
        self.assertEqual(
            routed["modules"]["top"]["settings"]["timing/allowFail"],
            "0" * 32,
        )
        self.assertTrue(
            set(prepare_route_input.SERIALIZED_ROUTER_CONTROLS).isdisjoint(
                routed["modules"]["top"]["settings"]
            )
        )
        self.assertEqual(
            routed["modules"]["top"]["settings"]["seed"], "1" * 64
        )
        self.assertEqual(
            routed["modules"]["top"]["cells"], design["modules"]["top"]["cells"]
        )

    def test_diagnostic_preserves_waiver_and_removes_router_controls(self):
        waiver = "0" * 31 + "1"
        design = {
            "modules": {
                "top": {
                    "settings": {
                        "timing/allowFail": waiver,
                        "router": "router1",
                        "seed": "1" * 64,
                    }
                }
            }
        }
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "placed.json"
            destination = Path(directory) / "route.json"
            source.write_text(json.dumps(design), encoding="utf-8")

            _, removed = prepare_route_input.prepare_route_input(
                source, destination, clear_timing_waiver=False
            )
            routed = json.loads(destination.read_text(encoding="utf-8"))

        self.assertEqual(removed, {"router": "router1"})
        self.assertEqual(
            routed["modules"]["top"]["settings"]["timing/allowFail"], waiver
        )
        self.assertEqual(
            routed["modules"]["top"]["settings"]["seed"], "1" * 64
        )


class TimingGateTests(unittest.TestCase):
    def passing_report(self):
        return {
            "fmax": {
                clock: {"achieved": required + 10.0, "constraint": required}
                for clock, required in check_timing.REQUIRED_CLOCKS.items()
            }
        }

    def test_accepts_all_required_clocks(self):
        failures, measurements = check_timing.check_fmax(self.passing_report())
        self.assertEqual(failures, [])
        self.assertEqual(len(measurements), len(check_timing.REQUIRED_CLOCKS))

    def test_uses_canonical_nextpnr_clock_names(self):
        self.assertEqual(
            check_timing.REQUIRED_CLOCKS,
            {
                "$glbnet$clk": 12.5,
                "$glbnet$clk25_mhz$TRELLIS_IO_IN": 25.0,
                "$glbnet$sd_clk_in": 20.0,
                "$glbnet$sdram_domain_clk": 60.0,
                "$glbnet$usb_phy_domain_clk": 48.0,
                "$glbnet$video_pixel_clk": 27.0,
                "$glbnet$video_shift_clk": 135.0,
            },
        )

    def test_rejects_a_timing_miss(self):
        report = self.passing_report()
        report["fmax"]["$glbnet$sdram_domain_clk"] = {
            "achieved": 59.56,
            "constraint": 60.01,
        }
        failures, _ = check_timing.check_fmax(report)
        self.assertEqual(len(failures), 1)
        self.assertIn("sdram_domain_clk", failures[0])

    def test_rejects_a_missing_required_clock(self):
        report = self.passing_report()
        del report["fmax"]["$glbnet$clk"]
        failures, _ = check_timing.check_fmax(report)
        self.assertEqual(len(failures), 1)
        self.assertIn("missing", failures[0])

    def test_rejects_a_weakened_report_constraint(self):
        report = self.passing_report()
        report["fmax"]["$glbnet$sdram_domain_clk"] = {
            "achieved": 59.0,
            "constraint": 50.0,
        }
        failures, _ = check_timing.check_fmax(report)
        self.assertEqual(len(failures), 2)
        self.assertTrue(all("sdram_domain_clk" in failure for failure in failures))


class PowerOnResetGateTests(unittest.TestCase):
    @staticmethod
    def synthesis(*gsr_values: str):
        return {
            "modules": {
                "astra_soc": {
                    "cells": {
                        "global_gsr": {"type": "GSR"},
                        **{
                            f"ff{index}": {
                                "type": "TRELLIS_FF",
                                "parameters": {"GSR": value},
                            }
                            for index, value in enumerate(gsr_values)
                        },
                    }
                }
            }
        }

    def test_accepts_configuration_reset_on_every_flip_flop(self):
        design = self.synthesis("ENABLED", "ENABLED")
        self.assertEqual(check_por.check_por(design), 2)

    def test_rejects_any_flip_flop_without_configuration_reset(self):
        design = self.synthesis("ENABLED", "DISABLED")
        with self.assertRaisesRegex(ValueError, "1 of 2 flip-flops"):
            check_por.check_por(design)

    def test_rejects_unsafe_flip_flop_in_preserved_submodule(self):
        design = self.synthesis("ENABLED")
        design["modules"]["reset_release"] = {
            "cells": {
                "release_ff": {
                    "type": "TRELLIS_FF",
                    "parameters": {"GSR": "DISABLED"},
                }
            }
        }
        with self.assertRaisesRegex(
            ValueError, "reset_release.release_ff"
        ):
            check_por.check_por(design)

    def test_rejects_multiple_physical_gsr_primitives(self):
        design = self.synthesis("ENABLED")
        design["modules"]["reset_release"] = {
            "cells": {"local_gsr": {"type": "GSR"}}
        }
        with self.assertRaisesRegex(ValueError, "exactly one physical GSR"):
            check_por.check_por(design)

    def test_rejects_only_gsr_inside_repeated_submodule(self):
        design = self.synthesis("ENABLED")
        del design["modules"]["astra_soc"]["cells"]["global_gsr"]
        design["modules"]["reset_release"] = {
            "cells": {"local_gsr": {"type": "GSR"}}
        }
        with self.assertRaisesRegex(ValueError, "top module 'astra_soc'"):
            check_por.check_por(design)

    def test_rejects_design_without_packed_flip_flops(self):
        with self.assertRaisesRegex(ValueError, "no TRELLIS_FF"):
            check_por.check_por(self.synthesis())


class PllSpecificationGateTests(unittest.TestCase):
    @staticmethod
    def pll_cell(
        *,
        input_hz=25_000_000,
        requested=(50_000_000, 60_000_000, 100_000_000, 0),
        ref_div=1,
        feedback_div=2,
        primary_div=12,
        secondary_divs=(10, 6, 1),
    ):
        def encoded(value):
            return f"{value:032b}"

        return {
            "type": "EHXPLLL",
            "attributes": {
                "ASTRA_PLL_IN_HZ": encoded(input_hz),
                **{
                    f"ASTRA_PLL_OUT{index}_HZ": encoded(frequency)
                    for index, frequency in enumerate(requested)
                },
                **{
                    f"ASTRA_PLL_OUT{index}_TOL_HZ": encoded(0)
                    for index in range(4)
                },
            },
            "parameters": {
                "CLKI_DIV": encoded(ref_div),
                "CLKFB_DIV": encoded(feedback_div),
                "CLKOP_DIV": encoded(primary_div),
                "CLKOP_ENABLE": "ENABLED",
                "FEEDBK_PATH": "CLKOP",
                **{
                    f"{name}_DIV": encoded(divisor)
                    for name, divisor in zip(
                        ("CLKOS", "CLKOS2", "CLKOS3"), secondary_divs
                    )
                },
                **{
                    f"{name}_ENABLE": (
                        "ENABLED" if requested[index] else "DISABLED"
                    )
                    for index, name in enumerate(
                        ("CLKOS", "CLKOS2", "CLKOS3"), start=1
                    )
                },
            },
        }

    def design(self, cell=None):
        return {
            "modules": {
                "astra_soc": {
                    "cells": {"pll": cell if cell is not None else self.pll_cell()}
                }
            }
        }

    def test_accepts_in_spec_exact_outputs(self):
        measurements = check_pll_spec.check_pll_spec(self.design())
        self.assertEqual(len(measurements), 1)
        self.assertEqual(measurements[0]["pfd_hz"], 25_000_000)
        self.assertEqual(measurements[0]["vco_hz"], 600_000_000)

    def test_rejects_project_trellis_low_pfd_limit(self):
        cell = self.pll_cell(
            requested=(60_000_000, 100_000_000, 0, 0),
            ref_div=5,
            feedback_div=12,
            primary_div=10,
            secondary_divs=(6, 1, 1),
        )
        with self.assertRaisesRegex(ValueError, "PFD 5000000 Hz"):
            check_pll_spec.check_pll_spec(self.design(cell))

    def test_rejects_wrong_output_divider(self):
        cell = self.pll_cell(secondary_divs=(10, 5, 1))
        with self.assertRaisesRegex(ValueError, "output 2 is 120000000 Hz"):
            check_pll_spec.check_pll_spec(self.design(cell))

    def test_rejects_missing_generator_metadata(self):
        cell = self.pll_cell()
        del cell["attributes"]["ASTRA_PLL_IN_HZ"]
        with self.assertRaisesRegex(ValueError, "missing PLL metadata"):
            check_pll_spec.check_pll_spec(self.design(cell))


class RouteProbeBitstreamTests(unittest.TestCase):
    @staticmethod
    def synthesis(init: str, *, width: str = "18"):
        return {
            "modules": {
                "astra_soc": {
                    "ports": {"clock": {"bits": [1]}},
                    "cells": {
                        "rom.0.0": {
                            "type": "DP16KD",
                            "connections": {"CLKA": [1]},
                            "parameters": {
                                "DATA_WIDTH_A": width,
                                "INITVAL_00": init,
                            },
                        }
                    },
                }
            }
        }

    @staticmethod
    def config(blocks: dict[int, str], route: str) -> str:
        sections = ""
        for block_id, value in blocks.items():
            row = f"{value} {value} {value} {value} {value} {value} {value} {value}\n"
            sections += f".bram_init {block_id}\n" + row * make_route_probe_bitstream.BRAM_ROWS + "\n"
        return route + sections

    def test_accepts_initializer_only_synthesis_change(self):
        changes = make_route_probe_bitstream.find_init_changes(
            self.synthesis("0" * 320), self.synthesis("1" * 320), "rom."
        )
        self.assertEqual(set(changes), {"rom.0.0"})

    def test_rejects_topology_change(self):
        with self.assertRaisesRegex(ValueError, "non-INIT parameters"):
            make_route_probe_bitstream.find_init_changes(
                self.synthesis("0" * 320),
                self.synthesis("1" * 320, width="9"),
                "rom.",
            )

    def test_patches_only_changed_bram_section(self):
        original = self.config({3: "000", 4: "111"}, "arc: KEEP ORIGINAL\n")
        replacement = self.config({3: "aaa", 4: "111"}, "arc: REEMITTED\n")
        patched, changed = make_route_probe_bitstream.patch_bram_blocks(
            original, replacement, expected_changes=1
        )

        self.assertEqual(changed, [3])
        self.assertIn("arc: KEEP ORIGINAL", patched)
        self.assertNotIn("arc: REEMITTED", patched)
        self.assertIn("aaa aaa aaa aaa aaa aaa aaa aaa", patched)
        self.assertIn("111 111 111 111 111 111 111 111", patched)

    def test_rejects_unexpected_bram_change_count(self):
        original = self.config({3: "000", 4: "111"}, "")
        replacement = self.config({3: "aaa", 4: "bbb"}, "")
        with self.assertRaisesRegex(ValueError, "expected 1"):
            make_route_probe_bitstream.patch_bram_blocks(
                original, replacement, expected_changes=1
            )


if __name__ == "__main__":
    unittest.main()
