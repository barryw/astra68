use crate::{CPU_HZ, POST_COLS, POST_ROWS, RAM_BYTES};
use std::error::Error;
use std::fmt;

pub(crate) const ROM_BASE: u32 = 0xffe0_0000;
pub(crate) const ROM_BYTES: usize = 256 * 1024;
pub(crate) const BRAM_BASE: u32 = 0x01ff_8000;
pub(crate) const BRAM_BYTES: usize = 32 * 1024;
pub(crate) const SDRAM_BASE: u32 = 0x0200_0000;
const VESTA_BASE: u32 = 0xfff0_0000;
const FRONT_PANEL_BASE: u32 = VESTA_BASE + 0x1000;
const ASTRAEA_BASE: u32 = 0xfff1_0000;
const VEGA_BASE: u32 = 0xfff2_0000;
const VEGA_TEXT_BASE: u32 = VEGA_BASE + 0x2000;
const VEGA_TEXT_APERTURE: u32 = 0x1000;
const UART_BASE: u32 = VESTA_BASE + 0x0500;
const VESTA_APERTURE: u32 = 0x0800;
const FRONT_PANEL_APERTURE: u32 = 0x0100;

const BUILD_ID: u32 = 0x18eb_e2e1;
const KERNEL_STATUS_READY: u32 = 0x4b31_4f4b;
const KERNEL_STATUS_SOAK: u32 = 0x4b31_534b;
const KERNEL_STATUS_PANIC: u32 = 0x4b50_414e;
const BIST_SWEEP_CYCLES: u64 = 1_437_500;
const BIST_TOTAL_CYCLES: u64 = BIST_SWEEP_CYCLES * 4;

const BLIT_DONE: u32 = 1 << 1;
const BLIT_MODE_COPY: u32 = 0;
const BLIT_MODE_FILL: u32 = 1;
const TIMER_ENABLE: u32 = 1 << 0;
const TIMER_PERIODIC: u32 = 1 << 1;
const TIMER_IRQ_ENABLE: u32 = 1 << 2;
const TIMER_EXPIRED: u32 = 1 << 0;
const IRQ_VALID: u32 = 1 << 31;
const IRQ_VECTOR_SHIFT: u32 = 16;
const IRQ_SOURCE_SHIFT: u32 = 8;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RomError(String);

impl fmt::Display for RomError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

impl Error for RomError {}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
enum BistState {
    #[default]
    Idle,
    Running {
        started_at: u64,
    },
    Done,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
struct BistRegisters {
    status: u32,
    progress: u32,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
struct Timer {
    load: u32,
    control: u32,
    status: u32,
    deadline: Option<u64>,
}

impl Timer {
    fn scale(self) -> u64 {
        1_u64 << ((self.control >> 4) & 0x0f)
    }

    fn period(self) -> u64 {
        u64::from(self.load.max(1)).saturating_mul(self.scale())
    }
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
struct Astraea {
    src: u32,
    dst: u32,
    src_pitch: u32,
    dst_pitch: u32,
    dim: u32,
    op: u32,
    color: u32,
    status: u32,
}

pub(crate) struct MachineBus {
    rom: Box<[u8]>,
    bram: Box<[u8]>,
    sdram: Box<[u8]>,
    console: Box<[u8]>,
    serial: Vec<u8>,
    cycles: u64,
    cycle_offset: u64,
    bist: BistState,
    astraea: Astraea,
    scratch: u32,
    scratch_trace: [u32; 32],
    scratch_trace_next: usize,
    scratch_trace_count: usize,
    irq_enable: u32,
    irq_soft: u32,
    irq_config: [u32; 32],
    timers: [Timer; 2],
    panel_change: u32,
    panel_led_data: u8,
    panel_led_ownership: u8,
    ready_for_loader: bool,
    post_failed: bool,
}

impl MachineBus {
    pub(crate) fn new(rom_image: &[u8]) -> Result<Self, RomError> {
        validate_rom(rom_image)?;

        let mut rom = vec![0xff; ROM_BYTES].into_boxed_slice();
        rom[..rom_image.len()].copy_from_slice(rom_image);
        Ok(Self {
            rom,
            bram: vec![0; BRAM_BYTES].into_boxed_slice(),
            sdram: vec![0; RAM_BYTES as usize].into_boxed_slice(),
            console: vec![b' '; POST_COLS * POST_ROWS].into_boxed_slice(),
            serial: Vec::with_capacity(16 * 1024),
            cycles: 0,
            cycle_offset: 0,
            bist: BistState::Idle,
            astraea: Astraea::default(),
            scratch: 0,
            scratch_trace: [0; 32],
            scratch_trace_next: 0,
            scratch_trace_count: 0,
            irq_enable: 0,
            irq_soft: 0,
            irq_config: [0; 32],
            timers: [Timer::default(); 2],
            panel_change: 0,
            panel_led_data: 0,
            panel_led_ownership: 0,
            ready_for_loader: false,
            post_failed: false,
        })
    }

