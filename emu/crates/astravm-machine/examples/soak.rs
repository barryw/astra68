use astravm_machine::{AstraMachine, BOOT_TIMEOUT_CYCLES, CPU_HZ};
use std::env;
use std::fs;
use std::process::ExitCode;
use std::time::Instant;

fn reported_cycle(line: &str) -> Option<u64> {
    let line = line.strip_suffix('\r').unwrap_or(line);
    line.strip_prefix("K2 PERFORMANCE SOAK PASS cycle=")?
        .parse::<u64>()
        .ok()
}

fn reported_cycles(transcript: &str) -> u64 {
    transcript
        .split_inclusive('\n')
        .filter_map(|line| reported_cycle(line.strip_suffix('\n')?))
        .max()
        .unwrap_or(0)
}

fn within_cycle_budget(cycles: u64, maximum_cycles: Option<u64>) -> bool {
    maximum_cycles.is_none_or(|limit| cycles <= limit)
}

#[cfg(test)]
mod tests {
    use super::{reported_cycle, reported_cycles, within_cycle_budget};

    const COMPLETE: &str = "K2 PERFORMANCE SOAK PASS cycle=1000";

    #[test]
    fn rejects_partial_serial_lines() {
        assert_eq!(reported_cycles("K2 PERFORMANCE SOAK PASS cycle=6"), 0);
        assert_eq!(reported_cycle("K2 PERFORMANCE SOAK PASS cycle="), None);
    }

    #[test]
    fn accepts_only_complete_checkpoints() {
        assert_eq!(reported_cycle(COMPLETE), Some(1000));
        assert_eq!(reported_cycle(&format!("{COMPLETE}\r")), Some(1000));
        assert_eq!(
            reported_cycles(&format!("K2 PERFORMANCE SOAK PASS cycle=10\n{COMPLETE}\n")),
            1000
        );
    }

    #[test]
    fn cycle_budget_is_optional_and_inclusive() {
        assert!(within_cycle_budget(101, None));
        assert!(within_cycle_budget(100, Some(100)));
        assert!(!within_cycle_budget(101, Some(100)));
    }
}

fn main() -> ExitCode {
    let mut arguments = env::args().skip(1);
    let Some(path) = arguments.next() else {
        eprintln!("usage: soak <astra_boot.bin> [completed-cycles] [maximum-machine-cycles]");
        return ExitCode::FAILURE;
    };
    let target = match arguments.next().as_deref().unwrap_or("1").parse::<u64>() {
        Ok(value) if value != 0 => value,
        _ => {
            eprintln!("completed-cycles must be a nonzero integer");
            return ExitCode::FAILURE;
        }
    };
    let maximum_cycles = match arguments.next() {
        Some(value) => match value.parse::<u64>() {
            Ok(value) if value != 0 => Some(value),
            _ => {
                eprintln!("maximum-machine-cycles must be a nonzero integer");
                return ExitCode::FAILURE;
            }
        },
        None => None,
    };
    if arguments.next().is_some() {
        eprintln!("usage: soak <astra_boot.bin> [completed-cycles] [maximum-machine-cycles]");
        return ExitCode::FAILURE;
    }

    let rom = match fs::read(&path) {
        Ok(rom) => rom,
        Err(error) => {
            eprintln!("cannot read ROM '{path}': {error}");
            return ExitCode::FAILURE;
        }
    };
    let mut machine = match AstraMachine::try_with_rom(&rom) {
        Ok(machine) => machine,
        Err(error) => {
            eprintln!("invalid ROM '{path}': {error}");
            return ExitCode::FAILURE;
        }
    };
    let timeout = BOOT_TIMEOUT_CYCLES.saturating_add(target.saturating_mul(CPU_HZ));
    let started = Instant::now();
    let mut completed = 0;
    while completed < target
        && !machine.snapshot().kernel_panicked
        && !machine.snapshot().post_failed
        && machine.snapshot().cycles < timeout
        && within_cycle_budget(machine.snapshot().cycles, maximum_cycles)
    {
        machine.advance(250_000);
        let observed = reported_cycles(&machine.serial_transcript());
        if observed > completed {
            completed = observed;
            let snapshot = machine.snapshot();
            eprintln!(
                "soak progress={completed}/{target} cycles={} wall={:.3}s",
                snapshot.cycles,
                started.elapsed().as_secs_f64()
            );
        }
    }

    let snapshot = machine.snapshot();
    print!("{}", machine.serial_transcript());
    eprintln!(
        "backend={} cycles={} pc=0x{:08X} build=0x{:08X} soak={completed}/{target}",
        snapshot.backend, snapshot.cycles, snapshot.cpu_pc, snapshot.build_id
    );
    let performance_pass = within_cycle_budget(snapshot.cycles, maximum_cycles);
    if !performance_pass {
        eprintln!(
            "performance regression: {} machine cycles exceeds maximum {}",
            snapshot.cycles,
            maximum_cycles.unwrap_or_default()
        );
    }
    if completed >= target && snapshot.kernel_soaking && performance_pass {
        ExitCode::SUCCESS
    } else {
        eprintln!("\nUART transcript:\n{}", machine.serial_transcript());
        ExitCode::FAILURE
    }
}
