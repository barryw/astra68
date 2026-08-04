use crate::RAM_BYTES;
use crate::bus::{BRAM_BASE, BRAM_BYTES, MachineBus, ROM_BASE, ROM_BYTES, SDRAM_BASE};
use core::cell::Cell;
use core::ffi::{c_int, c_uint, c_void};
use std::sync::{Mutex, MutexGuard, Once};

const M68K_CPU_TYPE_68030: c_uint = 6;
const M68K_REG_PC: c_int = 16;

static INITIALIZE: Once = Once::new();
static CPU_LOCK: Mutex<()> = Mutex::new(());

thread_local! {
    static ACTIVE_BUS: Cell<*mut MachineBus> = const { Cell::new(core::ptr::null_mut()) };
    static EXECUTING: Cell<bool> = const { Cell::new(false) };
    static ACTIVE_IRQ_LEVEL: Cell<u32> = const { Cell::new(0) };
}

unsafe extern "C" {
    fn m68k_init();
    fn m68k_set_cpu_type(cpu_type: c_uint);
    fn m68k_set_irq(level: c_uint);
    fn m68k_set_int_ack_callback(callback: Option<extern "C" fn(c_int) -> c_int>);
    fn m68k_pulse_reset();
    fn m68k_execute(cycles: c_int) -> c_int;
    fn m68k_cycles_run() -> c_int;
    fn m68k_end_timeslice();
    fn m68k_context_size() -> c_uint;
    fn m68k_get_context(destination: *mut c_void) -> c_uint;
    fn m68k_set_context(source: *mut c_void);
    fn m68k_get_reg(context: *mut c_void, register: c_int) -> c_uint;
    fn m68k_set_pmmu_bus_callbacks(
        read32: Option<extern "C" fn(c_uint, *mut c_uint) -> c_int>,
        write32: Option<extern "C" fn(c_uint, c_uint) -> c_int>,
    );
    fn astra_m68k_set_memory_regions(regions: *const MemoryRegions);
}

#[repr(C)]
struct MemoryRegions {
    rom: *const u8,
    rom_base: c_uint,
    rom_size: c_uint,
    bram: *mut u8,
    bram_base: c_uint,
    bram_size: c_uint,
    sdram: *mut u8,
    sdram_base: c_uint,
    sdram_size: c_uint,
}

pub(crate) struct MusashiCpu {
    context: Vec<usize>,
}

impl MusashiCpu {
    pub(crate) fn new(bus: &mut MachineBus) -> Self {
        INITIALIZE.call_once(|| unsafe { m68k_init() });
        let context_bytes = unsafe { m68k_context_size() } as usize;
        let words = context_bytes.div_ceil(size_of::<usize>());
        let mut cpu = Self {
            context: vec![0; words],
        };
        cpu.reset(bus);
        cpu
    }

    pub(crate) fn reset(&mut self, bus: &mut MachineBus) {
        let _lock = lock_cpu();
        unsafe { configure_memory(bus) };
        with_active_bus(bus, false, || unsafe {
            m68k_set_cpu_type(M68K_CPU_TYPE_68030);
            m68k_set_pmmu_bus_callbacks(Some(pmmu_read32), Some(pmmu_write32));
            m68k_set_int_ack_callback(Some(interrupt_acknowledge));
            m68k_pulse_reset();
            m68k_get_context(self.context.as_mut_ptr().cast());
        });
    }

    pub(crate) fn execute(&mut self, bus: &mut MachineBus, cycles: u64) -> u64 {
        if cycles == 0 || bus.terminal() {
            return 0;
        }

        let (budget, irq_level) = bus.prepare_execution(cycles);
        let requested = budget.min(c_int::MAX as u64) as c_int;
        let _lock = lock_cpu();
        unsafe { configure_memory(bus) };
        let ran = with_active_bus(bus, true, || unsafe {
            m68k_set_context(self.context.as_mut_ptr().cast());
            set_interrupt_level(irq_level);
            let ran = m68k_execute(requested).max(0) as u64;
            m68k_get_context(self.context.as_mut_ptr().cast());
            ran
        });
        bus.finish_timeslice(ran);
        ran
    }

    pub(crate) fn pc(&self) -> u32 {
        let _lock = lock_cpu();
        unsafe { m68k_get_reg(self.context.as_ptr().cast_mut().cast(), M68K_REG_PC) }
    }
}

unsafe fn configure_memory(bus: &mut MachineBus) {
    let regions = MemoryRegions {
        rom: bus.rom_ptr(),
        rom_base: ROM_BASE,
        rom_size: ROM_BYTES as c_uint,
        bram: bus.bram_ptr(),
        bram_base: BRAM_BASE,
        bram_size: BRAM_BYTES as c_uint,
        sdram: bus.sdram_ptr(),
        sdram_base: SDRAM_BASE,
        sdram_size: RAM_BYTES,
    };
    unsafe { astra_m68k_set_memory_regions(&regions) };
}

fn lock_cpu() -> MutexGuard<'static, ()> {
    CPU_LOCK
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