    pub(crate) fn reset(&mut self) {
        self.bram.fill(0);
        self.sdram.fill(0);
        self.console.fill(b' ');
        self.serial.clear();
        self.cycles = 0;
        self.cycle_offset = 0;
        self.bist = BistState::Idle;
        self.astraea = Astraea::default();
        self.scratch = 0;
        self.scratch_trace = [0; 32];
        self.scratch_trace_next = 0;
        self.scratch_trace_count = 0;
        self.irq_enable = 0;
        self.irq_soft = 0;
        self.irq_config = [0; 32];
        self.timers = [Timer::default(); 2];
        self.panel_change = 0;
        self.panel_led_data = 0;
        self.panel_led_ownership = 0;
        self.ready_for_loader = false;
        self.post_failed = false;
    }

    pub(crate) fn set_cycle_offset(&mut self, cycles: u64) {
        self.cycle_offset = cycles;
    }

    pub(crate) fn finish_timeslice(&mut self, cycles: u64) {
        self.cycles = self.cycles.saturating_add(cycles);
        self.cycle_offset = 0;
    }

    pub(crate) fn cycles(&self) -> u64 {
        self.cycles
    }

    pub(crate) fn current_cycles(&self) -> u64 {
        self.cycles.saturating_add(self.cycle_offset)
    }

    pub(crate) fn ready_for_loader(&self) -> bool {
        self.ready_for_loader
    }

    pub(crate) fn post_failed(&self) -> bool {
        self.post_failed
    }

    pub(crate) fn kernel_ready(&self) -> bool {
        self.scratch == KERNEL_STATUS_READY || self.scratch == KERNEL_STATUS_SOAK
    }

    pub(crate) fn kernel_soaking(&self) -> bool {
        self.scratch == KERNEL_STATUS_SOAK
    }

    pub(crate) fn kernel_panicked(&self) -> bool {
        self.scratch == KERNEL_STATUS_PANIC
    }

    pub(crate) fn terminal(&self) -> bool {
        self.post_failed || self.kernel_panicked()
    }

    pub(crate) fn prepare_execution(&mut self, requested: u64) -> (u64, u32) {
        self.refresh_timers();
        let now = self.current_cycles();
        let next_event = self
            .timers
            .iter()
            .filter(|timer| timer.control & TIMER_ENABLE != 0)
            .filter_map(|timer| timer.deadline)
            .filter(|deadline| *deadline > now)
            .map(|deadline| deadline - now)
            .min()
            .unwrap_or(requested);
        (requested.min(next_event).max(1), self.irq_level())
    }

    pub(crate) fn interrupt_acknowledge(&mut self, level: u32) -> u32 {
        self.refresh_timers();
        let pending = self.pending_enabled();
        for source in 0..32 {
            let config = self.irq_config[source];
            if pending & (1_u32 << source) != 0 && config & 7 == level {
                return (config >> 8) & 0xff;
            }
        }
        24
    }

    pub(crate) fn interrupt_level(&mut self) -> u32 {
        self.refresh_timers();
        self.irq_level()
    }

    pub(crate) fn build_id(&self) -> u32 {
        BUILD_ID
    }

    pub(crate) fn scratch(&self) -> u32 {
        self.scratch
    }

    pub(crate) fn scratch_trace(&self) -> Vec<u32> {
        let start = (self.scratch_trace_next + self.scratch_trace.len() - self.scratch_trace_count)
            % self.scratch_trace.len();

        (0..self.scratch_trace_count)
            .map(|offset| self.scratch_trace[(start + offset) % self.scratch_trace.len()])
            .collect()
    }

    pub(crate) fn console_rows(&self) -> Vec<String> {
        self.console
            .chunks_exact(POST_COLS)
            .map(|row| String::from_utf8_lossy(row).into_owned())
            .collect()
    }

    pub(crate) fn console_transcript(&self) -> String {
        rows_to_transcript(self.console_rows())
    }

    pub(crate) fn serial_transcript(&self) -> String {
        String::from_utf8_lossy(&self.serial).into_owned()
    }

