use crate::{CPU_HZ, DISPLAY_HEIGHT, DISPLAY_WIDTH, POST_COLS, POST_ROWS, RAM_BYTES};
use std::error::Error;
use std::fmt;
use std::sync::Arc;

pub(crate) const ROM_BASE: u32 = 0xffe0_0000;
pub(crate) const ROM_BYTES: usize = 256 * 1024;
pub(crate) const BRAM_BASE: u32 = 0x01ff_8000;
pub(crate) const BRAM_BYTES: usize = 32 * 1024;
pub(crate) const SDRAM_BASE: u32 = 0x0200_0000;
const VESTA_BASE: u32 = 0xfff0_0000;
const FRONT_PANEL_BASE: u32 = VESTA_BASE + 0x1000;
const ASTRAEA_BASE: u32 = 0xfff1_0000;
const VEGA_BASE: u32 = 0xfff2_0000;
const ASTRAEA_APERTURE: u32 = 0x8000;
const VEGA_APERTURE: u32 = 0x2000;
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
const BLIT_IRQ_ENABLE: u32 = 1 << 1;
const BLIT_MODE_COPY: u32 = 0;
const BLIT_MODE_FILL: u32 = 1;
const DRAW_DONE: u32 = 1 << 1;
const DRAW_IRQ_ENABLE: u32 = 1 << 1;
const DRAW_OP_GLYPH_MASK1: u32 = 8;
const DRAW_OP_OPAQUE_BACKGROUND: u32 = 1 << 8;
const DRAW_FORMAT_INDEX8: u32 = 0;
const DRAW_ERROR_INVALID_CONFIG: u32 = 1;
const DRAW_ERROR_ADDRESS_RANGE: u32 = 4;
const ASTRAEA_IRQ_BLIT_DONE: u32 = 1 << 0;
const ASTRAEA_IRQ_DRAW_DONE: u32 = 1 << 3;
const VEGA_IRQ_VBLANK: u32 = 1 << 0;
const VEGA_CAPS: u32 = 0x0000_0077;
const VEGA_CTRL_DISPLAY_ENABLE: u32 = 1 << 0;
const VEGA_CTRL_FRAMEBUFFER_ENABLE: u32 = 1 << 1;
const VEGA_FORMAT_RGB565: u32 = 0;
const VEGA_FORMAT_INDEX8: u32 = 1;
const VEGA_PRESENT_SUBMIT: u32 = 1 << 0;
const VEGA_PRESENT_PENDING: u32 = 1 << 0;
const VEGA_PRESENT_DONE: u32 = 1 << 2;
const VEGA_PRESENT_INVALID: u32 = 1 << 3;
const VEGA_FRAME_CYCLES: u64 = CPU_HZ / 60;
const IRQ_SOURCE_VEGA: u32 = 8;
const IRQ_SOURCE_ASTRAEA: u32 = 9;
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
    mask: u32,
    src_pitch: u32,
    dst_pitch: u32,
    mask_pitch: u32,
    dim: u32,
    op: u32,
    color: u32,
    key: u32,
    status: u32,
    fence: u32,
    draw_dst: u32,
    draw_dst_pitch: u32,
    draw_format: u32,
    draw_clip_min: u32,
    draw_clip_max: u32,
    draw_fg: u32,
    draw_bg: u32,
    draw_src: u32,
    draw_src_pitch: u32,
    draw_src_size: u32,
    draw_work: u32,
    draw_work_entries: u32,
    draw_op: u32,
    draw_status: u32,
    draw_fence: u32,
    irq_enable: u32,
    irq_status: u32,
    blit_irq_enable: bool,
    draw_irq_enable: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct Vega {
    irq_enable: u32,
    irq_status: u32,
    frame_counter: u32,
    next_vblank: u64,
    shadow_ctrl: u32,
    active_ctrl: u32,
    shadow_mode: u32,
    active_mode: u32,
    shadow_backdrop: u32,
    active_backdrop: u32,
    shadow_generation: u32,
    shadow_draw_fence: u32,
    shadow_blit_fence: u32,
    shadow_fb_base: u32,
    active_fb_base: u32,
    shadow_fb_pitch: u32,
    active_fb_pitch: u32,
    shadow_fb_format: u32,
    active_fb_format: u32,
    shadow_fb_colorkey: u32,
    active_fb_colorkey: u32,
    shadow_fb_view: u32,
    active_fb_view: u32,
    shadow_fb_virtual: u32,
    active_fb_virtual: u32,
    shadow_fb_wrap: u32,
    active_fb_wrap: u32,
    completed_generation: u32,
    completed_frame: u32,
    retired_fb: u32,
    present_pending: bool,
    present_done: bool,
    present_invalid: bool,
    palette: [u32; 256],
}

impl Default for Vega {
    fn default() -> Self {
        Self {
            irq_enable: 0,
            irq_status: 0,
            frame_counter: 0,
            next_vblank: VEGA_FRAME_CYCLES,
            shadow_ctrl: 0,
            active_ctrl: 0,
            shadow_mode: 0,
            active_mode: 0,
            shadow_backdrop: 0,
            active_backdrop: 0,
            shadow_generation: 0,
            shadow_draw_fence: 0,
            shadow_blit_fence: 0,
            shadow_fb_base: 0,
            active_fb_base: 0,
            shadow_fb_pitch: 0,
            active_fb_pitch: 0,
            shadow_fb_format: 0,
            active_fb_format: 0,
            shadow_fb_colorkey: 0,
            active_fb_colorkey: 0,
            shadow_fb_view: 0,
            active_fb_view: 0,
            shadow_fb_virtual: 0,
            active_fb_virtual: 0,
            shadow_fb_wrap: 0,
            active_fb_wrap: 0,
            completed_generation: 0,
            completed_frame: 0,
            retired_fb: 0,
            present_pending: false,
            present_done: false,
            present_invalid: false,
            palette: [0; 256],
        }
    }
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
    vega: Vega,
    display_generation: u64,
    display_rgba: Option<Arc<[u8]>>,
    scratch: u32,
    scratch_trace: [u32; 32],
    scratch_trace_next: usize,
    scratch_trace_count: usize,
    kernel_ready_seen: bool,
    kernel_soak_seen: bool,
    kernel_panic_seen: bool,
    irq_enable: u32,
    irq_soft: u32,
    irq_config: [u32; 32],
    irq_level_masks: [u32; 8],
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
            vega: Vega::default(),
            display_generation: 0,
            display_rgba: None,
            scratch: 0,
            scratch_trace: [0; 32],
            scratch_trace_next: 0,
            scratch_trace_count: 0,
            kernel_ready_seen: false,
            kernel_soak_seen: false,
            kernel_panic_seen: false,
            irq_enable: 0,
            irq_soft: 0,
            irq_config: [0; 32],
            irq_level_masks: [0; 8],
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
        self.vega = Vega::default();
        self.display_generation = 0;
        self.display_rgba = None;
        self.scratch = 0;
        self.scratch_trace = [0; 32];
        self.scratch_trace_next = 0;
        self.scratch_trace_count = 0;
        self.kernel_ready_seen = false;
        self.kernel_soak_seen = false;
        self.kernel_panic_seen = false;
        self.irq_enable = 0;
        self.irq_soft = 0;
        self.irq_config = [0; 32];
        self.irq_level_masks = [0; 8];
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

