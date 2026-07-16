# Coarse physical plan for the ULX3S build.
#
# This script runs after nextpnr packs cells and before placement. The goal is
# not to hand-place the design. The goal is to give each board-level subsystem a
# physical neighborhood, then tightly constrain only the small IO-adjacent
# timing islands that actually need it.
#
# Yosys/ABC may give packed FF/LUT cells auto-generated names, so classification
# checks both the packed cell name and the net names attached to its ports.

import os


FLOORPLAN_MODE = os.environ.get(
    "ASTRA_FLOORPLAN_MODE",
    os.environ.get("NOVA_FLOORPLAN_MODE", "critical"),
).strip().lower()
EXPLICIT_ENFORCE = {
    item.strip()
    for item in os.environ.get(
        "ASTRA_FLOORPLAN_ENFORCE",
        os.environ.get("NOVA_FLOORPLAN_ENFORCE", ""),
    ).split(",")
    if item.strip()
}


REGIONS = [
    # Keep the serializer load/shift flops and their cross-domain control close
    # to the GPDI pins without forcing each lane into a tiny island. The old
    # per-lane boxes were electrically attractive but over-constrained nextpnr's
    # legalizer once the design grew.
    {
        "name": "hdmi_ser_gpdi",
        "box": (64, 0, 126, 24),
        "tier": "critical",
        "enforce": True,
        "match": (
            "lattice_ecp5_shift",
            "load_channel",
            "tmds_shift",
            "tmds_pair",
            "ddr_tmds_clock",
            "load_clock",
            "tmds_clock_shift",
            "tmds_clock_pair",
            "tmds_control",
            "tmds_control_synchronizer_chain",
            "load_edge",
        ),
    },

    # The TG68K execution path crosses the external cache and returns to the
    # kernel in the same cycle. Keep the much smaller cache beside the PMMU and
    # bus interface without restricting placement of the 30K-cell CPU core.
    {
        "name": "tg68k_cache",
        "box": (72, 24, 116, 54),
        "tier": "critical",
        "enforce": True,
        "match": ("tg_cache_store_i",),
    },

    # GHDL flattens the VHDL entity hierarchy into tg68k_wrap, but ABC keeps
    # the originating PMMU and ALU net names. These report-only groups let us
    # measure and, when explicitly requested, constrain those smaller CPU
    # neighborhoods independently of the rest of the 68030.
    {
        "name": "tg68k_pmmu",
        "box": (0, 12, 76, 84),
        "tier": "report",
        "enforce": False,
        "prefer_nets": True,
        "match": ("\\pmmu_030.", ".pmmu_030."),
    },
    {
        "name": "tg68k_alu",
        "box": (56, 16, 112, 60),
        "tier": "report",
        "enforce": False,
        "prefer_nets": True,
        "match": ("\\alu.", ".alu."),
    },

    # Top-band HDMI encoder/packet logic. This stays report-only for now:
    # matching HDMI nets can also catch upstream audio/video mix logic, and
    # enforcing that broad cone over-constrains placement.
    {
        "name": "hdmi_pixel",
        "box": (52, 1, 124, 32),
        "tier": "report",
        "enforce": False,
        "match": ("hdmi_inst", "\\hdmi_inst", "packet_picker", "packet_assembler"),
    },

    # SDRAM lives on the east edge. Keep the 75 MHz controller and bridge near
    # that edge so physical SDRAM paths do not cross the chip.
    {
        "name": "sdram_edge",
        "box": (96, 20, 126, 95),
        "tier": "edge",
        "enforce": True,
        "match": (
            "sdram_i",
            "\\sdram_i",
            "sdram_inst",
            "\\sdram_inst",
            "g_sdram_enabled.sdram_i",
            "sdram_bridge_i",
            "sdram_bist_i",
        ),
    },

    # ESP/FTDI pins are on the west edge. Keep the byte transports and host
    # bridge on that side, with enough vertical space for the FIFOs.
    {
        "name": "host_io",
        "box": (0, 50, 50, 95),
        "tier": "edge",
        "enforce": False,
        "match": (
            "host_spi_i",
            "host_boot_i",
            "spi_sd_i",
            "uart_i",
            "uart_rx_fifo_i",
        ),
    },

    # Astraea and Vega analysis regions. They are report-only in the default
    # mode; timing experiments can enforce individual regions explicitly.
    {
        "name": "post_console",
        "box": (52, 4, 96, 36),
        "tier": "video",
        "enforce": False,
        "match": ("post_console_i", "char_mem", "font_mem"),
    },
    {
        "name": "astraea_draw",
        "box": (68, 44, 126, 95),
        "tier": "video",
        "enforce": False,
        "match": ("astraea_i.\\draw_i", "astraea_i.draw_i", "\\draw_i."),
    },
    {
        "name": "vega_sprites",
        # Include four EBR columns across the upper video band. The narrower
        # box consumed 17 of 22 EBRs and left too little routing freedom around
        # the HDMI and cache islands even though LUT utilization was low. Only
        # anchor the sparse EBR/DSP resources; connected LUT/FF logic follows
        # them without turning the complete 4K-cell builder into a hard fence.
        "box": (38, 2, 118, 54),
        "tier": "video",
        "enforce": False,
        "enforce_buckets": ("DP16KD", "MULT18X18D"),
        "match": ("sprite_builder_i",),
    },
    {
        "name": "vega_tiles",
        "box": (62, 10, 116, 48),
        "tier": "video",
        "enforce": False,
        "match": ("tile_builder_i",),
    },
    {
        "name": "astraea_copper",
        "box": (58, 32, 110, 62),
        "tier": "video",
        "enforce": False,
        "match": ("astraea_i.\\copper_i", "astraea_i.copper_i", "\\copper_i."),
    },
    {
        "name": "astraea_blitter_control",
        # Keep the captured operation, FSM decode, and row counters together.
        # These signals form the blitter's high-fanout control cone; allowing
        # the cone to span the complete datapath box creates long CE routes.
        "box": (58, 60, 86, 88),
        "tier": "chip",
        "enforce": False,
        "prefer_nets": True,
        "match": (
            "blitter_i.cfg_op_mem",
            "blitter_i.cfg_mode_mem",
            "blitter_i.cfg_dim_mem",
            "blitter_i.cfg_element_bytes_mem",
            "blitter_i.cfg_row_bytes_mem",
            "blitter_i.word_mode_mem",
            "blitter_i.total_units_mem",
            "blitter_i.state_mem",
            "blitter_i.rows_remaining_mem",
            "blitter_i.units_done_mem",
            "blitter_i.chunk_count_mem",
            "blitter_i.chunk_last_mem",
            "blitter_i.chunk_finishes_row_mem",
            "blitter_i.chunk_start_index_mem",
            "blitter_i.km_elements_remaining_mem",
        ),
    },
    {
        "name": "astraea_blitter_cdc",
        # The launch payload is a bundled-data CDC: CPU-side holding registers
        # feed a mem-domain snapshot while the synchronized start toggle is in
        # flight. Keep both banks and their first-level decode in one compact
        # neighborhood; the rest of the blitter retains the larger box below.
        "box": (60, 54, 88, 86),
        "tier": "chip",
        "enforce": False,
        "match": (
            "blitter_i.cfg_",
            "blitter_i.start_sync_mem",
            "blitter_i.start_seen_mem",
            "blitter_i.start_toggle_cpu",
        ),
    },
    {
        "name": "astraea_blitter",
        "box": (54, 50, 108, 95),
        "tier": "chip",
        "enforce": False,
        "match": ("astraea_i.\\blitter_i", "astraea_i.blitter_i", "\\blitter_i."),
    },
    {
        "name": "lyra_audio",
        "box": (58, 4, 126, 76),
        "tier": "report",
        "enforce": False,
        "match": ("lyra_i",),
    },

    # Remaining Vega cells after the specific builders above.
    {
        "name": "vega_video",
        "box": (58, 2, 122, 50),
        "tier": "report",
        "enforce": False,
        "match": ("g_sdram_enabled.vega_i", "\\vega_i."),
    },

    # Keep the 68030 and its bus-facing logic central. This remains report-only
    # because the CPU is too large for a tight hard region.
    {
        "name": "cpu_mem",
        "box": (2, 4, 86, 88),
        "tier": "chip",
        "enforce": False,
        "match": ("g_tg68k_enabled.tg_cpu", "\\tg_cpu", "boot_memory_map_i"),
    },

    # Host boot is the remaining unclassified SDRAM bus master. The BIST is
    # classified with sdram_edge so its wide response path stays local to the
    # controller instead of crossing the die.
    {
        "name": "bus_masters",
        "box": (18, 54, 82, 95),
        "tier": "chip",
        "enforce": False,
        "match": ("host_boot_i",),
    },

    # Remaining Astraea MMIO and arbitration cells after its engines above.
    {
        "name": "astraea_core",
        "box": (50, 36, 126, 95),
        "tier": "chip",
        "enforce": False,
        "match": ("g_sdram_enabled.astraea_i", "\\astraea_i."),
    },
]