    pub(crate) fn bist_progress_milli(&self) -> u16 {
        match self.bist {
            BistState::Idle => 0,
            BistState::Done => 1000,
            BistState::Running { started_at } => {
                let elapsed = self.current_cycles().saturating_sub(started_at);
                ((elapsed.saturating_mul(1000) / BIST_TOTAL_CYCLES).min(999)) as u16
            }
        }
    }

    pub(crate) fn read8(&mut self, address: u32) -> u8 {
        if let Some(value) = self.read_memory_byte(address) {
            return value;
        }
        if is_mmio(address) {
            let aligned = address & !3;
            let shift = 24 - ((address & 3) * 8);
            return (self.read_mmio32(aligned) >> shift) as u8;
        }
        0
    }

    pub(crate) fn read16(&mut self, address: u32) -> u16 {
        if is_mmio(address) && address & 3 <= 2 {
            let shift = 16 - ((address & 3) * 8);
            return (self.read_mmio32(address & !3) >> shift) as u16;
        }
        u16::from_be_bytes([self.read8(address), self.read8(address.wrapping_add(1))])
    }

    pub(crate) fn read32(&mut self, address: u32) -> u32 {
        if is_mmio(address) && address & 3 == 0 {
            return self.read_mmio32(address);
        }
        u32::from_be_bytes([
            self.read8(address),
            self.read8(address.wrapping_add(1)),
            self.read8(address.wrapping_add(2)),
            self.read8(address.wrapping_add(3)),
        ])
    }

    pub(crate) fn pmmu_read32(&mut self, address: u32) -> Option<u32> {
        self.mapped_range(address, 4).then(|| self.read32(address))
    }

    pub(crate) fn write8(&mut self, address: u32, value: u8) {
        if self.write_memory_byte(address, value) {
            return;
        }

        let register = address & !3;
        if register == UART_BASE {
            self.serial.push(value);
        } else if register == VESTA_BASE + 0x00d0 && value & 1 != 0 {
            self.start_bist();
        }
    }

    pub(crate) fn write16(&mut self, address: u32, value: u16) {
        let bytes = value.to_be_bytes();
        self.write8(address, bytes[0]);
        self.write8(address.wrapping_add(1), bytes[1]);
    }

    pub(crate) fn write32(&mut self, address: u32, value: u32) {
        if is_mmio(address) && address & 3 == 0 {
            self.write_mmio32(address, value);
            return;
        }
        for (offset, byte) in value.to_be_bytes().into_iter().enumerate() {
            self.write8(address.wrapping_add(offset as u32), byte);
        }
    }

    pub(crate) fn pmmu_write32(&mut self, address: u32, value: u32) -> bool {
        if !self.mapped_range(address, 4) {
            return false;
        }
        self.write32(address, value);
        true
    }

    fn mapped_range(&self, address: u32, length: u32) -> bool {
        if length == 0 || address.checked_add(length - 1).is_none() {
            return false;
        }
        (0..length).all(|offset| {
            let current = address + offset;
            self.read_memory_byte(current).is_some() || is_mmio(current)
        })
    }

    fn read_memory_byte(&self, address: u32) -> Option<u8> {
        if address < ROM_BYTES as u32 {
            return Some(self.rom[address as usize]);
        }
        if let Some(offset) = range_offset(address, ROM_BASE, ROM_BYTES) {
            return Some(self.rom[offset]);
        }
        if let Some(offset) = range_offset(address, BRAM_BASE, BRAM_BYTES) {
            return Some(self.bram[offset]);
        }
        if let Some(offset) = range_offset(address, SDRAM_BASE, RAM_BYTES as usize) {
            return Some(self.sdram[offset]);
        }
        if (VEGA_TEXT_BASE..VEGA_TEXT_BASE + VEGA_TEXT_APERTURE).contains(&address) {
            let offset = (address - VEGA_TEXT_BASE) as usize;
            return Some(self.console.get(offset).copied().unwrap_or(b' '));
        }
        None
    }

    fn write_memory_byte(&mut self, address: u32, value: u8) -> bool {
        if let Some(offset) = range_offset(address, BRAM_BASE, BRAM_BYTES) {
            self.bram[offset] = value;
            return true;
        }
        if let Some(offset) = range_offset(address, SDRAM_BASE, RAM_BYTES as usize) {
            self.sdram[offset] = value;
            return true;
        }
        if (VEGA_TEXT_BASE..VEGA_TEXT_BASE + VEGA_TEXT_APERTURE).contains(&address) {
            let offset = (address - VEGA_TEXT_BASE) as usize;
            if let Some(cell) = self.console.get_mut(offset) {
                *cell = value;
                if value == b'R' || value == b'E' {
                    self.refresh_terminal_state();
                }
            }
            return true;
        }
        if address < ROM_BYTES as u32 || range_offset(address, ROM_BASE, ROM_BYTES).is_some() {
            return true;
        }
        false
    }

