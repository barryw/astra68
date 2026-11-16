#!/usr/bin/env python3
"""The certification oracle must reject lost acknowledged bytes and baselines."""
import importlib.util
from pathlib import Path

path = Path(__file__).with_name('test-storage-power-cuts.py')
spec = importlib.util.spec_from_file_location('power_cut_oracle', path)
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)

oracle.verdict('synced', 'EXACT', 'EXACT')
oracle.verdict('written', 'UNACKNOWLEDGED', 'EXACT')
for phase, value, baseline in (
        ('synced', 'ABSENT', 'EXACT'), ('synced', 'PREFIX', 'EXACT'),
        ('synced', 'UNACKNOWLEDGED', 'EXACT'), ('synced', 'EXACT', 'ABSENT'),
        ('written', 'UNACKNOWLEDGED', 'PREFIX'), ('written', 'EXACT', 'EXACT'),
        ('unknown', 'UNACKNOWLEDGED', 'EXACT')):
    try:
        oracle.verdict(phase, value, baseline)
    except RuntimeError:
        pass
    else:
        raise AssertionError((phase, value, baseline))
print('Power-loss oracle: acknowledged data/baseline required; unacknowledged data not promised PASS')
