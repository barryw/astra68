use astravm_machine::{AstraMachine, BOOT_TIMEOUT_CYCLES};
use std::env;
use std::fs;
use std::process::ExitCode;

fn main() -> ExitCode {
    let Some(path) = env::args().nth(1) else {
        eprintln!("usage: kernel <astra_boot.bin>");
        return ExitCode::FAILURE;
    };
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

    while !machine.snapshot().kernel_ready
        && !machine.snapshot().kernel_panicked
        && !machine.snapshot().post_failed
        && machine.snapshot().cycles < BOOT_TIMEOUT_CYCLES
    {
        machine.advance(250_000);
    }

    let snapshot = machine.snapshot();
    print!("{}", machine.serial_transcript());
    eprintln!(
        "backend={} cycles={} pc=0x{:08X} build=0x{:08X} scratch=0x{:08X}",
        snapshot.backend, snapshot.cycles, snapshot.cpu_pc, snapshot.build_id, snapshot.scratch
    );
    if snapshot.kernel_ready {
        ExitCode::SUCCESS
    } else {
        eprintln!("scratch trace: {:08X?}", snapshot.scratch_trace);
        ExitCode::FAILURE
    }
}