def should_enforce(region):
    name = region["name"]
    tier = region.get("tier", "report")
    if name in EXPLICIT_ENFORCE:
        return True
    if FLOORPLAN_MODE in ("off", "none", "report"):
        return False
    if region.get("enforce", False):
        return True
    if FLOORPLAN_MODE == "critical":
        return tier == "critical"
    if FLOORPLAN_MODE in ("edge", "chip"):
        return tier in ("critical", "edge")
    if FLOORPLAN_MODE == "core":
        return tier in ("critical", "edge", "chip")
    if FLOORPLAN_MODE in ("video", "aggressive", "all"):
        return tier in ("critical", "edge", "chip", "video")
    return region.get("enforce", False)


def cell_net_names(cell):
    names = []
    for _, port in cell.ports:
        try:
            net = port.net
        except Exception:
            continue
        if net is None:
            continue
        try:
            names.append(net.name)
        except Exception:
            pass
    return names


def matches_any(value, needles):
    return any(needle in value for needle in needles)


def classify_cell(cell_name, net_names):
    # A retained wrapper name covers the complete CPU, while GHDL's internal
    # entity names survive only on nets. Let explicitly marked sub-blocks claim
    # their directly connected cells before the broad wrapper-name match.
    for region in REGIONS:
        if region.get("prefer_nets", False) and any(
            matches_any(value, region["match"]) for value in net_names
        ):
            return region["name"]
    # Prefer retained hierarchy in the packed cell name. A shared top-level
    # reset or bus net can inherit one module's alias after flattening; treating
    # that alias as stronger than the cell's own name pulls unrelated logic into
    # the wrong region and creates exactly the long routes this plan avoids.
    for region in REGIONS:
        if matches_any(cell_name, region["match"]):
            return region["name"]
    # ABC-generated cells do not always retain a useful hierarchical name, so
    # use attached nets only as a fallback for those cells.
    for region in REGIONS:
        if any(matches_any(value, region["match"]) for value in net_names):
            return region["name"]
    return None