    fn read_mmio32(&mut self, address: u32) -> u32 {
        if (UART_BASE..UART_BASE + 0x10).contains(&address) {
            return match address - UART_BASE {
                0x04 => 1,
                _ => 0,
            };
        }
        if (VESTA_BASE..VESTA_BASE + VESTA_APERTURE).contains(&address) {
            return self.read_vesta(address - VESTA_BASE);
        }
        if (FRONT_PANEL_BASE..FRONT_PANEL_BASE + FRONT_PANEL_APERTURE).contains(&address) {
            return self.read_front_panel(address - FRONT_PANEL_BASE);
        }
        if (ASTRAEA_BASE..ASTRAEA_BASE + 0x0100).contains(&address) {
            return self.read_astraea(address - ASTRAEA_BASE);
        }
        if (VEGA_BASE..VEGA_BASE + 0x0100).contains(&address) {
            return match address - VEGA_BASE {
                0x00 => 0x5645_4741,
                0x04 => 0x0001_0000,
                0x08 => 1,
                0x0c => 1 << 4,
                0x18 => 0,
                0x1c => 1,
                0x28 => (480 << 16) | 720,
                0x30 => 0x0010_1820,
                _ => 0,
            };
        }
        0
    }

    fn write_mmio32(&mut self, address: u32, value: u32) {
        if address == UART_BASE {
            self.serial.push(value as u8);
            return;
        }
        if (VESTA_BASE..VESTA_BASE + VESTA_APERTURE).contains(&address) {
            self.write_vesta(address - VESTA_BASE, value);
            return;
        }
        if (FRONT_PANEL_BASE..FRONT_PANEL_BASE + FRONT_PANEL_APERTURE).contains(&address) {
            self.write_front_panel(address - FRONT_PANEL_BASE, value);
            return;
        }
        if (ASTRAEA_BASE..ASTRAEA_BASE + 0x0100).contains(&address) {
            self.write_astraea(address - ASTRAEA_BASE, value);
        }
    }

    fn read_vesta(&mut self, offset: u32) -> u32 {
        self.refresh_timers();
        match offset {
            0x000 => 0x5653_5441,
            0x004 => 0x0001_0000,
            0x008 => 0x4136_3801,
            0x00c => 0,
            0x010 => 0x0000_000f,
            0x014 => 0,
            0x018 => self.scratch,
            0x01c => 0x0006_8030,
            0x020 => 0x5447_4d32,
            0x024 => 0x0000_000d,
            0x028 => CPU_HZ as u32,
            0x02c => SDRAM_BASE,
            0x030 => RAM_BYTES,
            0x034 => ROM_BASE,
            0x038 => ROM_BYTES as u32,
            0x03c => BUILD_ID,
            0x040 => 3,
            0x044 => 16,
            0x048 => 0,
            0x050 => 0x5653_5441,
            0x054 => 0x0001_0000,
            0x058 => VESTA_BASE,
            0x05c => 0x0001_0000,
            0x060 => 0x4153_5452,
            0x064 => 0x0001_0000,
            0x068 => ASTRAEA_BASE,
            0x06c => 0x0001_0000,
            0x070 => 0x5645_4741,
            0x074 => 0x0001_0000,
            0x078 => VEGA_BASE,
            0x07c => 0x0001_0000,
            0x0d0 => 0,
            0x0d4 => self.bist_registers().status,
            0x0d8 => self.bist_registers().progress,
            0x0dc..=0x0e8 => 0,
            0x0ec => self.current_cycles() as u32,
            0x0f0 => (self.current_cycles() >> 32) as u32,
            0x0f4..=0x0128 => 0,
            0x300 => self.pending_raw(),
            0x304 => self.irq_enable,
            0x308 => self.irq_soft,
            0x30c => 0,
            0x310 => self.irq_current(),
            0x380..=0x3fc if offset & 3 == 0 => self.irq_config[((offset - 0x380) / 4) as usize],
            0x400..=0x41c if offset & 3 == 0 => {
                let timer = ((offset - 0x400) / 0x10) as usize;
                match offset & 0x0f {
                    0x0 => self.timers[timer].load,
                    0x4 => self.timer_value(timer),
                    0x8 => self.timers[timer].control,
                    0xc => self.timers[timer].status,
                    _ => 0,
                }
            }
            _ => 0,
        }
    }