fn with_active_bus<T>(bus: &mut MachineBus, executing: bool, operation: impl FnOnce() -> T) -> T {
    struct ActiveBusGuard;

    impl Drop for ActiveBusGuard {
        fn drop(&mut self) {
            ACTIVE_BUS.with(|active| active.set(core::ptr::null_mut()));
            EXECUTING.with(|state| state.set(false));
        }
    }

    ACTIVE_BUS.with(|active| {
        assert!(
            active.get().is_null(),
            "nested Musashi execution is unsupported"
        );
        active.set(bus);
    });
    EXECUTING.with(|state| state.set(executing));
    let _guard = ActiveBusGuard;
    operation()
}

fn with_bus<T>(
    default: T,
    synchronize_cycles: bool,
    operation: impl FnOnce(&mut MachineBus) -> T,
) -> T {
    ACTIVE_BUS.with(|active| {
        let bus = active.get();
        if bus.is_null() {
            return default;
        }

        unsafe {
            if synchronize_cycles {
                let offset = EXECUTING.with(|state| {
                    if state.get() {
                        m68k_cycles_run().max(0) as u64
                    } else {
                        0
                    }
                });
                (*bus).set_cycle_offset(offset);
            }
            operation(&mut *bus)
        }
    })
}

fn finish_if_terminal(bus: &MachineBus) {
    if bus.terminal() {
        unsafe { m68k_end_timeslice() };
    }
}

extern "C" fn pmmu_read32(address: c_uint, value: *mut c_uint) -> c_int {
    if value.is_null() {
        return 0;
    }
    let synchronize_cycles = MachineBus::access_needs_cycle_sync(address, 4);
    match with_bus(None, synchronize_cycles, |bus| bus.pmmu_read32(address)) {
        Some(result) => {
            unsafe { *value = result };
            1
        }
        None => 0,
    }
}

extern "C" fn pmmu_write32(address: c_uint, value: c_uint) -> c_int {
    let synchronize_cycles = MachineBus::access_needs_cycle_sync(address, 4);
    with_bus(false, synchronize_cycles, |bus| {
        bus.pmmu_write32(address, value)
    })
    .into()
}

extern "C" fn interrupt_acknowledge(level: c_int) -> c_int {
    with_bus(24, true, |bus| {
        bus.interrupt_acknowledge(level.max(0) as u32) as c_int
    })
}

fn set_interrupt_level(level: u32) -> bool {
    let changed = ACTIVE_IRQ_LEVEL.with(|active| active.replace(level) != level);
    unsafe { m68k_set_irq(level) };
    changed
}

fn update_interrupt_level(bus: &mut MachineBus) -> bool {
    let level = bus.interrupt_level();
    set_interrupt_level(level)
}

#[unsafe(no_mangle)]
pub extern "C" fn astravm_bus_read_memory_8(address: c_uint) -> c_uint {
    let synchronize_cycles = MachineBus::access_needs_cycle_sync(address, 1);
    with_bus(0, synchronize_cycles, |bus| bus.read8(address).into())
}

#[unsafe(no_mangle)]
pub extern "C" fn astravm_bus_read_memory_16(address: c_uint) -> c_uint {
    let synchronize_cycles = MachineBus::access_needs_cycle_sync(address, 2);
    with_bus(0, synchronize_cycles, |bus| bus.read16(address).into())
}

#[unsafe(no_mangle)]
pub extern "C" fn astravm_bus_read_memory_32(address: c_uint) -> c_uint {
    let synchronize_cycles = MachineBus::access_needs_cycle_sync(address, 4);
    with_bus(0, synchronize_cycles, |bus| bus.read32(address))
}

#[unsafe(no_mangle)]
pub extern "C" fn astravm_bus_write_memory_8(address: c_uint, value: c_uint) {
    let synchronize_cycles = MachineBus::access_needs_cycle_sync(address, 1);
    with_bus((), synchronize_cycles, |bus| {
        bus.write8(address, value as u8);
        let irq_changed = update_interrupt_level(bus);
        finish_if_terminal(bus);
        if irq_changed {
            unsafe { m68k_end_timeslice() };
        }
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn astravm_bus_write_memory_16(address: c_uint, value: c_uint) {
    let synchronize_cycles = MachineBus::access_needs_cycle_sync(address, 2);
    with_bus((), synchronize_cycles, |bus| {
        bus.write16(address, value as u16);
        let irq_changed = update_interrupt_level(bus);
        finish_if_terminal(bus);
        if irq_changed {
            unsafe { m68k_end_timeslice() };
        }
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn astravm_bus_write_memory_32(address: c_uint, value: c_uint) {
    let synchronize_cycles = MachineBus::access_needs_cycle_sync(address, 4);
    with_bus((), synchronize_cycles, |bus| {
        bus.write32(address, value);
        let irq_changed = update_interrupt_level(bus);
        finish_if_terminal(bus);
        if irq_changed || MachineBus::write_changes_event_deadline(address) {
            unsafe { m68k_end_timeslice() };
        }
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn astravm_bus_after_memory_write() {
    with_bus((), false, |bus| {
        if update_interrupt_level(bus) {
            unsafe { m68k_end_timeslice() };
        }
    });
}
