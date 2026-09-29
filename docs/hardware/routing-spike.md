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

### Locked nets (Task 3)

The three rule-bearing nets are hand-routed as data in
`hardware/reva/spike/locked.py` (all B.Cu, no vias, 0.25 mm), locked, and
checked by the new `locked` step, which runs before `copper`. Corners were
drawn against:

    KIPY hardware/reva/spike/run.py --method none --where SENSE_1
       PORT10   1   (204.500, 56.260) B.Cu
       U_MUX5   3   (255.968, 84.551) B.Cu
       U_MUX4   3   (274.767, 42.413) B.Cu
       U_MUX3   3   (224.491, 42.435) B.Cu
       port: PORT10
    KIPY hardware/reva/spike/run.py --method none --where OUT_L
       PORT21   1   (204.500, 101.980) B.Cu
       J17      T   (260.300, 118.920) F.Cu/B.Cu
       port: PORT21
    KIPY hardware/reva/spike/run.py --method none --where OUT_R
       PORT22   1   (204.500, 104.520) B.Cu
       J18      T   (271.800, 118.920) F.Cu/B.Cu
       port: PORT22

Clean run:

    KIPY hardware/reva/spike/run.py --method none --layers 2

    built the strip: 72 parts, 13 keepouts, 23 ports in 8.2 s
    locked 14 items on SENSE_1, OUT_L, OUT_R
    (13 keepout lines as above)
    wrote hardware\reva\spike\out\none-2L.kicad_pcb
        1. nets       52 nets, 0 differ from the intent node for node
        2. anchors    36 panel parts, worst 0.0000 mm off its hole (limit 0.01)
        3. decoupling 3 decouplers, worst 1.778 mm (limit 2.0)
        4. locked     14 locked segments unchanged on SENSE_1, OUT_L, OUT_R
        5. copper     gated: shorting_items 0, clearance 0, hole_clearance 0, hole_to_hole 0, tracks_crossing 0, track_dangling 0 (not gated: copper_edge_clearance 15, courtyards_overlap 6, pth_inside_courtyard 2, silk_edge_clearance 23, silk_over_copper 44, silk_overlap 20, unconnected_items 128)
        6. render     rendered none-2L-top.png, none-2L-bottom.png
    proof took 2.6 s -- GREEN

Segments per net, read back from the saved and reloaded board (every one
still on its own net and locked, so no corner touches a foreign pad):
SENSE_1 7, OUT_L 3, OUT_R 4. `unconnected_items` drops from 133 to 128 --
the 3 SENSE_1 connections plus one each for OUT_L and OUT_R; the report
names none of the three nets as unconnected.

One topology fact the routes had to respect: the port column puts OUT_L
(y 101.98) above OUT_R (y 104.52), while the jacks put OUT_L's tip (J17)
west of OUT_R's (J18). Two tracks with that terminal order cannot both
leave their tips northward without crossing, so OUT_R leaves J18 southward
and passes under J17's tip along y 121.0 (0.875 mm from the outline; the
board's edge rule is 0.5 and `copper_edge_clearance` stays at 15).

Locked-step sabotages, each exit 1:

    KIPY hardware/reva/spike/run.py --method none --layers 2 --sabotage locked
    RED 4. locked     14 locked segments unchanged on SENSE_1, OUT_L, OUT_R
            moved or added: ('SENSE_1', 'B.Cu', 246.0, 84.551, 256.468, 84.551, 0.25)
            gone: ('SENSE_1', 'B.Cu', 246.0, 84.551, 255.968, 84.551, 0.25)
    KIPY hardware/reva/spike/run.py --method none --layers 2 --sabotage locked_missing
    RED 4. locked     examined 0 locked segments

The `locked` sabotage moves a segment end 0.5 mm inside U_MUX5's COM pad,
so only `locked` turns red (`copper` stays green). With the new step
`copper` is step 5; re-run of the Task 2 sabotage:

    KIPY hardware/reva/spike/run.py --method none --layers 2 --sabotage copper
    RED 5. copper     gated: shorting_items 0, clearance 1, hole_clearance 0, hole_to_hole 0, tracks_crossing 0, track_dangling 1 (not gated: copper_edge_clearance 15, courtyards_overlap 6, pth_inside_courtyard 2, silk_edge_clearance 23, silk_over_copper 44, silk_overlap 20, unconnected_items 129)

`ratsnest` and `audio_missing` are proven on the first routed board
(Task 6).
