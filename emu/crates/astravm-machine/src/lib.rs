//! Headless Astra 68 reference machine.
//!
//! The machine executes the unchanged boot ROM on the vendored Musashi 68030
//! core. Host UI code sees immutable snapshots and never participates in CPU,
//! bus, device, or virtual-time behavior.

mod bus;
mod musashi;

use bus::MachineBus;
pub use bus::RomError;
use musashi::MusashiCpu;

pub const CPU_HZ: u64 = 12_500_000;
pub const RAM_BYTES: u32 = 32 * 1024 * 1024;
pub const DISPLAY_WIDTH: usize = 720;
pub const DISPLAY_HEIGHT: usize = 480;
pub const POST_COLS: usize = 90;
pub const POST_ROWS: usize = 30;
pub const BOOT_TIMEOUT_CYCLES: u64 = 100_000_000;

const DEFAULT_ROM: &[u8] = include_bytes!("../../../rom/astra_boot.bin");

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PostState {
    Pending,
    Running,
    Passed,
    Failed,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PostStageSnapshot {
    pub key: &'static str,
    pub label: &'static str,
    pub detail: &'static str,
    pub state: PostState,
    pub progress_milli: u16,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MachineSnapshot {
    pub cycles: u64,
    pub cpu_hz: u64,
    pub cpu_pc: u32,
    pub ram_bytes: u32,
    pub build_id: u32,
    pub backend: &'static str,
    pub paused: bool,
    pub ready_for_loader: bool,
    pub post_failed: bool,
    pub stages: Vec<PostStageSnapshot>,
    pub console_rows: Vec<String>,
}

#[derive(Clone, Copy)]
struct StageDefinition {
    key: &'static str,
    label: &'static str,
    detail: &'static str,
    running_marker: &'static str,
    passed_marker: &'static str,
}

const POST_STAGES: [StageDefinition; 9] = [
    StageDefinition {
        key: "vesta",
        label: "Vesta identity",
        detail: "VSTA v1.0 / TGM2",
        running_marker: "ASTRA 68 SYSTEM ROM",
        passed_marker: "\n  POST\n",
    },
    StageDefinition {
        key: "sdram-init",
        label: "SDRAM init",
        detail: "32 MiB @ 0x02000000",
        running_marker: "SDRAM init ........",
        passed_marker: "SDRAM init ........ OK",
    },
    StageDefinition {
        key: "data-lanes",
        label: "Data / byte lanes",
        detail: "8 / 16 / 32-bit big-endian",
        running_marker: "Data/byte lanes ....",
        passed_marker: "Data/byte lanes .... OK",
    },
    StageDefinition {
        key: "address-lines",
        label: "Address lines",
        detail: "full 25-bit SDRAM decode",
        running_marker: "Address lines ......",
        passed_marker: "Address lines ...... OK",
    },
    StageDefinition {
        key: "cache",
        label: "Cache coherence",
        detail: "self-modifying SDRAM code",
        running_marker: "Cache coherence ....",
        passed_marker: "Cache coherence .... OK",
    },
    StageDefinition {
        key: "bram-width",
        label: "CPU BRAM access",
        detail: "real ROM benchmark",
        running_marker: "CPU BRAM access ....",
        passed_marker: "CPU BRAM access .... OK",
    },
    StageDefinition {
        key: "sdram-width",
        label: "CPU SDRAM access",
        detail: "real ROM benchmark",
        running_marker: "CPU SDRAM access ...",
        passed_marker: "CPU SDRAM access ... OK",
    },
    StageDefinition {
        key: "astraea",
        label: "Astraea DMA",
        detail: "fill + copy + preserve",
        running_marker: "Astraea DMA ........",
        passed_marker: "Astraea DMA ........ OK",
    },
    StageDefinition {
        key: "bist",
        label: "Full-range BIST",
        detail: "four SDRAM sweeps",
        running_marker: "Full-range BIST ....",
        passed_marker: "Full-range BIST .... OK",
    },
];

pub struct AstraMachine {
    cpu: MusashiCpu,
    bus: MachineBus,
    paused: bool,
}

impl Default for AstraMachine {
    fn default() -> Self {
        Self::new()
    }
}

impl AstraMachine {
    pub fn new() -> Self {
        Self::try_with_rom(DEFAULT_ROM).expect("embedded Astra boot ROM must be valid")
    }

    pub fn try_with_rom(rom: &[u8]) -> Result<Self, RomError> {
        let mut bus = MachineBus::new(rom)?;
        let cpu = MusashiCpu::new(&mut bus);
        Ok(Self {
            cpu,
            bus,
            paused: false,
        })
    }

    pub fn reset(&mut self) {
        self.bus.reset();
        self.cpu.reset(&mut self.bus);
        self.paused = false;
    }

    pub fn set_paused(&mut self, paused: bool) {
        self.paused = paused;
    }

    pub fn advance(&mut self, cycles: u64) {
        if self.paused || self.bus.terminal() || cycles == 0 {
            return;
        }

        let mut remaining = cycles;
        while remaining != 0 && !self.bus.terminal() {
            let ran = self.cpu.execute(&mut self.bus, remaining);
            if ran == 0 {
                break;
            }
            remaining = remaining.saturating_sub(ran);
        }
    }

    pub fn snapshot(&self) -> MachineSnapshot {
        let console_rows = self.bus.console_rows();
        let transcript = rows_to_transcript(&console_rows);
        MachineSnapshot {
            cycles: self.bus.cycles(),
            cpu_hz: CPU_HZ,
            cpu_pc: self.cpu.pc(),
            ram_bytes: RAM_BYTES,
            build_id: self.bus.build_id(),
            backend: "MUSASHI 68030 / REAL ROM",
            paused: self.paused,
            ready_for_loader: self.bus.ready_for_loader(),
            post_failed: self.bus.post_failed(),
            stages: stage_snapshots(
                &transcript,
                self.bus.post_failed(),
                self.bus.bist_progress_milli(),
            ),
            console_rows,
        }
    }

    pub fn console_transcript(&self) -> String {
        self.bus.console_transcript()
    }

    pub fn serial_transcript(&self) -> String {
        self.bus.serial_transcript()
    }
}

fn stage_snapshots(
    transcript: &str,
    post_failed: bool,
    bist_progress_milli: u16,
) -> Vec<PostStageSnapshot> {
    let mut stages: Vec<_> = POST_STAGES
        .iter()
        .map(|definition| {
            let state = if transcript.contains(definition.passed_marker) {
                PostState::Passed
            } else if transcript.contains(definition.running_marker) {
                PostState::Running
            } else {
                PostState::Pending
            };
            let progress_milli = match state {
                PostState::Passed => 1000,
                PostState::Running if definition.key == "bist" => bist_progress_milli,
                PostState::Running => 300,
                PostState::Pending | PostState::Failed => 0,
            };
            PostStageSnapshot {
                key: definition.key,
                label: definition.label,
                detail: definition.detail,
                state,
                progress_milli,
            }
        })
        .collect();

    if stages.iter().all(|stage| stage.state == PostState::Pending) {
        stages[0].state = PostState::Running;
        stages[0].progress_milli = 100;
    }
    if post_failed {
        let failed_index = stages
            .iter()
            .position(|stage| stage.state == PostState::Running)
            .or_else(|| {
                stages
                    .iter()
                    .position(|stage| stage.state == PostState::Pending)
            });
        if let Some(index) = failed_index {
            let stage = &mut stages[index];
            stage.state = PostState::Failed;
            stage.progress_milli = 0;
        }
    }
    stages
}

fn rows_to_transcript(rows: &[String]) -> String {
    let mut transcript = String::new();
    for row in rows {
        transcript.push_str(row.trim_end());
        transcript.push('\n');
    }
    transcript
}

#[cfg(test)]
mod tests {
    use super::*;

    fn run_to_terminal(machine: &mut AstraMachine, chunk_cycles: u64) {
        while !machine.snapshot().ready_for_loader
            && !machine.snapshot().post_failed
            && machine.snapshot().cycles < BOOT_TIMEOUT_CYCLES
        {
            machine.advance(chunk_cycles);
        }
    }

    #[test]
    fn reset_vectors_enter_the_physical_rom() {
        let machine = AstraMachine::new();
        let snapshot = machine.snapshot();

        assert_eq!(snapshot.cycles, 0);
        assert_eq!(snapshot.cpu_pc, 0xffe0_0400);
        assert_eq!(snapshot.stages[0].state, PostState::Running);
        assert!(!snapshot.ready_for_loader);
    }

    #[test]
    fn unchanged_boot_rom_completes_post() {
        let mut machine = AstraMachine::new();
        run_to_terminal(&mut machine, 250_000);
        let snapshot = machine.snapshot();
        let screen = machine.console_transcript();
        let serial = machine.serial_transcript();

        assert!(
            snapshot.ready_for_loader,
            "screen:\n{screen}\nserial:\n{serial}"
        );
        assert!(!snapshot.post_failed);
        assert!(snapshot.cycles < BOOT_TIMEOUT_CYCLES);
        assert!(
            snapshot
                .stages
                .iter()
                .all(|stage| stage.state == PostState::Passed)
        );
        assert!(screen.contains("ASTRA 68 SYSTEM ROM v0.3"));
        assert!(screen.contains("POST PASS"));
        assert!(screen.contains("READY FOR OS LOADER"));
        assert!(!screen.contains("FUNCTIONAL MODEL"));
        assert!(serial.contains("CPU BRAM cycles"));
        assert!(serial.contains("Astraea DMA"));
    }

    #[test]
    fn post_completion_does_not_halt_the_machine() {
        let mut machine = AstraMachine::new();
        run_to_terminal(&mut machine, 250_000);
        let completed_at = machine.snapshot().cycles;

        machine.advance(10_000);

        assert!(machine.snapshot().ready_for_loader);
        assert!(machine.snapshot().cycles > completed_at);
    }

    #[test]
    fn timeslice_chunking_does_not_change_rom_output() {
        let mut coarse_chunks = AstraMachine::new();
        run_to_terminal(&mut coarse_chunks, 250_000);

        let mut many_chunks = AstraMachine::new();
        run_to_terminal(&mut many_chunks, 50_000);

        assert!(coarse_chunks.snapshot().ready_for_loader);
        assert!(many_chunks.snapshot().ready_for_loader);
        // READY is painted before the ROM finishes its final serial summary.
        // Let both machines drain that bounded tail before comparing output.
        coarse_chunks.advance(1_000_000);
        many_chunks.advance(1_000_000);
        assert_eq!(
            coarse_chunks.console_transcript(),
            many_chunks.console_transcript()
        );
        assert_eq!(
            coarse_chunks.serial_transcript(),
            many_chunks.serial_transcript()
        );
    }

    #[test]
    fn pause_stops_virtual_time() {
        let mut machine = AstraMachine::new();
        machine.set_paused(true);
        machine.advance(1_000_000);

        assert_eq!(machine.snapshot().cycles, 0);
        assert_eq!(machine.snapshot().cpu_pc, 0xffe0_0400);
    }

    #[test]
    fn reset_restores_power_on_state() {
        let pristine = AstraMachine::new().snapshot();
        let mut machine = AstraMachine::new();
        machine.advance(1_000_000);
        machine.reset();

        assert_eq!(machine.snapshot(), pristine);
    }

    #[test]
    fn console_matches_vega_geometry() {
        let machine = AstraMachine::new();
        let snapshot = machine.snapshot();

        assert_eq!(snapshot.console_rows.len(), POST_ROWS);
        assert!(
            snapshot
                .console_rows
                .iter()
                .all(|row| row.len() == POST_COLS)
        );
    }
}