    fn write_vesta(&mut self, offset: u32, value: u32) {
        match offset {
            0x018 => {
                self.scratch = value;
                self.scratch_trace[self.scratch_trace_next] = value;
                self.scratch_trace_next = (self.scratch_trace_next + 1) % self.scratch_trace.len();
                self.scratch_trace_count =
                    (self.scratch_trace_count + 1).min(self.scratch_trace.len());
            }
            0x0d0 if value & 1 != 0 => self.start_bist(),
            0x304 => self.irq_enable = value,
            0x308 => self.irq_soft = value,
            0x30c => self.irq_soft &= !value,
            0x380..=0x3fc if offset & 3 == 0 => {
                self.irq_config[((offset - 0x380) / 4) as usize] = value & 0x0001_ffff;
            }
            0x400..=0x41c if offset & 3 == 0 => {
                let timer = ((offset - 0x400) / 0x10) as usize;
                match offset & 0x0f {
                    0x0 => self.timers[timer].load = value,
                    0x8 => {
                        self.timers[timer].control = value & 0x0000_00f7;
                        let deadline = if value & TIMER_ENABLE != 0 {
                            Some(
                                self.current_cycles()
                                    .saturating_add(self.timers[timer].period()),
                            )
                        } else {
                            None
                        };
                        self.timers[timer].deadline = deadline;
                    }
                    0xc => self.timers[timer].status &= !value,
                    _ => {}
                }
            }
            _ => {}
        }
    }

    fn read_front_panel(&self, offset: u32) -> u32 {
        match offset {
            0x00 => 0x504e_4c30,
            0x04 => 0x0001_0000,
            0x08 => 0x0f04_0608,
            0x0c | 0x10 => 0,
            0x14 => self.panel_change,
            0x18 => u32::from(self.panel_led_data),
            0x1c => u32::from(self.panel_led_ownership),
            _ => 0,
        }
    }

    fn write_front_panel(&mut self, offset: u32, value: u32) {
        match offset {
            0x14 => self.panel_change &= !value,
            0x18 => self.panel_led_data = value as u8,
            0x1c => self.panel_led_ownership = value as u8,
            0x20 => self.panel_led_data |= value as u8,
            0x24 => self.panel_led_data &= !(value as u8),
            0x28 => self.panel_led_data ^= value as u8,
            _ => {}
        }
    }

    fn refresh_timers(&mut self) {
        let now = self.current_cycles();
        for timer in &mut self.timers {
            let Some(deadline) = timer.deadline else {
                continue;
            };
            if timer.control & TIMER_ENABLE == 0 || now < deadline {
                continue;
            }
            timer.status |= TIMER_EXPIRED;
            if timer.control & TIMER_PERIODIC != 0 {
                let period = timer.period();
                let elapsed_periods = (now - deadline) / period + 1;
                timer.deadline =
                    Some(deadline.saturating_add(period.saturating_mul(elapsed_periods)));
            } else {
                timer.control &= !TIMER_ENABLE;
                timer.deadline = None;
            }
        }
    }

    fn timer_value(&self, index: usize) -> u32 {
        let timer = self.timers[index];
        let Some(deadline) = timer.deadline else {
            return 0;
        };
        if deadline <= self.current_cycles() {
            return 0;
        }
        ((deadline - self.current_cycles()) / timer.scale()).min(u64::from(u32::MAX)) as u32
    }

    fn pending_raw(&self) -> u32 {
        let mut pending = self.irq_soft;
        for (source, timer) in self.timers.iter().enumerate() {
            if timer.status & TIMER_EXPIRED != 0 && timer.control & TIMER_IRQ_ENABLE != 0 {
                pending |= 1_u32 << source;
            }
        }
        pending
    }

    fn pending_enabled(&self) -> u32 {
        self.pending_raw() & self.irq_enable
    }

    fn irq_level(&self) -> u32 {
        let pending = self.pending_enabled();
        (1..=7)
            .rev()
            .find(|level| {
                (0..32).any(|source| {
                    pending & (1_u32 << source) != 0 && self.irq_config[source] & 7 == *level
                })
            })
            .unwrap_or(0)
    }

    fn irq_current(&self) -> u32 {
        let level = self.irq_level();
        let pending = self.pending_enabled();
        for source in 0..32 {
            let config = self.irq_config[source];
            if level != 0 && pending & (1_u32 << source) != 0 && config & 7 == level {
                return IRQ_VALID
                    | (((config >> 8) & 0xff) << IRQ_VECTOR_SHIFT)
                    | ((source as u32) << IRQ_SOURCE_SHIFT)
                    | level;
            }
        }
        0
    }

