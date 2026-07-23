use astravm_machine::{AstraMachine, BOOT_TIMEOUT_CYCLES, CPU_HZ};
use std::env;
use std::fs;
use std::process::ExitCode;
use std::time::Instant;

fn reported_cycles(transcript: &str) -> u64 {
    transcript
        .lines()
        .filter_map(|line| {
            line.strip_prefix("K1 SOAK cycles=")?
                .split_once(' ')?
                .0
                .parse::<u64>()
                .ok()
        })
        .max()
        .unwrap_or(0)
}

fn main() -> ExitCode {
    let mut arguments = env::args().skip(1);
    let Some(path) = arguments.next() else {
        eprintln!("usage: soak <astra_boot.bin> [completed-cycles]");
        return ExitCode::FAILURE;
    };
    let target = match arguments.next().as_deref().unwrap_or("1").parse::<u64>() {
        Ok(value) if value != 0 => value,
        _ => {
            eprintln!("completed-cycles must be a nonzero integer");
            return ExitCode::FAILURE;
        }
    };
    if arguments.next().is_some() {
        eprintln!("usage: soak <astra_boot.bin> [completed-cycles]");
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
    if completed >= target && snapshot.kernel_soaking {
        ExitCode::SUCCESS
    } else {
        eprintln!("\nUART transcript:\n{}", machine.serial_transcript());
        ExitCode::FAILURE
    }
}
