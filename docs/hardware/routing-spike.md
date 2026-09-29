# Rev A routing spike (P4a)

Spec: [`../superpowers/specs/2026-09-29-rev-a-p4a-routing-spike-design.md`](../superpowers/specs/2026-09-29-rev-a-p4a-routing-spike-design.md)

*Status: measuring. The recommendation is written in the last task.*

## Measurement log

Every number below was printed by the command beside it.

### Strip (Task 2)

Clean run (`KIPY` is KiCad 10.0's Python):

    KIPY hardware/reva/spike/run.py --method none --layers 2

    built the strip: 72 parts, 13 keepouts, 23 ports in 7.9 s
       keepout J12.S      x 203.00..203.96 y 106.41..108.64
       keepout J12.T      x 203.00..204.06 y 117.66..120.19
       keepout J12.TN     x 203.00..204.06 y 109.36..111.89
       keepout RV38.tab   x 203.00..203.91 y 32.18..35.82
       keepout RV40.tab   x 203.17..206.28 y 48.40..52.04
       keepout RV40.1     x 203.00..203.52 y 41.62..43.82
       keepout RV44.tab   x 203.00..204.66 y 77.18..80.82
       keepout RV48.tab   x 204.29..207.41 y 95.18..98.82
       keepout RV48.1     x 203.00..204.65 y 88.40..90.60
       keepout SW2.2      x 203.00..204.80 y 10.80..12.80
       keepout SW2.3      x 205.30..207.30 y 10.80..12.80
       keepout SW2.5      x 203.00..204.80 y 16.20..18.20
       keepout SW2.6      x 205.30..207.30 y 16.20..18.20
    wrote hardware\reva\spike\out\none-2L.kicad_pcb
        1. nets       52 nets, 0 differ from the intent node for node
        2. anchors    36 panel parts, worst 0.0000 mm off its hole (limit 0.01)
        3. decoupling 3 decouplers, worst 1.778 mm (limit 2.0)
        4. copper     gated: shorting_items 0, clearance 0, hole_clearance 0, hole_to_hole 0, tracks_crossing 0, track_dangling 0 (not gated: copper_edge_clearance 15, courtyards_overlap 6, pth_inside_courtyard 2, silk_edge_clearance 23, silk_over_copper 44, silk_overlap 20, unconnected_items 133)
        5. render     rendered none-2L-top.png, none-2L-bottom.png
    proof took 2.4 s -- GREEN

The 15 `copper_edge_clearance` are the top pot row's pins (pins north) at
y about 7.0 against the assumed edge at y 6: a finding for P4's rail zone.

Placement-stage sabotages, each `KIPY hardware/reva/spike/run.py --method none --layers 2 --sabotage <name>`,
each exit 1 (the `nets` line is followed by a per-net want/got dump, shortened here):

    --sabotage nets
    RED 1. nets       52 nets, 2 differ from the intent node for node
            SABOTAGE want [] got [('D13', '1')]
    --sabotage nets_missing
    RED 1. nets       examined 0 nets
    --sabotage anchors
    RED 2. anchors    36 panel parts, worst 0.5000 mm off its hole (limit 0.01)
            D13 sits 0.500 mm off its hole
    --sabotage anchors_missing
    RED 2. anchors    examined 0 panel parts
    --sabotage decoupling
    RED 3. decoupling 3 decouplers, worst 4.665 mm (limit 2.0)
            C14 is 4.665 mm from U_MUX3's VCC pad
    --sabotage decoupling_missing
    RED 3. decoupling examined 0 decouplers
    --sabotage copper
    RED 4. copper     gated: shorting_items 0, clearance 1, hole_clearance 0, hole_to_hole 0, tracks_crossing 0, track_dangling 1 (not gated: copper_edge_clearance 15, courtyards_overlap 6, pth_inside_courtyard 2, silk_edge_clearance 23, silk_over_copper 44, silk_overlap 20, unconnected_items 134)
            clearance: 1
            track_dangling: 1
