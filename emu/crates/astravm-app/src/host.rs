use astravm_machine::{AstraMachine, CPU_HZ, MachineSnapshot};
use std::env;
use std::fs;
use std::sync::mpsc::{Receiver, SyncSender, TryRecvError, sync_channel};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

const SNAPSHOT_PERIOD: Duration = Duration::from_millis(16);
const MAX_HOST_DELTA: Duration = Duration::from_millis(100);

enum MachineCommand {
    Reset,
    SetPaused(bool),
    Shutdown,
}

pub struct MachineHost {
    commands: SyncSender<MachineCommand>,
    snapshots: Receiver<MachineSnapshot>,
    worker: Option<JoinHandle<()>>,
}

impl MachineHost {
    pub fn spawn() -> Self {
        let (command_tx, command_rx) = sync_channel(16);
        let (snapshot_tx, snapshot_rx) = sync_channel(2);
        let worker = thread::Builder::new()
            .name("astravm-machine".to_owned())
            .spawn(move || machine_thread(command_rx, snapshot_tx))
            .expect("failed to start AstraVM machine thread");

        Self {
            commands: command_tx,
            snapshots: snapshot_rx,
            worker: Some(worker),
        }
    }

    pub fn reset(&self) {
        let _ = self.commands.try_send(MachineCommand::Reset);
    }

    pub fn set_paused(&self, paused: bool) {
        let _ = self.commands.try_send(MachineCommand::SetPaused(paused));
    }

    pub fn latest_snapshot(&self) -> Option<MachineSnapshot> {
        let mut latest = None;
        loop {
            match self.snapshots.try_recv() {
                Ok(snapshot) => latest = Some(snapshot),
                Err(TryRecvError::Empty | TryRecvError::Disconnected) => return latest,
            }
        }
    }
}

impl Drop for MachineHost {
    fn drop(&mut self) {
        let _ = self.commands.send(MachineCommand::Shutdown);
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}

fn machine_thread(commands: Receiver<MachineCommand>, snapshots: SyncSender<MachineSnapshot>) {
    let mut machine = load_machine();
    let mut last_host_tick = Instant::now();
    let mut next_snapshot = last_host_tick;
    let mut fractional_cycles = 0_u128;

    let _ = snapshots.try_send(machine.snapshot());

    loop {
        loop {
            match commands.try_recv() {
                Ok(MachineCommand::Reset) => {
                    machine.reset();
                    fractional_cycles = 0;
                }
                Ok(MachineCommand::SetPaused(paused)) => machine.set_paused(paused),
                Ok(MachineCommand::Shutdown) | Err(TryRecvError::Disconnected) => return,
                Err(TryRecvError::Empty) => break,
            }
        }

        let now = Instant::now();
        let elapsed = now.duration_since(last_host_tick).min(MAX_HOST_DELTA);
        last_host_tick = now;

        let scaled = elapsed.as_nanos() * u128::from(CPU_HZ) + fractional_cycles;
        let elapsed_cycles = scaled / 1_000_000_000;
        fractional_cycles = scaled % 1_000_000_000;
        machine.advance(elapsed_cycles as u64);

        if now >= next_snapshot {
            let _ = snapshots.try_send(machine.snapshot());
            next_snapshot = now + SNAPSHOT_PERIOD;
        }

        thread::sleep(Duration::from_millis(1));
    }
}

fn load_machine() -> AstraMachine {
    let Ok(path) = env::var("ASTRA68_BOOT_ROM") else {
        return AstraMachine::new();
    };
    let rom = fs::read(&path)
        .unwrap_or_else(|error| panic!("failed to read ASTRA68_BOOT_ROM '{path}': {error}"));
    AstraMachine::try_with_rom(&rom)
        .unwrap_or_else(|error| panic!("invalid ASTRA68_BOOT_ROM '{path}': {error}"))
}
