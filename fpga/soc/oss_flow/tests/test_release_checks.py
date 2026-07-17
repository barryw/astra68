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

    def test_rejects_a_timing_miss(self):
        report = self.passing_report()
        report["fmax"]["$glbnet$sdram_domain_clk"] = {
            "achieved": 71.56,
            "constraint": 75.01,
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
            "achieved": 74.0,
            "constraint": 50.0,
        }
        failures, _ = check_timing.check_fmax(report)
        self.assertEqual(len(failures), 2)
        self.assertTrue(all("sdram_domain_clk" in failure for failure in failures))


if __name__ == "__main__":
    unittest.main()