def region_contains(region, loc):
    x0, y0, x1, y1 = region["box"]
    return x0 <= loc.x <= x1 and y0 <= loc.y <= y1


def cell_bucket(cell):
    try:
        return str(ctx.getBelBucketForCellType(cell.type))
    except Exception:
        return "cell_type:" + str(cell.type)


def count_region_capacity(region_names):
    capacity = {name: {} for name in region_names}
    for bel in ctx.getBels():
        try:
            loc = ctx.getBelLocation(bel)
            bucket = str(ctx.getBelBucketForBel(bel))
        except Exception:
            continue
        for region in REGIONS:
            name = region["name"]
            if region_contains(region, loc):
                capacity[name][bucket] = capacity[name].get(bucket, 0) + 1
    return capacity


for region in REGIONS:
    name = region["name"]
    x0, y0, x1, y1 = region["box"]
    ctx.createRectangularRegion(name, x0, y0, x1, y1)

region_names = [region["name"] for region in REGIONS]
counts = {name: 0 for name in region_names}
constrained_counts = {name: 0 for name in region_names}
bucket_demand = {name: {} for name in region_names}
type_demand = {name: {} for name in region_names}
for cell_name, cell in ctx.cells:
    cell_name = str(cell_name)
    region_name = classify_cell(cell_name, cell_net_names(cell))
    if region_name is None:
        continue
    counts[region_name] += 1
    bucket = cell_bucket(cell)
    cell_type = str(cell.type)
    bucket_demand[region_name][bucket] = bucket_demand[region_name].get(bucket, 0) + 1
    type_demand[region_name][cell_type] = type_demand[region_name].get(cell_type, 0) + 1
    region = next(region for region in REGIONS if region["name"] == region_name)
    if should_enforce(region):
        enforce_buckets = region.get("enforce_buckets")
        if enforce_buckets is None or bucket in enforce_buckets:
            ctx.constrainCellToRegion(cell_name, region_name)
            constrained_counts[region_name] += 1

enforced_regions = [region["name"] for region in REGIONS if should_enforce(region)]
print("Astra floorplan mode: %s; explicit=%s; enforced=%s" % (
    FLOORPLAN_MODE,
    ",".join(sorted(EXPLICIT_ENFORCE)) if EXPLICIT_ENFORCE else "-",
    ",".join(enforced_regions) if enforced_regions else "-",
))
print("Astra floorplan constraints: " + ", ".join("%s=%d" % item for item in sorted(counts.items())))

capacity = count_region_capacity(region_names)
for name in region_names:
    if counts[name] == 0:
        continue
    bucket_parts = []
    for bucket, demand in sorted(bucket_demand[name].items()):
        cap = capacity[name].get(bucket, 0)
        spare = cap - demand
        used = 100.0 * demand / cap if cap else 0.0
        bucket_parts.append("%s=%d/%d %.1f%% spare=%d" % (bucket, demand, cap, used, spare))

    top_types = sorted(type_demand[name].items(), key=lambda item: (-item[1], item[0]))[:4]
    type_parts = ["%s=%d" % item for item in top_types]
    enforced = "yes" if should_enforce(next(region for region in REGIONS if region["name"] == name)) else "no"
    print("Astra floorplan region %s: enforced=%s; cells=%d; constrained=%d; buckets: %s; top cell types: %s" %
          (name, enforced, counts[name], constrained_counts[name],
           ", ".join(bucket_parts), ", ".join(type_parts)))