    pub(crate) fn rom_ptr(&self) -> *const u8 {
        self.rom.as_ptr()
    }

    pub(crate) fn bram_ptr(&mut self) -> *mut u8 {
        self.bram.as_mut_ptr()
    }

    pub(crate) fn sdram_ptr(&mut self) -> *mut u8 {
        self.sdram.as_mut_ptr()
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
        self.kernel_ready_seen
    }

    pub(crate) fn kernel_soaking(&self) -> bool {
        self.kernel_soak_seen
    }

    pub(crate) fn kernel_panicked(&self) -> bool {
        self.kernel_panic_seen
    }

    pub(crate) fn terminal(&self) -> bool {
        self.post_failed || self.kernel_panicked()
    }

    pub(crate) fn prepare_execution(&mut self, requested: u64) -> (u64, u32) {
        self.refresh_device_events();
        let now = self.current_cycles();
        let next_timer = self
            .timers
            .iter()
            .filter(|timer| timer.control & TIMER_ENABLE != 0)
            .filter_map(|timer| timer.deadline)
            .filter(|deadline| *deadline > now)
            .map(|deadline| deadline - now)
            .min()
            .unwrap_or(requested);
        let next_vblank = self.vega.next_vblank.saturating_sub(now).max(1);
        let next_event = next_timer.min(next_vblank);
        (requested.min(next_event).max(1), self.irq_level())
    }