    fn read_astraea(&self, offset: u32) -> u32 {
        match offset {
            0x00 => 0x4153_5452,
            0x04 => 0x0001_0000,
            0x40 => self.astraea.src,
            0x44 => self.astraea.dst,
            0x4c => self.astraea.src_pitch,
            0x50 => self.astraea.dst_pitch,
            0x58 => self.astraea.dim,
            0x5c => self.astraea.op,
            0x60 => self.astraea.color,
            0x6c => self.astraea.status,
            _ => 0,
        }
    }

    fn write_astraea(&mut self, offset: u32, value: u32) {
        match offset {
            0x40 => self.astraea.src = value,
            0x44 => self.astraea.dst = value,
            0x4c => self.astraea.src_pitch = value,
            0x50 => self.astraea.dst_pitch = value,
            0x58 => self.astraea.dim = value,
            0x5c => self.astraea.op = value,
            0x60 => self.astraea.color = value,
            0x68 if value & 1 != 0 => self.run_blitter(),
            _ => {}
        }
    }

    fn run_blitter(&mut self) {
        self.astraea.status = 1;
        let width = (self.astraea.dim & 0xffff) as usize;
        let height = (self.astraea.dim >> 16) as usize;
        let element_bytes = match (self.astraea.op >> 4) & 3 {
            0 => 1,
            1 => 2,
            2 => 4,
            _ => return self.blit_error(1),
        };
        let row_bytes = match width.checked_mul(element_bytes) {
            Some(bytes) if bytes != 0 && height != 0 => bytes,
            _ => return self.blit_error(1),
        };
        let src_pitch = self.astraea.src_pitch as usize;
        let dst_pitch = self.astraea.dst_pitch as usize;

        for row in 0..height {
            let dst = self.astraea.dst as usize + row.saturating_mul(dst_pitch);
            let dst_end = match dst.checked_add(row_bytes) {
                Some(end) if end <= self.sdram.len() => end,
                _ => return self.blit_error(2),
            };
            match self.astraea.op & 0x0f {
                BLIT_MODE_COPY => {
                    let src = self.astraea.src as usize + row.saturating_mul(src_pitch);
                    let src_end = match src.checked_add(row_bytes) {
                        Some(end) if end <= self.sdram.len() => end,
                        _ => return self.blit_error(2),
                    };
                    self.sdram.copy_within(src..src_end, dst);
                }
                BLIT_MODE_FILL => {
                    let color = self.astraea.color.to_be_bytes();
                    let pattern = &color[4 - element_bytes..];
                    for chunk in self.sdram[dst..dst_end].chunks_exact_mut(element_bytes) {
                        chunk.copy_from_slice(pattern);
                    }
                }
                _ => return self.blit_error(1),
            }
        }
        self.astraea.status = BLIT_DONE;
    }

    fn blit_error(&mut self, code: u32) {
        self.astraea.status = BLIT_DONE | (code << 8);
    }

    fn start_bist(&mut self) {
        if !matches!(self.bist, BistState::Running { .. }) {
            self.bist = BistState::Running {
                started_at: self.current_cycles(),
            };
        }
    }

    fn bist_registers(&mut self) -> BistRegisters {
        let BistState::Running { started_at } = self.bist else {
            return match self.bist {
                BistState::Idle => BistRegisters::default(),
                BistState::Done => BistRegisters {
                    status: (3 << 8) | (1 << 1),
                    progress: RAM_BYTES - 4,
                },
                BistState::Running { .. } => unreachable!(),
            };
        };

        let elapsed = self.current_cycles().saturating_sub(started_at);
        if elapsed >= BIST_TOTAL_CYCLES {
            self.materialize_bist_result();
            self.bist = BistState::Done;
            return BistRegisters {
                status: (3 << 8) | (1 << 1),
                progress: RAM_BYTES - 4,
            };
        }

        let sweep = elapsed / BIST_SWEEP_CYCLES;
        let within_sweep = elapsed % BIST_SWEEP_CYCLES;
        let phase = if sweep & 1 == 0 { 1 } else { 2 };
        let progress = ((u64::from(RAM_BYTES - 4) * within_sweep) / BIST_SWEEP_CYCLES) as u32;
        BistRegisters {
            status: (phase << 8) | 1,
            progress,
        }
    }

