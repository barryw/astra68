use astravm_machine::{AstraMachine, BOOT_TIMEOUT_CYCLES};
use std::env;
use std::fs;
use std::process::ExitCode;

fn main() -> ExitCode {
    let mut machine = match env::args().nth(1) {
        Some(path) => {
            let rom = match fs::read(&path) {
                Ok(rom) => rom,
                Err(error) => {
                    eprintln!("cannot read ROM '{path}': {error}");
                    return ExitCode::FAILURE;
                }
            };
            match AstraMachine::try_with_rom(&rom) {
                Ok(machine) => machine,
                Err(error) => {
                    eprintln!("invalid ROM '{path}': {error}");
                    return ExitCode::FAILURE;
                }
            }
        }
        None => AstraMachine::new(),
    };

    while !machine.snapshot().ready_for_loader
        && !machine.snapshot().kernel_ready
        && !machine.snapshot().post_failed
        && !machine.snapshot().kernel_panicked
        && machine.snapshot().cycles < BOOT_TIMEOUT_CYCLES
    {
        machine.advance(250_000);
    }

    let snapshot = machine.snapshot();
    print!("{}", machine.console_transcript());
    eprintln!(
        "backend={} cycles={} pc=0x{:08X} build=0x{:08X}",
        snapshot.backend, snapshot.cycles, snapshot.cpu_pc, snapshot.build_id
    );

    if snapshot.ready_for_loader || snapshot.kernel_ready {
        ExitCode::SUCCESS
    } else {
        eprintln!("\nUART transcript:\n{}", machine.serial_transcript());
        ExitCode::FAILURE
    }
}