    pub(crate) fn interrupt_acknowledge(&mut self, level: u32) -> u32 {
        self.refresh_device_events();
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
        self.refresh_device_events();
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

    pub(crate) fn display_generation(&self) -> u64 {
        self.display_generation
    }

    pub(crate) fn display_rgba(&self) -> Option<Arc<[u8]>> {
        self.display_rgba.clone()
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
        if let Some(bytes) = self.read_memory_bytes::<2>(address) {
            return u16::from_be_bytes(bytes);
        }
        if is_mmio(address) && address & 3 <= 2 {
            let shift = 16 - ((address & 3) * 8);
            return (self.read_mmio32(address & !3) >> shift) as u16;
        }
        u16::from_be_bytes([self.read8(address), self.read8(address.wrapping_add(1))])
    }

    pub(crate) fn read32(&mut self, address: u32) -> u32 {
        if let Some(bytes) = self.read_memory_bytes::<4>(address) {
            return u32::from_be_bytes(bytes);
        }
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
        if self.write_memory_bytes(address, &bytes) {
            return;
        }
        self.write8(address, bytes[0]);
        self.write8(address.wrapping_add(1), bytes[1]);
    }

    pub(crate) fn write32(&mut self, address: u32, value: u32) {
        if is_mmio(address) && address & 3 == 0 {
            self.write_mmio32(address, value);
            return;
        }
        let bytes = value.to_be_bytes();
        if self.write_memory_bytes(address, &bytes) {
            return;
        }
        for (offset, byte) in bytes.into_iter().enumerate() {
            self.write8(address.wrapping_add(offset as u32), byte);
        }
    }

    pub(crate) fn access_needs_cycle_sync(address: u32, width: u32) -> bool {
        (0..width).any(|offset| is_mmio(address.wrapping_add(offset)))
    }

    pub(crate) fn write_changes_event_deadline(address: u32) -> bool {
        address == VESTA_BASE + 0x0408 || address == VESTA_BASE + 0x0418
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

    fn read_memory_bytes<const N: usize>(&self, address: u32) -> Option<[u8; N]> {
        let source = if let Some(offset) = range_offset_width(address, 0, ROM_BYTES, N) {
            self.rom.get(offset..offset + N)
        } else if let Some(offset) = range_offset_width(address, ROM_BASE, ROM_BYTES, N) {
            self.rom.get(offset..offset + N)
        } else if let Some(offset) = range_offset_width(address, BRAM_BASE, BRAM_BYTES, N) {
            self.bram.get(offset..offset + N)
        } else if let Some(offset) = range_offset_width(address, SDRAM_BASE, RAM_BYTES as usize, N)
        {
            self.sdram.get(offset..offset + N)
        } else if let Some(offset) =
            range_offset_width(address, VEGA_TEXT_BASE, VEGA_TEXT_APERTURE as usize, N)
        {
            self.console.get(offset..offset + N)
        } else {
            None
        }?;

        Some(
            source
                .try_into()
                .expect("validated fixed-width memory span"),
        )
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

    fn write_memory_bytes<const N: usize>(&mut self, address: u32, value: &[u8; N]) -> bool {
        if let Some(offset) = range_offset_width(address, BRAM_BASE, BRAM_BYTES, N) {
            self.bram[offset..offset + N].copy_from_slice(value);
            return true;
        }
        if let Some(offset) = range_offset_width(address, SDRAM_BASE, RAM_BYTES as usize, N) {
            self.sdram[offset..offset + N].copy_from_slice(value);
            return true;
        }
        if let Some(offset) =
            range_offset_width(address, VEGA_TEXT_BASE, VEGA_TEXT_APERTURE as usize, N)
        {
            if let Some(destination) = self.console.get_mut(offset..offset + N) {
                destination.copy_from_slice(value);
                if value.contains(&b'R') || value.contains(&b'E') {
                    self.refresh_terminal_state();
                }
            }
            return true;
        }
        if range_offset_width(address, 0, ROM_BYTES, N).is_some()
            || range_offset_width(address, ROM_BASE, ROM_BYTES, N).is_some()
        {
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
        if (ASTRAEA_BASE..ASTRAEA_BASE + ASTRAEA_APERTURE).contains(&address) {
            return self.read_astraea(address - ASTRAEA_BASE);
        }
        if (VEGA_BASE..VEGA_BASE + VEGA_APERTURE).contains(&address) {
            return self.read_vega(address - VEGA_BASE);
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
        if (ASTRAEA_BASE..ASTRAEA_BASE + ASTRAEA_APERTURE).contains(&address) {
            self.write_astraea(address - ASTRAEA_BASE, value);
            return;
        }
        if (VEGA_BASE..VEGA_BASE + VEGA_APERTURE).contains(&address) {
            self.write_vega(address - VEGA_BASE, value);
        }
    }

    fn read_vesta(&mut self, offset: u32) -> u32 {
        self.refresh_device_events();
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
                self.kernel_ready_seen |=
                    value == KERNEL_STATUS_READY || value == KERNEL_STATUS_SOAK;
                self.kernel_soak_seen |= value == KERNEL_STATUS_SOAK;
                self.kernel_panic_seen |= value == KERNEL_STATUS_PANIC;
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
                let source = ((offset - 0x380) / 4) as usize;
                let source_mask = 1_u32 << source;
                let old_level = (self.irq_config[source] & 7) as usize;
                let config = value & 0x0001_ffff;
                let new_level = (config & 7) as usize;
                self.irq_level_masks[old_level] &= !source_mask;
                self.irq_level_masks[new_level] |= source_mask;
                self.irq_config[source] = config;
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

    fn refresh_device_events(&mut self) {
        self.refresh_timers();
        self.refresh_video();
    }

    fn refresh_video(&mut self) {
        let now = self.current_cycles();

        if now < self.vega.next_vblank {
            return;
        }
        let frames = (now - self.vega.next_vblank) / VEGA_FRAME_CYCLES + 1;
        self.vega.next_vblank = self
            .vega
            .next_vblank
            .saturating_add(frames.saturating_mul(VEGA_FRAME_CYCLES));
        self.vega.frame_counter = self.vega.frame_counter.wrapping_add(frames as u32);
        self.vega.irq_status |= VEGA_IRQ_VBLANK;
        if self.vega.present_pending {
            self.promote_present();
        }
    }

    fn submit_present(&mut self) {
        if self.vega.present_pending || !self.valid_shadow_scene() {
            self.vega.present_invalid = true;
            return;
        }
        self.vega.present_pending = true;
    }

    fn valid_shadow_scene(&self) -> bool {
        if self.vega.shadow_mode > 5 {
            return false;
        }
        if self.vega.shadow_ctrl & VEGA_CTRL_FRAMEBUFFER_ENABLE == 0 {
            return true;
        }
        if self.vega.shadow_fb_format != VEGA_FORMAT_INDEX8
            && self.vega.shadow_fb_format != VEGA_FORMAT_RGB565
        {
            return false;
        }
        if self.vega.shadow_draw_fence > self.astraea.draw_fence
            || self.vega.shadow_blit_fence > self.astraea.fence
        {
            return false;
        }

        let (active_width, active_height) = mode_dimensions(self.vega.shadow_mode);
        let virtual_width = self.vega.shadow_fb_virtual & 0xffff;
        let virtual_height = self.vega.shadow_fb_virtual >> 16;
        let view_x = self.vega.shadow_fb_view & 0xffff;
        let view_y = self.vega.shadow_fb_view >> 16;
        let bytes_per_pixel = if self.vega.shadow_fb_format == VEGA_FORMAT_INDEX8 {
            1
        } else {
            2
        };
        if virtual_width < active_width
            || virtual_height < active_height
            || self.vega.shadow_fb_pitch < virtual_width.saturating_mul(bytes_per_pixel)
        {
            return false;
        }
        if self.vega.shadow_fb_wrap & 1 == 0
            && view_x
                .checked_add(active_width)
                .is_none_or(|end| end > virtual_width)
        {
            return false;
        }
        if self.vega.shadow_fb_wrap & 2 == 0
            && view_y
                .checked_add(active_height)
                .is_none_or(|end| end > virtual_height)
        {
            return false;
        }

        let row_bytes = match virtual_width.checked_mul(bytes_per_pixel) {
            Some(bytes) => bytes,
            None => return false,
        };
        let last_row = match virtual_height
            .checked_sub(1)
            .and_then(|rows| rows.checked_mul(self.vega.shadow_fb_pitch))
        {
            Some(offset) => offset,
            None => return false,
        };
        let end = self
            .vega
            .shadow_fb_base
            .checked_add(last_row)
            .and_then(|address| address.checked_add(row_bytes));
        end.is_some_and(|address| address <= RAM_BYTES)
    }

    fn promote_present(&mut self) {
        self.vega.present_pending = false;
        self.vega.present_done = true;
        self.vega.retired_fb = self.vega.active_fb_base;
        self.vega.active_ctrl = self.vega.shadow_ctrl;
        self.vega.active_mode = self.vega.shadow_mode;
        self.vega.active_backdrop = self.vega.shadow_backdrop;
        self.vega.active_fb_base = self.vega.shadow_fb_base;
        self.vega.active_fb_pitch = self.vega.shadow_fb_pitch;
        self.vega.active_fb_format = self.vega.shadow_fb_format;
        self.vega.active_fb_colorkey = self.vega.shadow_fb_colorkey;
        self.vega.active_fb_view = self.vega.shadow_fb_view;
        self.vega.active_fb_virtual = self.vega.shadow_fb_virtual;
        self.vega.active_fb_wrap = self.vega.shadow_fb_wrap;
        self.vega.completed_generation = self.vega.shadow_generation;
        self.vega.completed_frame = self.vega.frame_counter;
        self.display_rgba = self.render_active_frame();
        self.display_generation = self.display_generation.wrapping_add(1);
    }

    fn render_active_frame(&self) -> Option<Arc<[u8]>> {
        if self.vega.active_ctrl & (VEGA_CTRL_DISPLAY_ENABLE | VEGA_CTRL_FRAMEBUFFER_ENABLE)
            != (VEGA_CTRL_DISPLAY_ENABLE | VEGA_CTRL_FRAMEBUFFER_ENABLE)
        {
            return None;
        }

        let backdrop = self.vega.active_backdrop;
        let mut rgba = vec![0; DISPLAY_WIDTH * DISPLAY_HEIGHT * 4];
        for pixel in rgba.chunks_exact_mut(4) {
            pixel[0] = (backdrop >> 16) as u8;
            pixel[1] = (backdrop >> 8) as u8;
            pixel[2] = backdrop as u8;
            pixel[3] = 0xff;
        }

        let virtual_width = self.vega.active_fb_virtual & 0xffff;
        let virtual_height = self.vega.active_fb_virtual >> 16;
        let view_x = self.vega.active_fb_view & 0xffff;
        let view_y = self.vega.active_fb_view >> 16;
        for output_y in 0..DISPLAY_HEIGHT as u32 {
            for output_x in 0..DISPLAY_WIDTH as u32 {
                let Some((logical_x, logical_y)) =
                    mode_logical_pixel(self.vega.active_mode, output_x, output_y)
                else {
                    continue;
                };
                let mut source_x = view_x + logical_x;
                let mut source_y = view_y + logical_y;
                if source_x >= virtual_width {
                    if self.vega.active_fb_wrap & 1 == 0 || virtual_width == 0 {
                        continue;
                    }
                    source_x %= virtual_width;
                }
                if source_y >= virtual_height {
                    if self.vega.active_fb_wrap & 2 == 0 || virtual_height == 0 {
                        continue;
                    }
                    source_y %= virtual_height;
                }

                let bytes_per_pixel = if self.vega.active_fb_format == VEGA_FORMAT_INDEX8 {
                    1
                } else {
                    2
                };
                let Some(source) = source_y
                    .checked_mul(self.vega.active_fb_pitch)
                    .and_then(|row| {
                        source_x
                            .checked_mul(bytes_per_pixel)
                            .and_then(|x| row.checked_add(x))
                    })
                    .and_then(|offset| self.vega.active_fb_base.checked_add(offset))
                else {
                    continue;
                };
                let color = if self.vega.active_fb_format == VEGA_FORMAT_INDEX8 {
                    let Some(index) = self.sdram.get(source as usize) else {
                        continue;
                    };
                    self.vega.palette[*index as usize]
                } else {
                    let Some(bytes) = self.sdram.get(source as usize..source as usize + 2) else {
                        continue;
                    };
                    let value = u16::from_be_bytes([bytes[0], bytes[1]]);
                    let red = u32::from((value >> 11) & 0x1f) * 255 / 31;
                    let green = u32::from((value >> 5) & 0x3f) * 255 / 63;
                    let blue = u32::from(value & 0x1f) * 255 / 31;
                    (red << 16) | (green << 8) | blue
                };
                let destination = (output_y as usize * DISPLAY_WIDTH + output_x as usize) * 4;
                rgba[destination] = (color >> 16) as u8;
                rgba[destination + 1] = (color >> 8) as u8;
                rgba[destination + 2] = color as u8;
                rgba[destination + 3] = 0xff;
            }
        }
        Some(Arc::from(rgba))
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
        if self.vega.irq_status & self.vega.irq_enable != 0 {
            pending |= 1_u32 << IRQ_SOURCE_VEGA;
        }
        if self.astraea.irq_status
            & (self.astraea.irq_enable
                | (u32::from(self.astraea.blit_irq_enable) * ASTRAEA_IRQ_BLIT_DONE)
                | (u32::from(self.astraea.draw_irq_enable) * ASTRAEA_IRQ_DRAW_DONE))
            != 0
        {
            pending |= 1_u32 << IRQ_SOURCE_ASTRAEA;
        }
        pending
    }

    fn pending_enabled(&self) -> u32 {
        self.pending_raw() & self.irq_enable
    }

    fn irq_level(&self) -> u32 {
        let pending = self.pending_enabled();
        for level in (1..=7).rev() {
            if pending & self.irq_level_masks[level as usize] != 0 {
                return level;
            }
        }
        0
    }

    fn irq_current(&mut self) -> u32 {
        let level = self.irq_level();
        let pending = self.pending_enabled();
        for source in 0..32 {
            let config = self.irq_config[source];
            if level != 0 && pending & (1_u32 << source) != 0 && config & 7 == level {
                self.irq_enable &= !(1_u32 << source);
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
            0x04 => 0x0004_0000,
            0x10 => self.astraea.irq_enable,
            0x14 => self.astraea.irq_status,
            0x18 => 0x0000_00ff,
            0x40 => self.astraea.src,
            0x44 => self.astraea.dst,
            0x48 => self.astraea.mask,
            0x4c => self.astraea.src_pitch,
            0x50 => self.astraea.dst_pitch,
            0x54 => self.astraea.mask_pitch,
            0x58 => self.astraea.dim,
            0x5c => self.astraea.op,
            0x60 => self.astraea.color,
            0x64 => self.astraea.key,
            0x6c => self.astraea.status,
            0x70 => self.astraea.fence,
            0x100 => self.astraea.draw_dst,
            0x104 => self.astraea.draw_dst_pitch,
            0x108 => self.astraea.draw_format,
            0x10c => self.astraea.draw_clip_min,
            0x110 => self.astraea.draw_clip_max,
            0x120 => self.astraea.draw_fg,
            0x124 => self.astraea.draw_bg,
            0x134 => self.astraea.draw_src,
            0x138 => self.astraea.draw_src_pitch,
            0x13c => self.astraea.draw_src_size,
            0x144 => self.astraea.draw_work,
            0x148 => self.astraea.draw_work_entries,
            0x14c => self.astraea.draw_op,
            0x154 => self.astraea.draw_status,
            0x158 => self.astraea.draw_fence,
            _ => 0,
        }
    }

    fn write_astraea(&mut self, offset: u32, value: u32) {
        match offset {
            0x10 => {
                self.astraea.irq_enable = value & (ASTRAEA_IRQ_BLIT_DONE | ASTRAEA_IRQ_DRAW_DONE)
            }
            0x14 => self.astraea.irq_status &= !value,
            0x40 => self.astraea.src = value,
            0x44 => self.astraea.dst = value,
            0x48 => self.astraea.mask = value,
            0x4c => self.astraea.src_pitch = value,
            0x50 => self.astraea.dst_pitch = value,
            0x54 => self.astraea.mask_pitch = value,
            0x58 => self.astraea.dim = value,
            0x5c => self.astraea.op = value,
            0x60 => self.astraea.color = value,
            0x64 => self.astraea.key = value,
            0x68 => {
                self.astraea.blit_irq_enable = value & BLIT_IRQ_ENABLE != 0;
                if value & 1 != 0 {
                    self.run_blitter();
                }
            }
            0x70 => self.astraea.fence = value,
            0x100 => self.astraea.draw_dst = value,
            0x104 => self.astraea.draw_dst_pitch = value,
            0x108 => self.astraea.draw_format = value,
            0x10c => self.astraea.draw_clip_min = value,
            0x110 => self.astraea.draw_clip_max = value,
            0x120 => self.astraea.draw_fg = value,
            0x124 => self.astraea.draw_bg = value,
            0x134 => self.astraea.draw_src = value,
            0x138 => self.astraea.draw_src_pitch = value,
            0x13c => self.astraea.draw_src_size = value,
            0x144 => self.astraea.draw_work = value,
            0x148 => self.astraea.draw_work_entries = value,
            0x14c => self.astraea.draw_op = value,
            0x150 => {
                self.astraea.draw_irq_enable = value & DRAW_IRQ_ENABLE != 0;
                if value & 1 != 0 {
                    self.run_draw();
                }
            }
            0x158 => self.astraea.draw_fence = value,
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
            _ => return self.blit_complete(0),
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
        self.blit_complete(0);
    }

    fn blit_error(&mut self, code: u32) {
        self.blit_complete(code);
    }

    fn blit_complete(&mut self, error: u32) {
        self.astraea.status = BLIT_DONE | (error << 8);
        self.astraea.irq_status |= ASTRAEA_IRQ_BLIT_DONE;
    }

    fn run_draw(&mut self) {
        self.astraea.draw_status = 1;
        if self.astraea.draw_format != DRAW_FORMAT_INDEX8
            || self.astraea.draw_op & 0xff != DRAW_OP_GLYPH_MASK1
            || self.astraea.draw_dst_pitch == 0
            || self.astraea.draw_src_pitch == 0
            || self.astraea.draw_work_entries == 0
            || self.astraea.draw_work_entries > 1024
        {
            self.draw_complete(DRAW_ERROR_INVALID_CONFIG);
            return;
        }

        let descriptor_bytes = match self.astraea.draw_work_entries.checked_mul(16) {
            Some(bytes) => bytes,
            None => {
                self.draw_complete(DRAW_ERROR_ADDRESS_RANGE);
                return;
            }
        };
        if !self.sdram_offset_range(self.astraea.draw_work, descriptor_bytes) {
            self.draw_complete(DRAW_ERROR_ADDRESS_RANGE);
            return;
        }

        for entry in 0..self.astraea.draw_work_entries {
            let descriptor = self.astraea.draw_work + entry * 16;
            let Some(source_offset) = self.read_sdram_u32(descriptor) else {
                self.draw_complete(DRAW_ERROR_ADDRESS_RANGE);
                return;
            };
            let Some(source_position) = self.read_sdram_u32(descriptor + 4) else {
                self.draw_complete(DRAW_ERROR_ADDRESS_RANGE);
                return;
            };
            let Some(destination_position) = self.read_sdram_u32(descriptor + 8) else {
                self.draw_complete(DRAW_ERROR_ADDRESS_RANGE);
                return;
            };
            let Some(size) = self.read_sdram_u32(descriptor + 12) else {
                self.draw_complete(DRAW_ERROR_ADDRESS_RANGE);
                return;
            };
            if !self.draw_mask1_glyph(source_offset, source_position, destination_position, size) {
                self.draw_complete(DRAW_ERROR_ADDRESS_RANGE);
                return;
            }
        }
        self.draw_complete(0);
    }

    fn draw_mask1_glyph(
        &mut self,
        source_offset: u32,
        source_position: u32,
        destination_position: u32,
        size: u32,
    ) -> bool {
        let source_x = (source_position & 0xffff) as usize;
        let source_y = (source_position >> 16) as usize;
        let destination_x = (destination_position as u16 as i16) as i32;
        let destination_y = ((destination_position >> 16) as u16 as i16) as i32;
        let width = (size & 0xffff) as usize;
        let height = (size >> 16) as usize;
        let source_width = (self.astraea.draw_src_size & 0xffff) as usize;
        let source_height = (self.astraea.draw_src_size >> 16) as usize;
        let clip_min_x = (self.astraea.draw_clip_min as u16 as i16) as i32;
        let clip_min_y = ((self.astraea.draw_clip_min >> 16) as u16 as i16) as i32;
        let clip_max_x = (self.astraea.draw_clip_max as u16 as i16) as i32;
        let clip_max_y = ((self.astraea.draw_clip_max >> 16) as u16 as i16) as i32;
        let source_base = match self.astraea.draw_src.checked_add(source_offset) {
            Some(base) => base,
            None => return false,
        };
        let opaque = self.astraea.draw_op & DRAW_OP_OPAQUE_BACKGROUND != 0;

        if width == 0
            || height == 0
            || source_x
                .checked_add(width)
                .is_none_or(|end| end > source_width)
            || source_y
                .checked_add(height)
                .is_none_or(|end| end > source_height)
            || clip_min_x > clip_max_x
            || clip_min_y > clip_max_y
        {
            return false;
        }

        for row in 0..height {
            for column in 0..width {
                let source_bit = source_x + column;
                let source_row = match u32::try_from(source_y + row)
                    .ok()
                    .and_then(|row| row.checked_mul(self.astraea.draw_src_pitch))
                {
                    Some(offset) => offset,
                    None => return false,
                };
                let source_address = match source_base
                    .checked_add(source_row)
                    .and_then(|address| address.checked_add((source_bit / 8) as u32))
                {
                    Some(address) => address,
                    None => return false,
                };
                let Some(source_byte) = self.sdram.get(source_address as usize).copied() else {
                    return false;
                };
                let set = source_byte & (0x80 >> (source_bit & 7)) != 0;
                if !set && !opaque {
                    continue;
                }

                let x = destination_x + column as i32;
                let y = destination_y + row as i32;
                if x < clip_min_x || x >= clip_max_x || y < clip_min_y || y >= clip_max_y {
                    continue;
                }
                if x < 0 || y < 0 {
                    return false;
                }
                let destination_row = match (y as u32).checked_mul(self.astraea.draw_dst_pitch) {
                    Some(offset) => offset,
                    None => return false,
                };
                let destination = match self
                    .astraea
                    .draw_dst
                    .checked_add(destination_row)
                    .and_then(|address| address.checked_add(x as u32))
                {
                    Some(address) => address,
                    None => return false,
                };
                let Some(pixel) = self.sdram.get_mut(destination as usize) else {
                    return false;
                };
                *pixel = if set {
                    self.astraea.draw_fg as u8
                } else {
                    self.astraea.draw_bg as u8
                };
            }
        }
        true
    }

    fn sdram_offset_range(&self, offset: u32, length: u32) -> bool {
        length != 0
            && offset
                .checked_add(length)
                .is_some_and(|end| end <= self.sdram.len() as u32)
    }

    fn read_sdram_u32(&self, offset: u32) -> Option<u32> {
        let bytes = self
            .sdram
            .get(offset as usize..offset.checked_add(4)? as usize)?;
        Some(u32::from_be_bytes(bytes.try_into().ok()?))
    }

    fn draw_complete(&mut self, error: u32) {
        self.astraea.draw_status = DRAW_DONE | (error << 8);
        self.astraea.irq_status |= ASTRAEA_IRQ_DRAW_DONE;
    }

    fn read_vega(&mut self, offset: u32) -> u32 {
        self.refresh_video();
        if (0x400..0x800).contains(&offset) && offset & 3 == 0 {
            return self.vega.palette[((offset - 0x400) / 4) as usize];
        }
        match offset {
            0x00 => 0x5645_4741,
            0x04 => 0x0005_0000,
            0x08 => self.vega.shadow_ctrl,
            0x0c => (1 << 4) | (u32::from(self.vega.present_pending) << 2),
            0x10 => self.vega.irq_enable,
            0x14 => self.vega.irq_status,
            0x18 => self.vega.shadow_mode,
            0x1c => VEGA_CAPS,
            0x28 => {
                let (width, height) = mode_dimensions(self.vega.shadow_mode);
                (height << 16) | width
            }
            0x30 => self.vega.shadow_backdrop,
            0x34 => self.vega.shadow_generation,
            0x38 => self.vega.shadow_draw_fence,
            0x3c => self.vega.shadow_blit_fence,
            0x40 => self.vega.shadow_fb_base,
            0x44 => self.vega.shadow_fb_pitch,
            0x48 => self.vega.shadow_fb_format,
            0x4c => self.vega.shadow_fb_colorkey,
            0x54 => {
                (u32::from(self.vega.present_pending) * VEGA_PRESENT_PENDING)
                    | (u32::from(self.vega.present_done) * VEGA_PRESENT_DONE)
                    | (u32::from(self.vega.present_invalid) * VEGA_PRESENT_INVALID)
            }
            0x58 => self.vega.completed_generation,
            0x5c => self.vega.completed_frame,
            0x60 => self.vega.retired_fb,
            0x64 => self.vega.frame_counter,
            0x68 => self.vega.shadow_fb_view,
            0x6c => self.vega.shadow_fb_virtual,
            0x70 => self.vega.shadow_fb_wrap,
            _ => 0,
        }
    }

    fn write_vega(&mut self, offset: u32, value: u32) {
        self.refresh_video();
        if (0x400..0x800).contains(&offset) && offset & 3 == 0 {
            self.vega.palette[((offset - 0x400) / 4) as usize] = value & 0x00ff_ffff;
            return;
        }
        match offset {
            0x08 => self.vega.shadow_ctrl = value & 0x1f,
            0x10 => self.vega.irq_enable = value & 0x7,
            0x14 => self.vega.irq_status &= !value,
            0x18 => self.vega.shadow_mode = value & 0x7,
            0x30 => self.vega.shadow_backdrop = value & 0x00ff_ffff,
            0x34 => self.vega.shadow_generation = value,
            0x38 => self.vega.shadow_draw_fence = value,
            0x3c => self.vega.shadow_blit_fence = value,
            0x40 => self.vega.shadow_fb_base = value & 0x01ff_ffff,
            0x44 => self.vega.shadow_fb_pitch = value,
            0x48 => self.vega.shadow_fb_format = value & 0x7,
            0x4c => self.vega.shadow_fb_colorkey = value,
            0x50 if value & VEGA_PRESENT_SUBMIT != 0 => self.submit_present(),
            0x54 => {
                if value & VEGA_PRESENT_DONE != 0 {
                    self.vega.present_done = false;
                }
                if value & VEGA_PRESENT_INVALID != 0 {
                    self.vega.present_invalid = false;
                }
            }
            0x68 => self.vega.shadow_fb_view = value,
            0x6c => self.vega.shadow_fb_virtual = value,
            0x70 => self.vega.shadow_fb_wrap = value & 0x3,
            _ => {}
        }
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
        self.ready_for_loader |= contains(&self.console, b"READY FOR OS LOADER");
        self.post_failed |= contains(&self.console, b"HALTED: POST FAILURE");
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

fn range_offset_width(address: u32, base: u32, length: usize, width: usize) -> Option<usize> {
    let offset = address.checked_sub(base)? as usize;
    let end = offset.checked_add(width)?;
    (end <= length).then_some(offset)
}

fn mode_dimensions(mode: u32) -> (u32, u32) {
    match mode {
        1 => (640, 480),
        2 => (320, 240),
        3 => (320, 200),
        4 => (400, 300),
        5 => (640, 400),
        _ => (720, 480),
    }
}

fn mode_logical_pixel(mode: u32, x: u32, y: u32) -> Option<(u32, u32)> {
    match mode {
        1 if (40..680).contains(&x) && y < 480 => Some((x - 40, y)),
        2 if (40..680).contains(&x) && y < 480 => Some(((x - 40) >> 1, y >> 1)),
        3 if (40..680).contains(&x) && (40..440).contains(&y) => {
            Some(((x - 40) >> 1, (y - 40) >> 1))
        }
        4 if (160..560).contains(&x) && (90..390).contains(&y) => Some((x - 160, y - 90)),
        5 if (40..680).contains(&x) && (40..440).contains(&y) => Some((x - 40, y - 40)),
        0 if x < 720 && y < 480 => Some((x, y)),
        _ => None,
    }
}

fn is_mmio(address: u32) -> bool {
    (VESTA_BASE..VESTA_BASE + VESTA_APERTURE).contains(&address)
        || (FRONT_PANEL_BASE..FRONT_PANEL_BASE + FRONT_PANEL_APERTURE).contains(&address)
        || (UART_BASE..UART_BASE + 0x10).contains(&address)
        || (ASTRAEA_BASE..ASTRAEA_BASE + ASTRAEA_APERTURE).contains(&address)
        || (VEGA_BASE..VEGA_BASE + VEGA_APERTURE).contains(&address)
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
    fn astraea_mask1_batch_renders_indexed_glyphs() {
        const FONT: u32 = 0x3000;
        const DESCRIPTOR: u32 = 0x4000;
        const FRAMEBUFFER: u32 = 0x5000;

        let mut bus = MachineBus::new(&test_rom()).unwrap();
        bus.write8(SDRAM_BASE + FONT, 0b1010_0000);
        bus.write32(SDRAM_BASE + DESCRIPTOR, 0);
        bus.write32(SDRAM_BASE + DESCRIPTOR + 4, 0);
        bus.write32(SDRAM_BASE + DESCRIPTOR + 8, (2 << 16) | 3);
        bus.write32(SDRAM_BASE + DESCRIPTOR + 12, (1 << 16) | 8);

        bus.write32(ASTRAEA_BASE + 0x100, FRAMEBUFFER);
        bus.write32(ASTRAEA_BASE + 0x104, 16);
        bus.write32(ASTRAEA_BASE + 0x108, DRAW_FORMAT_INDEX8);
        bus.write32(ASTRAEA_BASE + 0x10c, 0);
        bus.write32(ASTRAEA_BASE + 0x110, (8 << 16) | 16);
        bus.write32(ASTRAEA_BASE + 0x120, 0xfc);
        bus.write32(ASTRAEA_BASE + 0x134, FONT);
        bus.write32(ASTRAEA_BASE + 0x138, 1);
        bus.write32(ASTRAEA_BASE + 0x13c, (1 << 16) | 8);
        bus.write32(ASTRAEA_BASE + 0x144, DESCRIPTOR);
        bus.write32(ASTRAEA_BASE + 0x148, 1);
        bus.write32(ASTRAEA_BASE + 0x14c, DRAW_OP_GLYPH_MASK1);
        bus.write32(ASTRAEA_BASE + 0x158, 0x1234);
        bus.write32(ASTRAEA_BASE + 0x150, 1);

        assert_eq!(bus.read8(SDRAM_BASE + FRAMEBUFFER + 2 * 16 + 3), 0xfc);
        assert_eq!(bus.read8(SDRAM_BASE + FRAMEBUFFER + 2 * 16 + 4), 0);
        assert_eq!(bus.read8(SDRAM_BASE + FRAMEBUFFER + 2 * 16 + 5), 0xfc);
        assert_eq!(bus.read32(ASTRAEA_BASE + 0x154), DRAW_DONE);
        assert_eq!(bus.read32(ASTRAEA_BASE + 0x158), 0x1234);
        assert_ne!(bus.read32(ASTRAEA_BASE + 0x18) & (1 << 5), 0);
    }

    #[test]
    fn vega_present_retires_indexed_frame_at_vblank() {
        const FRAMEBUFFER: u32 = 0x1000;

        let mut bus = MachineBus::new(&test_rom()).unwrap();
        bus.write8(SDRAM_BASE + FRAMEBUFFER, 7);
        bus.write32(VEGA_BASE + 0x400 + 7 * 4, 0x0011_2233);
        bus.write32(
            VEGA_BASE + 0x08,
            VEGA_CTRL_DISPLAY_ENABLE | VEGA_CTRL_FRAMEBUFFER_ENABLE,
        );
        bus.write32(VEGA_BASE + 0x40, FRAMEBUFFER);
        bus.write32(VEGA_BASE + 0x44, DISPLAY_WIDTH as u32);
        bus.write32(VEGA_BASE + 0x48, VEGA_FORMAT_INDEX8);
        bus.write32(
            VEGA_BASE + 0x6c,
            ((DISPLAY_HEIGHT as u32) << 16) | DISPLAY_WIDTH as u32,
        );
        bus.write32(VEGA_BASE + 0x34, 42);
        bus.write32(VEGA_BASE + 0x50, VEGA_PRESENT_SUBMIT);

        assert_eq!(
            bus.read32(VEGA_BASE + 0x54) & VEGA_PRESENT_PENDING,
            VEGA_PRESENT_PENDING
        );
        assert!(bus.display_rgba().is_none());

        bus.finish_timeslice(VEGA_FRAME_CYCLES);
        assert_eq!(bus.read32(VEGA_BASE + 0x58), 42);
        assert_eq!(
            bus.read32(VEGA_BASE + 0x54) & VEGA_PRESENT_DONE,
            VEGA_PRESENT_DONE
        );
        assert_eq!(&bus.display_rgba().unwrap()[..4], &[0x11, 0x22, 0x33, 0xff]);
        assert_eq!(bus.display_generation(), 1);

        bus.write32(VEGA_BASE + 0x08, 0);
        bus.write32(VEGA_BASE + 0x34, 43);
        bus.write32(VEGA_BASE + 0x50, VEGA_PRESENT_SUBMIT);
        bus.finish_timeslice(VEGA_FRAME_CYCLES);
        assert_eq!(bus.read32(VEGA_BASE + 0x58), 43);
        assert!(bus.display_rgba().is_none());
        assert_eq!(bus.display_generation(), 2);
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
        assert_eq!(bus.interrupt_acknowledge(4), 80);
        assert_eq!(bus.read32(VESTA_BASE + 0x310), IRQ_VALID | (80 << 16) | 4);
        assert_eq!(bus.read32(VESTA_BASE + 0x304) & 1, 0);

        bus.write32(VESTA_BASE + 0x40c, TIMER_EXPIRED);
        assert_eq!(bus.read32(VESTA_BASE + 0x300), 0);
        assert_eq!(bus.prepare_execution(1_000), (100, 0));
    }

    #[test]
    fn interrupt_priority_masks_follow_source_reconfiguration() {
        let mut bus = MachineBus::new(&test_rom()).unwrap();

        bus.write32(VESTA_BASE + 0x380, (80 << 8) | 4);
        bus.write32(VESTA_BASE + 0x304, 1);
        bus.write32(VESTA_BASE + 0x308, 1);
        assert_eq!(bus.prepare_execution(1_000).1, 4);

        bus.write32(VESTA_BASE + 0x380, (80 << 8) | 6);
        assert_eq!(bus.prepare_execution(1_000).1, 6);

        bus.write32(VESTA_BASE + 0x380, 80 << 8);
        assert_eq!(bus.prepare_execution(1_000).1, 0);
    }

    #[test]
    fn vega_vblank_drives_a_vectored_interrupt() {
        let mut bus = MachineBus::new(&test_rom()).unwrap();

        bus.write32(VESTA_BASE + 0x380 + IRQ_SOURCE_VEGA * 4, (84 << 8) | 3);
        bus.write32(VESTA_BASE + 0x304, 1 << IRQ_SOURCE_VEGA);
        bus.write32(VEGA_BASE + 0x10, VEGA_IRQ_VBLANK);
        assert_eq!(
            bus.prepare_execution(VEGA_FRAME_CYCLES + 100).0,
            VEGA_FRAME_CYCLES
        );

        bus.finish_timeslice(VEGA_FRAME_CYCLES);
        assert_eq!(bus.prepare_execution(1_000).1, 3);
        assert_ne!(bus.read32(VESTA_BASE + 0x300) & (1 << IRQ_SOURCE_VEGA), 0);
        assert_eq!(
            bus.read32(VESTA_BASE + 0x310),
            IRQ_VALID | (84 << IRQ_VECTOR_SHIFT) | (IRQ_SOURCE_VEGA << IRQ_SOURCE_SHIFT) | 3
        );
        assert_eq!(bus.read32(VEGA_BASE + 0x14), VEGA_IRQ_VBLANK);

        bus.write32(VEGA_BASE + 0x14, VEGA_IRQ_VBLANK);
        assert_eq!(bus.read32(VESTA_BASE + 0x300) & (1 << IRQ_SOURCE_VEGA), 0);
    }

    #[test]
    fn astraea_noop_completion_drives_a_vectored_interrupt() {
        let mut bus = MachineBus::new(&test_rom()).unwrap();

        bus.write32(VESTA_BASE + 0x380 + IRQ_SOURCE_ASTRAEA * 4, (84 << 8) | 3);
        bus.write32(VESTA_BASE + 0x304, 1 << IRQ_SOURCE_ASTRAEA);
        bus.write32(ASTRAEA_BASE + 0x10, ASTRAEA_IRQ_BLIT_DONE);
        bus.write32(ASTRAEA_BASE + 0x58, 0);
        bus.write32(ASTRAEA_BASE + 0x68, 1 | BLIT_IRQ_ENABLE);

        assert_eq!(bus.read32(ASTRAEA_BASE + 0x6c), BLIT_DONE);
        assert_eq!(bus.read32(ASTRAEA_BASE + 0x14), ASTRAEA_IRQ_BLIT_DONE);
        assert_eq!(bus.prepare_execution(1_000).1, 3);
        assert_ne!(
            bus.read32(VESTA_BASE + 0x300) & (1 << IRQ_SOURCE_ASTRAEA),
            0
        );

        bus.write32(ASTRAEA_BASE + 0x14, ASTRAEA_IRQ_BLIT_DONE);
        assert_eq!(
            bus.read32(VESTA_BASE + 0x300) & (1 << IRQ_SOURCE_ASTRAEA),
            0
        );
    }

    #[test]
    fn kernel_status_controls_machine_completion() {
        let mut bus = MachineBus::new(&test_rom()).unwrap();

        bus.write32(VESTA_BASE + 0x018, KERNEL_STATUS_READY);
        assert!(bus.kernel_ready());
        assert!(!bus.kernel_soaking());
        assert!(!bus.terminal());
        bus.write32(VESTA_BASE + 0x018, 1);
        assert!(bus.kernel_ready());

        bus.write32(VESTA_BASE + 0x018, KERNEL_STATUS_SOAK);
        assert!(bus.kernel_ready());
        assert!(bus.kernel_soaking());
        assert!(!bus.terminal());
        bus.write32(VESTA_BASE + 0x018, 2);
        assert!(bus.kernel_soaking());

        bus.write32(VESTA_BASE + 0x018, KERNEL_STATUS_PANIC);
        assert!(bus.kernel_panicked());
        assert!(bus.terminal());
        bus.write32(VESTA_BASE + 0x018, 3);
        assert!(bus.kernel_panicked());
        assert_eq!(
            bus.scratch_trace(),
            vec![
                KERNEL_STATUS_READY,
                1,
                KERNEL_STATUS_SOAK,
                2,
                KERNEL_STATUS_PANIC,
                3,
            ]
        );

        for value in 0..40 {
            bus.write32(VESTA_BASE + 0x018, value);
        }
        assert_eq!(bus.scratch_trace(), (8..40).collect::<Vec<_>>());

        bus.reset();
        assert_eq!(bus.scratch(), 0);
        assert!(bus.scratch_trace().is_empty());
        assert!(!bus.kernel_ready());
        assert!(!bus.kernel_soaking());
        assert!(!bus.kernel_panicked());
    }
}