    fn materialize_bist_result(&mut self) {
        for (address, byte) in self.sdram.iter_mut().enumerate() {
            let pattern = (address as u32 & 0xff)
                ^ ((address as u32 >> 8) & 0xff)
                ^ ((address as u32 >> 16) & 0xff)
                ^ ((address as u32 >> 24) & 1)
                ^ 0xa5;
            *byte = !(pattern as u8);
        }
    }

    fn refresh_terminal_state(&mut self) {
        self.ready_for_loader = contains(&self.console, b"READY FOR OS LOADER");
        self.post_failed = contains(&self.console, b"HALTED: POST FAILURE");
    }
}

fn validate_rom(rom: &[u8]) -> Result<(), RomError> {
    if rom.len() < 8 {
        return Err(RomError(
            "boot ROM is too short to contain reset vectors".into(),
        ));
    }
    if rom.len() > ROM_BYTES {
        return Err(RomError(format!(
            "boot ROM is {} bytes; the Astra aperture is {ROM_BYTES} bytes",
            rom.len()
        )));
    }

    let initial_sp = u32::from_be_bytes(rom[0..4].try_into().unwrap());
    let initial_pc = u32::from_be_bytes(rom[4..8].try_into().unwrap());
    if !(BRAM_BASE..=BRAM_BASE + BRAM_BYTES as u32).contains(&initial_sp) {
        return Err(RomError(format!(
            "reset SP 0x{initial_sp:08x} is outside Astra BRAM"
        )));
    }
    if !(ROM_BASE..ROM_BASE + ROM_BYTES as u32).contains(&initial_pc) {
        return Err(RomError(format!(
            "reset PC 0x{initial_pc:08x} is outside the boot ROM"
        )));
    }
    Ok(())
}

fn range_offset(address: u32, base: u32, length: usize) -> Option<usize> {
    let offset = address.checked_sub(base)? as usize;
    (offset < length).then_some(offset)
}

fn is_mmio(address: u32) -> bool {
    (VESTA_BASE..VESTA_BASE + VESTA_APERTURE).contains(&address)
        || (FRONT_PANEL_BASE..FRONT_PANEL_BASE + FRONT_PANEL_APERTURE).contains(&address)
        || (UART_BASE..UART_BASE + 0x10).contains(&address)
        || (ASTRAEA_BASE..ASTRAEA_BASE + 0x0100).contains(&address)
        || (VEGA_BASE..VEGA_BASE + 0x0100).contains(&address)
}

fn contains(haystack: &[u8], needle: &[u8]) -> bool {
    haystack
        .windows(needle.len())
        .any(|window| window == needle)
}

fn rows_to_transcript(rows: Vec<String>) -> String {
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

    fn test_rom() -> [u8; 8] {
        [0x02, 0x00, 0x00, 0x00, 0xff, 0xe0, 0x04, 0x00]
    }

    #[test]
    fn memory_is_big_endian_and_supports_unaligned_access() {
        let mut bus = MachineBus::new(&test_rom()).unwrap();
        bus.write32(SDRAM_BASE + 1, 0x1234_abcd);

        assert_eq!(bus.read32(SDRAM_BASE + 1), 0x1234_abcd);
        assert_eq!(bus.read16(SDRAM_BASE + 2), 0x34ab);
        assert_eq!(bus.read8(SDRAM_BASE + 4), 0xcd);
    }

    #[test]
    fn identity_matches_the_hardware_contract() {
        let mut bus = MachineBus::new(&test_rom()).unwrap();

        assert_eq!(bus.read32(VESTA_BASE), 0x5653_5441);
        assert_eq!(bus.read32(VESTA_BASE + 0x20), 0x5447_4d32);
        assert_eq!(bus.read32(VEGA_BASE), 0x5645_4741);
        assert_eq!(bus.read32(ASTRAEA_BASE), 0x4153_5452);
    }

    #[test]
    fn astraea_fill_and_copy_use_sdram_offsets() {
        let mut bus = MachineBus::new(&test_rom()).unwrap();
        bus.write32(ASTRAEA_BASE + 0x44, 0x1000);
        bus.write32(ASTRAEA_BASE + 0x50, 16);
        bus.write32(ASTRAEA_BASE + 0x58, (1 << 16) | 4);
        bus.write32(ASTRAEA_BASE + 0x5c, BLIT_MODE_FILL | (2 << 4));
        bus.write32(ASTRAEA_BASE + 0x60, 0x5aa5_c33c);
        bus.write32(ASTRAEA_BASE + 0x68, 1);
        assert_eq!(bus.read32(SDRAM_BASE + 0x1000), 0x5aa5_c33c);

        bus.write32(ASTRAEA_BASE + 0x40, 0x1000);
        bus.write32(ASTRAEA_BASE + 0x44, 0x2000);
        bus.write32(ASTRAEA_BASE + 0x4c, 16);
        bus.write32(ASTRAEA_BASE + 0x50, 16);
        bus.write32(ASTRAEA_BASE + 0x5c, BLIT_MODE_COPY | (2 << 4));
        bus.write32(ASTRAEA_BASE + 0x68, 1);
        assert_eq!(bus.read32(SDRAM_BASE + 0x200c), 0x5aa5_c33c);
        assert_eq!(bus.read32(ASTRAEA_BASE + 0x6c), BLIT_DONE);
    }

    #[test]
    fn bist_is_busy_before_it_completes() {
        let mut bus = MachineBus::new(&test_rom()).unwrap();
        bus.write32(VESTA_BASE + 0x0d0, 1);
        assert_eq!(bus.read32(VESTA_BASE + 0x0d4) & 1, 1);

        bus.finish_timeslice(BIST_TOTAL_CYCLES);
        let status = bus.read32(VESTA_BASE + 0x0d4);
        assert_eq!(status & 1, 0);
        assert_ne!(status & (1 << 1), 0);
        assert_eq!(status >> 8, 3);
    }

    #[test]
    fn malformed_roms_are_rejected() {
        assert!(MachineBus::new(&[]).is_err());
        let mut bad_pc = test_rom();
        bad_pc[4..].copy_from_slice(&0x1000_u32.to_be_bytes());
        assert!(MachineBus::new(&bad_pc).is_err());
    }

    #[test]
    fn pmmu_table_bus_rejects_unmapped_physical_addresses() {
        let mut bus = MachineBus::new(&test_rom()).unwrap();

        assert_eq!(bus.pmmu_read32(0x0100_0000), None);
        assert!(!bus.pmmu_write32(0x0100_0000, 0x1234_5678));

        assert!(bus.pmmu_write32(SDRAM_BASE, 0x1234_5678));
        assert_eq!(bus.pmmu_read32(SDRAM_BASE), Some(0x1234_5678));
    }

    #[test]
    fn vesta_periodic_timer_drives_a_vectored_interrupt() {
        let mut bus = MachineBus::new(&test_rom()).unwrap();

        bus.write32(VESTA_BASE + 0x380, (80 << 8) | 4);
        bus.write32(VESTA_BASE + 0x304, 1);
        bus.write32(VESTA_BASE + 0x400, 100);
        bus.write32(
            VESTA_BASE + 0x408,
            TIMER_ENABLE | TIMER_PERIODIC | TIMER_IRQ_ENABLE,
        );
        assert_eq!(bus.prepare_execution(1_000), (100, 0));

        bus.finish_timeslice(100);
        assert_eq!(bus.prepare_execution(1_000).1, 4);
        assert_eq!(bus.read32(VESTA_BASE + 0x300), 1);
        assert_eq!(bus.read32(VESTA_BASE + 0x310), IRQ_VALID | (80 << 16) | 4);
        assert_eq!(bus.interrupt_acknowledge(4), 80);

        bus.write32(VESTA_BASE + 0x40c, TIMER_EXPIRED);
        assert_eq!(bus.read32(VESTA_BASE + 0x300), 0);
        assert_eq!(bus.prepare_execution(1_000), (100, 0));
    }

    #[test]
    fn kernel_status_controls_machine_completion() {
        let mut bus = MachineBus::new(&test_rom()).unwrap();

        bus.write32(VESTA_BASE + 0x018, KERNEL_STATUS_READY);
        assert!(bus.kernel_ready());
        assert!(!bus.kernel_soaking());
        assert!(!bus.terminal());

        bus.write32(VESTA_BASE + 0x018, KERNEL_STATUS_SOAK);
        assert!(bus.kernel_ready());
        assert!(bus.kernel_soaking());
        assert!(!bus.terminal());

        bus.write32(VESTA_BASE + 0x018, KERNEL_STATUS_PANIC);
        assert!(bus.kernel_panicked());
        assert!(bus.terminal());
        assert_eq!(
            bus.scratch_trace(),
            vec![KERNEL_STATUS_READY, KERNEL_STATUS_SOAK, KERNEL_STATUS_PANIC]
        );

        for value in 0..40 {
            bus.write32(VESTA_BASE + 0x018, value);
        }
        assert_eq!(bus.scratch_trace(), (8..40).collect::<Vec<_>>());

        bus.reset();
        assert_eq!(bus.scratch(), 0);
        assert!(bus.scratch_trace().is_empty());
    }
}
