"""Restore ECP5 LUT-permutation policy after reloading placed JSON.

nextpnr binds serialized BEL placements before rebuilding ECP5 cell metadata.
Its per-slice permutation cache therefore sees every reloaded TRELLIS_COMB as
ordinary logic. Rebinding the placed combinational cells after import refreshes
that cache with the already reconstructed CCU2 and distributed-RAM modes.
"""


def refresh_lut_permutation_policy(context):
    placements = []
    for _, cell in context.cells:
        if str(cell.type) != "TRELLIS_COMB":
            continue
        bel = str(cell.bel)
        if not bel:
            continue
        placements.append((bel, cell, cell.belStrength))

    for bel, _, _ in placements:
        context.unbindBel(bel)
    for bel, cell, strength in placements:
        context.bindBel(bel, cell, strength)

    return len(placements)


if "ctx" in globals():
    refreshed = refresh_lut_permutation_policy(ctx)
    print(
        "Astra ECP5 split-route guard: refreshed LUT-permutation policy "
        "for %d placed TRELLIS_COMB cells" % refreshed
    )
