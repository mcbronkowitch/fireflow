#!/usr/bin/env python3
"""Every signal net of the coupon as explicit track data. Millimetres, y grows
downward, origin top-left -- the same frame as placement.py.

Data, not a router. Every corner below was placed by hand against measured pad
coordinates and re-measured by `build_pcb.py`'s own gates; there is no search
here to re-run and no cost function to re-tune. That is deliberate. A generated
route would be unreviewable at exactly the moment this coupon matters -- when a
measurement comes out wrong and the question is which piece of copper is to
blame -- and the board is small enough that the honest answer is a list.

Endpoints are named, never numeric: `("pad", "U_SM", "A5")` is resolved from
the built board by `pad_xy()`, so moving a part in `placement.py` moves the
track's end with it and the corner coordinates in between are the only thing
that has to be re-thought. Raw `(x, y)` appears only for corners -- points that
belong to the route rather than to a part.

Three facts the geometry below is built around, all read off the board rather
than assumed:

- **The SM's own pad rows are a wall.** Banks A and D are 2x5 grids on a
  2.54 mm pitch with ~1.7 mm through-hole pads, so the gap between two rows is
  1.27 mm of centre-to-copper minus 0.85 mm of pad radius: 0.42 mm. A 0.25 mm
  signal track threads that with 0.295 mm to spare; the 0.8 mm supply tracks
  cannot, which is why +-12 V leaves bank A downward (y 44.5) and climbs the
  right column instead of cutting across the bank.
- **F.Cu under an SOIC body is empty.** Both muxes and all three chain
  packages carry their pads in two columns with 6..7 mm of clear laminate
  between them, and most of the short hops below use it. B.Cu is reserved for
  the crossings that genuinely have nowhere to go on F.Cu.
- **The plane stitching got there first.** Task 3 left 79 vias and their
  0.5 mm tracks scattered one per SMD plane pad; they are obstacles here, and
  `check_stitch_hygiene()` polices this task's copper against them with the
  same hard zero it polices Task 3's with.

Rule 2 of the spec -- audio at least 10 mm from `SR_CLK` and the LED nets --
is honoured outside the SM footprint, per the controller ruling recorded in
`placement.py`. The audio pair leaves B1/B2 straight downward and turns west
only once it is south of the module, which puts the whole run in the analog
band while `SR_CLK` and the LED nets stay north of y 44.
"""

# Spec Section 4 widths: signal, supply rails, and the Eurorack +-12 V pair.
W_SIG = 0.25
W_SUP = 0.5
W_12V = 0.8


def _p(ref, number):
    """Endpoint shorthand: the pad, wherever placement.py currently puts it."""
    return ("pad", ref, str(number))


# (net_name, layer, width_mm, [endpoint, ...]); endpoint is
# ("pad", ref, pad_number) or (x_mm, y_mm).
TRACKS = []

# (net_name, (x_mm, y_mm))
VIAS = []


# --- +-12 V: IDC -> SM bank A -> bulk caps -------------------------------
# 0.8 mm the whole way, and the only nets on the board that never touch a
# plane. Bank A's bottom row (y 41.27) has +12 V on its westmost pad A5 and
# -12 V on its eastmost A1, five pads apart, so the two rails leave the module
# in opposite directions and never share a corridor:
#
#   +12 V  A5 -> south out of the bank (y 44.5) -> east -> up the x 86.0 lane,
#          which passes west of every J_PWR pad (nearest edge x 87.15) --
#          taps pins 9/10 on the way -> over the header's top (y 19.6) ->
#          down into C_BP12 pad 1 from the west at its own y.
#   -12 V  A1 -> east at its own y to the x 84.6 lane -> north to the header's
#          top row -> pins 1/2 -> east at x 92.5, in the 4.4 mm channel
#          between the header's pad column (edge x 91.39) and the bulk-cap
#          column (edge x 95.775) -> C_BN12 pad 1.
#
# The two never cross: +12 V is north of y 21.04 wherever it is east of
# x 92.5, and -12 V is south of y 22 there. Nothing approaches a bulk cap
# from directly above or below -- each cap's other pad is GND, 2.08 mm away
# on the same x, so both rails come in sideways at the pad's own y.
TRACKS += [
    ("+12V", "F.Cu", W_12V, [_p("U_SM", "A5"), (71.92, 44.5), (86.0, 44.5),
                             (86.0, 19.6), (93.0, 19.6), (93.0, 21.038),
                             _p("C_BP12", 1)]),
    ("+12V", "F.Cu", W_12V, [(86.0, 32.16), _p("J_PWR", 9), _p("J_PWR", 10)]),
    ("-12V", "F.Cu", W_12V, [_p("U_SM", "A1"), (84.6, 41.27), (84.6, 22.0),
                             (85.0, 22.0)]),
    ("-12V", "B.Cu", W_12V, [(85.0, 22.0), _p("J_PWR", 1)]),
    ("-12V", "F.Cu", W_12V, [_p("J_PWR", 1), _p("J_PWR", 2), (92.5, 22.0),
                             (92.5, 25.038), _p("C_BN12", 1)]),
]

# The board's only unavoidable crossing, and its only supply-side via.
# -12 V leaves A1 heading east and has to enter J_PWR pin 1 from the west;
# +12 V leaves A5 heading north up the same western corridor. Both rails are
# pinned at both ends -- A5/A1 five pads apart on bank A's bottom row, the
# header's -12 V pins on its top row and its +12 V pins on its bottom row,
# and the two bulk caps stacked so each can only be entered sideways at its
# own pad's y (each cap's other pad is GND 2.08 mm away on the same x, and
# the cap column is a solid wall of copper from y 18.37 to 29.93). Every
# F.Cu-only ordering of those four fixed points was tried on paper and each
# produced exactly this crossing somewhere else; the first draft's DRC report
# named it outright ("tracks_crossing 1"). So -12 V dives under the +12 V lane
# for 3 mm instead. One via, not two: J_PWR pin 1 is a through-hole pad, so
# the B.Cu run terminates in copper that is already on every layer.
VIAS += [
    ("-12V", (85.0, 22.0)),
]


# --- the two COM clusters, in the order the spec's Section 4 rule 1 names ---
# mux COM pin -> test point -> DNP capacitor pad -> 0 R link, F.Cu only, one
# layer, no via, so the settling measurement sees the node and not a stack of
# barrels. Placement built the chain to be walked in that order; the numbers
# below are the walk.
#
# MUX16: 4.35 + 2.96 + 4.44 = 11.75 mm. The last leg does NOT go straight from
# the capacitor pad to the 0 R -- a straight line from C_COM16 pad 1 to R_S16
# pad 1 passes 0.016 mm from C_COM16's own AGND pad, measured, which is a
# short in everything but name. It steps north out of the pad first and runs
# west at y 50.6, threading between that AGND pad (edge y 51.275) and JP_3V3's
# A+3V3 stitching via at (82.08, 49.75).
#
# MUX8: 7.45 + 2.46 + 4.11 = 14.02 mm. COM is pin 3, and pins 1 and 2 sit
# directly between it and TP_COM8 on the same pad column, so the run leaves
# the package westward and climbs at x 72.7. Not 72.9, which was the first
# draft and which DRC measured at 0.025 mm to U_MUX8 pad 1: a SOIC-16 pad is
# 1.95 mm long, so its edge stands at x 73.05 and the assumed 1.5 mm pad was
# 0.45 mm too narrow. 72.7 leaves 0.225 mm. That detour is the 2.6 mm this
# net spends above the placement's 11.4 mm ideal; still inside the spec's 15.
TRACKS += [
    ("MUX16_COM", "F.Cu", W_SIG, [_p("U_MUX16", 1), _p("TP_COM16", 1),
                                  _p("C_COM16", 1), (83.537, 50.6),
                                  (79.54, 50.6), _p("R_S16", 1)]),
    ("MUX8_COM", "F.Cu", W_SIG, [_p("U_MUX8", 3), (72.7, 60.095), (72.7, 55.3),
                                 _p("TP_COM8", 1), _p("C_COM8", 1),
                                 _p("R_S8", 1)]),
]


# --- COM to the SM's sense pins ------------------------------------------
# 6.73 mm of straight vertical each, R_S16 pad 2 to A2 and R_S8 pad 2 to A3 --
# except that the +12 V corridor at y 44.5 lies across both of them. +12 V is
# pinned at A5, the westmost pad of bank A's bottom row, and has to reach the
# header east of the module; it cannot climb between bank A's rows (0.42 mm of
# clear laminate, less than half what a 0.8 mm track needs) and every route
# north of the bank collides with the -12 V lane instead. So the sense drops
# dive to B.Cu 1 mm below their 0 R and arrive from underneath. One via each,
# not two: A2 and A3 are through-hole pads in the module's landing pattern, so
# the B.Cu run ends in copper that is already on every layer. B.Cu under the
# module is otherwise empty -- the only things down there are the landing
# pattern's own holes, 2.54 mm away on either side.
TRACKS += [
    ("SENSE_ADC9_MUX16", "F.Cu", W_SIG, [_p("R_S16", 2), (79.54, 46.2)]),
    ("SENSE_ADC9_MUX16", "B.Cu", W_SIG, [(79.54, 46.2), _p("U_SM", "A2")]),
    ("SENSE_ADC10_MUX8", "F.Cu", W_SIG, [_p("R_S8", 2), (77.0, 46.2)]),
    ("SENSE_ADC10_MUX8", "B.Cu", W_SIG, [(77.0, 46.2), _p("U_SM", "A3")]),
]
VIAS += [
    ("SENSE_ADC9_MUX16", (79.54, 46.2)),
    ("SENSE_ADC10_MUX8", (77.0, 46.2)),
]


# --- the two spare ADC probe points --------------------------------------
# D8 and D9 are boxed in by bank D on three sides, so both leave northward --
# into a three-lane wall that runs the width of the board: SR_CLK's F.Cu arm
# at y 4.6, BTN_1's B.Cu arm at y 5.5 and SR_DATA_IN's B.Cu arm at y 6.5.
# No via fits between them (each needs 0.625 mm and the gaps are 0.9 and 1.0),
# and no single layer clears all three. What does clear them is their EASTERN
# ends: BTN_1's stops at x 84.5 and SR_DATA_IN's at 82.08. So both probes run
# east first, through the 0.79 mm corridor between bank D's pads and the
# button's, and climb at x 88 -- in the 4.5 mm gap between the button's two
# contact pairs, where all three arms have already finished.
TRACKS += [
    # D8 is the westerly of the two, so it takes the y 5.5 line -- 0.9 mm
    # south of SR_CLK's arm and directly over BTN_1's B.Cu one -- and D9 the
    # 0.79 mm corridor at y 9.4. Drawn the other way round the two crossed
    # each other at (79.54, 9.4), which is what DRC reported.
    ("SENSE_ADC11_TP", "F.Cu", W_SIG, [_p("U_SM", "D8"), (77.0, 5.5), (88.0, 5.5)]),
    ("SENSE_ADC11_TP", "B.Cu", W_SIG, [(88.0, 5.5), (88.0, 3.0), (86.5, 3.0)]),
    ("SENSE_ADC11_TP", "F.Cu", W_SIG, [(86.5, 3.0), _p("TP_ADC11", 1)]),
    ("SENSE_ADC12_TP", "F.Cu", W_SIG, [_p("U_SM", "D9"), (79.54, 9.4),
                                       (88.75, 9.4)]),
    ("SENSE_ADC12_TP", "B.Cu", W_SIG, [(88.75, 9.4), (88.75, 3.5)]),
    ("SENSE_ADC12_TP", "F.Cu", W_SIG, [(88.75, 3.5), _p("TP_ADC12", 1)]),
]
VIAS += [("SENSE_ADC11_TP", (88.0, 5.5)), ("SENSE_ADC11_TP", (86.5, 3.0)),
         ("SENSE_ADC12_TP", (88.75, 9.4)), ("SENSE_ADC12_TP", (88.75, 3.5))]


# --- audio: B2/B1 down the west edge to the jack -------------------------
# F.Cu the whole way, no via, and the whole run over the analog band.
#
# Rule 2 of the spec -- 10 mm from every SR_CLK and LED net -- is what shapes
# this, and it is measured against the two nearest pieces of the digital
# left column: R_LED8's LED_8_K pad, whose south edge is at y 33.59, and
# U_IN1 pin 2, the chain's clock terminus at (2.525, 36.35). An obvious
# westward exit at B2's own y (41.54) fails on the first of those at 7.95 mm,
# and a westward run at y 44.3 fails on the second at 9.82 mm. Both audio
# nets therefore turn west only at y 47 or below, which puts the whole run
# south of the module's outline: the closest either comes to a clock or LED
# node is 12.1 mm (the L channel's x 8.3 descent to U_IN1 pin 2), against the
# 10.87 mm placement.py itself measured from B2 to R_LED8.
#
# B2 is directly above B1 in the same pad column, so the L channel cannot
# simply drop: it steps south-west past B1's pad (0.687 mm clear of its
# corner) and takes the outer lane at x 8.3, west of the ring pad, while the
# R channel drops straight from B1 and turns west 1.8 mm further north. The
# two never cross -- L stays north of R until x 13.5, and west of it after.
TRACKS += [
    ("AUDIO_OUT_L", "F.Cu", W_SIG, [_p("U_SM", "B2"), (18.0, 44.0), (18.0, 47.0),
                                    (8.3, 47.0), (8.3, 60.5), _p("J_AUDIO", "T"),
                                    _p("TP_AUDIO_L", 1)]),
    ("AUDIO_OUT_R", "F.Cu", W_SIG, [_p("U_SM", "B1"), (20.73, 48.8), (13.5, 48.8),
                                    _p("J_AUDIO", "R")]),
]


# --- LED cathodes: diode to its series resistor --------------------------
# Eight 3 mm hops across the 4 mm-pitch column gap, F.Cu, no via. The only
# thing near them is each resistor's own GND stitching via 2 mm further along
# the column, which is a different net but 2.2 mm away at the closest.
TRACKS += [("LED_%d_K" % n, "F.Cu", W_SIG, [_p("D%d" % n, 1), _p("R_LED%d" % n, 1)])
           for n in range(1, 9)]


# --- LED anodes: the far side of a SOIC-16 -------------------------------
# THE ARITHMETIC THAT DECIDES THIS GROUP. Seven of the eight anodes land on a
# 74HC595 output that sits on the package's WEST pad column, while the LEDs
# are all to its EAST. There are three ways past a SOIC-16 and the board
# closes all three:
#
#  * Through the package. Pad pitch 1.27 mm, pad width 0.6 mm, so 0.67 mm of
#    laminate between neighbours; a 0.25 mm track with the board's 0.2 mm
#    clearance needs 0.65 mm. It fits by 0.01 mm on paper and by nothing at
#    all on a panel. Not attempted.
#  * Around the north or south end. Each package is stacked against its own
#    100 n decoupler -- 0.15 mm of courtyard gap -- and the one real window
#    that opens between their pads (0.948 mm, C_SR1's lower pad to U_SR1's
#    top pads) has Task 3's GND stitching via sitting in the middle of it,
#    leaving 0.111 mm on one side and 0.237 mm on the other.
#  * Around the west edge at x 1.0. That is one lane, genuinely clear, and
#    seven nets want it.
#
# So the anodes go under the board. B.Cu here is emptier than F.Cu by a wide
# margin -- it holds no pads at all in this column, only Task 3's two via
# columns at x 7.48 and x 8.475 -- and the crossing windows between those
# vias were enumerated before any lane was drawn: y 5.59..8.99,
# 10.24..12.80, 16.15..17.82, 20.23..23.63, 24.88..27.44. Every lane below
# sits inside one of them.
#
# Each net is F.Cu stub -> via -> B.Cu -> via -> F.Cu stub. The east vias sit
# at x 12.5, in the 1.555 mm alley between the diode column and the resistor
# column (0.48 mm and 0.475 mm clear); the west vias at x 5.0, in the
# package's own empty interior (1.2 mm clear of either pad column, 1.27 mm
# apart from each other). LED_3 is the exception and stays entirely on F.Cu:
# its output is QA on pin 15, the one chain output that already faces east.
LED_ANODE = [
    # net, SR pin, B.Cu polyline from the west via to the east via.
    # The west vias sit at x 5.3, not 5.0: the address bus needs four B.Cu
    # lanes down the packages' west side and 5.3 is what leaves room for the
    # outermost of them at x 4.6 while keeping 0.9 mm of copper to the
    # package's own east pad column.
    ("LED_1", ("U_SR1", 6), [(5.3, 12.15), (10.2, 12.15), (10.2, 2.975)]),
    ("LED_2", ("U_SR1", 7), [(5.3, 13.42), (5.9, 13.42), (5.9, 12.6),
                             (10.65, 12.6), (10.65, 6.975)]),
    # 4 and 5 turn north further east than their own nesting needs, to leave
    # the x 11.1 lane free for LED_3's B.Cu (see below); 4, 5 and 6 stay
    # nested among themselves, each turning east of the one before it.
    ("LED_4", ("U_SR2", 1), [(5.3, 20.44), (11.55, 20.44), (11.55, 14.975)]),
    ("LED_5", ("U_SR2", 2), [(5.3, 21.71), (12.0, 21.71), (12.0, 18.975)]),
    ("LED_6", ("U_SR2", 3), [(5.3, 22.98), (11.1, 22.98)]),
    # LED_7 and LED_8 are nested, never crossed: 7 keeps north of 8 at every
    # point. The obvious reading -- give each the shortest path -- crossed
    # them twice, which DRC named at (11.55, 25.52) and (5.0, 25.52). 8 turns
    # north-east first, at x 10.5, and 7 turns behind it at x 11.1.
    ("LED_7", ("U_SR2", 4), [(5.3, 24.25), (5.9, 24.25), (5.9, 24.95),
                             (11.1, 24.95), (11.1, 26.975)]),
    ("LED_8", ("U_SR2", 5), [(5.3, 25.52), (10.5, 25.52), (10.5, 30.975)]),
]
for _net, (_ref, _pin), _path in LED_ANODE:
    _n = int(_net.split("_")[1])
    _east = (12.5, _path[-1][1])
    TRACKS += [
        (_net, "F.Cu", W_SIG, [_p(_ref, _pin), _path[0]]),
        (_net, "B.Cu", W_SIG, _path + [_east]),
        (_net, "F.Cu", W_SIG, [_east, _p("D%d" % _n, 2)]),
    ]
    VIAS += [(_net, _path[0]), (_net, _east)]

# LED_3 is QA on pin 15 -- the one chain output already on the east column --
# and it still cannot use the east alley. SR1_TO_SR2 owns that alley from
# y 14.69 to 22.98, and pin 15 sits at y 21.71, i.e. BETWEEN the two pins
# that net joins: any eastward exit from pin 15 crosses it, which is what DRC
# reported at (7.475, 21.71). So LED_3 leaves west into the package's own
# interior instead and takes its via at (6.0, 19.6) -- the pocket north of
# U_SR2's pin 16 and south of C_SR2's lower pad, 0.436 mm and 0.558 mm clear
# -- then crosses the via wall at y 16.5, inside the 16.15..17.82 window.
TRACKS += [
    ("LED_3", "F.Cu", W_SIG, [_p("U_SR2", 15), (6.0, 21.71), (6.0, 19.6)]),
    ("LED_3", "B.Cu", W_SIG, [(6.0, 19.6), (6.0, 16.5), (11.1, 16.5),
                              (11.1, 10.975), (12.5, 10.975)]),
    ("LED_3", "F.Cu", W_SIG, [(12.5, 10.975), _p("D3", 2)]),
]
VIAS += [("LED_3", (6.0, 19.6)), ("LED_3", (12.5, 10.975))]


# --- the chain's own two short hops --------------------------------------
# The east alley, x 8.7..10.0, is the only F.Cu corridor between the package
# column and the diode column, and it is 1.3 mm wide with Task 3's x 8.475
# via column eating into it. Three lanes share it, at x 9.15, 9.7 and 8.95,
# each placed against the nearest via rather than against the alley's middle.
#
# SR1_TO_SR2 cannot run straight down the pin-9-to-pin-14 line: C_SR2 sits
# between the two packages with its pads on exactly that x, and its two
# stitching vias close what is left.
#
# SR_DATA_OUT comes from the module's B7, which is boxed in on three sides by
# its own pad column, so it leaves eastward into the module's empty interior,
# climbs to y 10 and crosses the diode column through the 0.8 mm window
# between the D2/D3 rows -- one of the seven such windows the 4 mm LED pitch
# leaves, and the reason this net needs no via at all.
TRACKS += [
    ("SR1_TO_SR2", "F.Cu", W_SIG, [_p("U_SR1", 9), (9.15, 14.69), (9.15, 22.98),
                                   _p("U_SR2", 14)]),
    # SR_DATA_OUT runs on B.Cu from end to end and spends exactly one via.
    # B7 is a through-hole module pad, so the B.Cu run starts in copper that
    # is already on every layer, and its far end lands in U_SR1's own empty
    # interior at (6.0, 8.9) -- 0.264 mm from pin 14's pad corner, 0.288 mm
    # from pin 13's -- where a 1.6 mm F.Cu stub finishes the job from the
    # inside. That is what frees the east alley for SR_CLK: an F.Cu
    # SR_DATA_OUT had to reach pin 14 from the alley, and SR_CLK then had no
    # way past it to pin 11 without crossing at (9.15, 8.34).
    ("SR_DATA_OUT", "B.Cu", W_SIG, [_p("U_SM", "B7"), (26.0, 36.46), (26.0, 1.3),
                                    (9.5, 1.3), (9.5, 1.75), (6.0, 1.75),
                                    (6.0, 7.7)]),
    ("SR_DATA_OUT", "F.Cu", W_SIG, [(6.0, 7.7), _p("U_SR1", 14)]),
]
VIAS += [("SR_DATA_OUT", (6.0, 7.7))]
# The 0.45 mm step at x 9.5 is not decoration: Task 3 left a GND via at
# (7.48, 0.887) and a second at (14.0, 2.0), and the only lane that clears
# both is one that runs at y 1.3 east of x 9.5 and at y 1.75 west of it.


# --- SR_CLK: one run, TP_CLK -> B8 -> 595 -> 595 -> 165 ------------------
# The clock is the net the spec is strictest about, and it is drawn here as a
# single polyline with no branch anywhere: each pad is entered from one side
# and left from the other, so every node on it has degree 2 and the two ends
# are TP_CLK and U_IN1 pin 2.
#
# B8 is the awkward node. Its pad is boxed in by B7, B9 and B3 at 0.66 mm, so
# both of its segments have to leave eastward -- but they leave at different
# heights inside the 1.88 mm pad (y 39.6 in, y 38.4 out), which keeps them
# from becoming one doubled track. The clock's east arm runs at x 68, in the
# module's empty interior, 2.855 mm clear of bank D.
#
# The three package pins are all pin 11 on the east column, and the run drops
# past them down one lane at x 9.7 -- entering each pad westward and leaving
# it on a short diagonal back to the same lane 0.85 mm further south, which
# is what keeps the lane from doubling back over itself. The last leg is the
# only one that cannot stay on F.Cu: U_IN1's CP is pin 2 on the WEST column,
# and the window between C_IN1's lower pad and U_IN1's top pads is 0.948 mm
# with a stitching via in the middle of it -- 0.111 mm and 0.237 mm left. So
# the clock dives at (9.7, 35.5), runs 4.7 mm of B.Cu under the package and
# comes up inside it at (5.0, 36.35).
TRACKS += [
    ("SR_CLK", "F.Cu", W_SIG, [_p("TP_CLK", 1), (96.0, 4.6), (68.0, 4.6),
                               (68.0, 39.6), _p("U_SM", "B8")]),
    ("SR_CLK", "F.Cu", W_SIG, [_p("U_SM", "B8"), (27.0, 38.4), (27.0, 5.4),
                               (19.0, 5.4), (19.0, 6.0), (9.7, 6.0),
                               (9.7, 12.15), _p("U_SR1", 11)]),
    ("SR_CLK", "F.Cu", W_SIG, [_p("U_SR1", 11), (9.7, 13.0), (9.7, 26.79),
                               _p("U_SR2", 11)]),
    ("SR_CLK", "F.Cu", W_SIG, [_p("U_SR2", 11), (9.7, 27.6), (9.7, 35.5)]),
    ("SR_CLK", "B.Cu", W_SIG, [(9.7, 35.5), (5.3, 35.5), (5.3, 36.35)]),
    ("SR_CLK", "F.Cu", W_SIG, [(5.3, 36.35), _p("U_IN1", 2)]),
]
VIAS += [("SR_CLK", (9.7, 35.5)), ("SR_CLK", (5.3, 36.35))]


# --- SR_DATA_IN and BTN_1: the two long hauls to U_IN1 -------------------
# Both start on through-hole copper -- the module's D10 pad and the tactile
# switch's own legs -- so both run on B.Cu from the first millimetre and
# spend one via each, at the U_IN1 end. F.Cu is not available to either: the
# whole width of the digital strip between them and U_IN1 is already spoken
# for by the +-12 V corridor, SR_CLK's x 68 arm and the diode/resistor
# columns, and both nets would have to cross all three.
#
# They share the board's southern digital edge at y 45.5 and 46.0, in the
# 0.855 mm of clear laminate the module's B1/B10 pads leave above the moat,
# and they stay 0.5 mm apart the whole way. BTN_1 turns south at x 27 rather
# than x 26 for one reason: SR_DATA_OUT's B.Cu arm owns x 26 from y 1.3 to
# 36.46, and passing east of it is the only way across without a crossing.
TRACKS += [
    ("SR_DATA_IN", "B.Cu", W_SIG, [_p("U_SM", "D10"), (82.08, 6.5), (68.0, 6.5),
                                   (68.0, 46.0), (9.0, 46.0), (9.0, 45.0)]),
    ("SR_DATA_IN", "F.Cu", W_SIG, [(9.0, 45.0), _p("U_IN1", 9)]),
    ("BTN_1", "F.Cu", W_SIG, [_p("SW1", 1), _p("R_BTN", 2)]),
    ("BTN_1", "B.Cu", W_SIG, [_p("SW1", 1), (84.5, 8.0), (84.5, 5.5), (27.0, 5.5),
                              (27.0, 45.5), (10.2, 45.5), (10.2, 42.0)]),
    ("BTN_1", "F.Cu", W_SIG, [(10.2, 42.0), _p("U_IN1", 11)]),
]
VIAS += [("SR_DATA_IN", (9.0, 45.0)), ("BTN_1", (10.2, 42.0))]


# --- U_SR1's interior via column -----------------------------------------
# Six of the 595's outputs have to reach the analog end of the board and
# five of them are on the package's WEST pad column, the far side again. The
# escape is the interior: one via per pin at x 5.3, on the pins' own 1.27 mm
# pitch, each fed by a 2.8 mm F.Cu stub from its pad. x 5.3 is not arbitrary
# -- it is the westmost column that still leaves 0.9 mm to the east pad row
# for SR_DATA_OUT's and SR_LATCH's vias at x 6.0, and the eastmost that
# leaves five 0.45 mm B.Cu lanes between it and the board's west edge.
#
# MUX_A0 is the exception: QA is pin 15, on the east column, so it walks west
# INTO the interior at x 6.0 (0.375 mm clear of pin 16's pad) and drops into
# the column one slot above MUX_A1.
ADDR_VIA = {
    "MUX_A0": (5.3, 4.53), "MUX_A1": (5.3, 5.8), "MUX_A2": (5.3, 7.07),
    "MUX_A3": (5.3, 8.34), "MUX16_EN_N": (5.3, 9.61),
}
TRACKS += [
    ("MUX_A0", "F.Cu", W_SIG, [_p("U_SR1", 15), (6.0, 7.07), (6.0, 4.53),
                               ADDR_VIA["MUX_A0"]]),
    ("MUX_A1", "F.Cu", W_SIG, [_p("U_SR1", 1), ADDR_VIA["MUX_A1"]]),
    ("MUX_A2", "F.Cu", W_SIG, [_p("U_SR1", 2), ADDR_VIA["MUX_A2"]]),
    ("MUX_A3", "F.Cu", W_SIG, [_p("U_SR1", 3), ADDR_VIA["MUX_A3"]]),
    ("MUX16_EN_N", "F.Cu", W_SIG, [_p("U_SR1", 4), ADDR_VIA["MUX16_EN_N"]]),
]
VIAS += [(net, xy) for net, xy in sorted(ADDR_VIA.items())]

# The five interior vias feed five B.Cu lanes down the board's west edge, and
# the lanes are assigned by the only rule that avoids crossings in a fan-out
# like this: the northernmost pin takes the westernmost lane, and the
# westernmost lane turns east furthest SOUTH. x 0.9 and 1.8 are the two lanes
# outside Task 3's x 2.525 stitching column; 1.8 has to step out to 2.6 for
# y 36..43, because U_IN1's four GND vias stand at x 1.525 there and leave
# 0.275 mm on the inside. The southern turn happens below y 46.45, which is
# where BTN_1's and SR_DATA_IN's own B.Cu arms have already finished: south
# of that line nothing crosses the board until the pots at y 49.6.
ADDR_LANE = {                    # net: (lane x, east-turn y)
    "MUX_A0": (0.9, 62.8), "MUX_A1": (1.8, 48.95), "MUX_A2": (3.15, 48.5),
    "MUX_A3": (3.6, 47.5), "MUX16_EN_N": (4.05, 46.5),
}
for _net, (_lx, _ly) in sorted(ADDR_LANE.items()):
    _vx, _vy = ADDR_VIA[_net]
    _path = [(_vx, _vy), (_lx, _vy)]
    if _lx == 1.8:               # the U_IN1 via column detour
        _path += [(1.8, 36.0), (2.6, 36.0), (2.6, 43.0), (1.8, 43.0)]
    _path += [(_lx, _ly)]
    TRACKS.append((_net, "B.Cu", W_SIG, _path))

# MUX8_EN_N is the sixth output and there is no sixth B.Cu lane, so it takes
# the one F.Cu route out of this corner instead: west to x 1.0, 0.425 mm
# clear of the package pad column, then south past U_SR2, then east above
# U_IN1's pads at y 33.5 and down the packages' own interior at x 6.0. It
# needs a single via at the very end, where the analog band begins.
TRACKS += [
    ("MUX8_EN_N", "F.Cu", W_SIG, [_p("U_SR1", 5), (1.0, 10.88), (1.0, 33.5),
                                  (6.0, 33.5), (6.0, 46.6)]),
    # East along the board's southern digital edge, then back to F.Cu at
    # x 70.5 for the descent into the analog band -- F.Cu because SR_LATCH's
    # own B.Cu arm lies across y 47.4 for the full width, and the 8:1's
    # enable pin has to get under it.
    ("MUX8_EN_N", "B.Cu", W_SIG, [(6.0, 46.6), (70.5, 46.6)]),
    ("MUX8_EN_N", "F.Cu", W_SIG, [(70.5, 46.6), (70.5, 63.905), _p("U_MUX8", 6)]),
]
VIAS += [("MUX8_EN_N", (6.0, 46.6)), ("MUX8_EN_N", (70.5, 46.6))]


# --- SR_LATCH: the module's D1 to both 595s and the 165 -----------------
# Three of its four nodes are on a package's far pad column, so it is a B.Cu
# spine with three short taps, running down the innermost west lane at x 4.5.
# The taps sit at x 6.0 -- the eastmost a via can stand in a SOIC interior
# and still keep 0.2 mm to the pad row -- and at x 5.3 for U_IN1, one slot
# north of SR_CLK's own via in the same interior.
# The east arm climbs at x 84.0, between the module's bank A (edge x 83.02)
# and -12 V's B.Cu hop into J_PWR pin 1 at x 85: 0.855 mm and 1.0 mm.
TRACKS += [
    ("SR_LATCH", "F.Cu", W_SIG, [(6.0, 11.52), _p("U_SR1", 12)]),
    ("SR_LATCH", "F.Cu", W_SIG, [(6.0, 26.4), _p("U_SR2", 12)]),
    ("SR_LATCH", "F.Cu", W_SIG, [(5.3, 34.4), _p("U_IN1", 1)]),
    ("SR_LATCH", "B.Cu", W_SIG, [(6.0, 11.52), (4.5, 11.52), (4.5, 26.4),
                                 (6.0, 26.4)]),
    ("SR_LATCH", "B.Cu", W_SIG, [(4.5, 26.4), (4.5, 34.4), (5.3, 34.4)]),
    ("SR_LATCH", "B.Cu", W_SIG, [(4.5, 34.4), (4.5, 47.4), (84.0, 47.4),
                                 (84.0, 16.0), (82.08, 16.0), _p("U_SM", "D1")]),
]
VIAS += [("SR_LATCH", (6.0, 11.52)), ("SR_LATCH", (6.0, 26.4)),
         ("SR_LATCH", (5.3, 34.4))]

# THE MODULE'S PAD FIELD IS SOLID. Measured, not assumed: a landing-pattern
# pad is 1.88 mm square on a 2.54 mm pitch, so 0.66 mm of laminate separates
# two neighbours and a 0.25 mm track with 0.2 mm clearance needs 0.65 mm.
# Nothing threads bank B, C or D -- not a signal track, not anywhere. Every
# net that leaves a module pad in this file therefore leaves it on the one
# side that opens into free board, and every net that has to get past a bank
# crosses in one of the two clear bands the banks leave: y 19.35..32.66
# between the C and B banks on the west columns, and y < 6.66 above bank C.
# SR_DATA_OUT's first draft crossed at y 10.0 and shorted straight onto pad
# C4; it now climbs to y 6.3 inside the module's empty interior, crosses
# above the bank, and steps back down to y 10 in the 4 mm of clear board
# between the module's outline and the diode column.


# --- the analog fan-out --------------------------------------------------
# Both muxes are SOIC packages with every channel on a pad column, and the
# same 0.67 mm-between-pads arithmetic that closed the 595s closes them: a
# channel cannot be reached from the outside except along its own row, and
# 24 channels want 24 rows. So each mux pin gets a via in its package's own
# empty interior -- x 86.0 and 90.0 inside the 16:1, x 76.0 and 77.4 inside
# the 8:1, each column on the pins' own 1.27 mm pitch -- and the fan-out
# happens on B.Cu underneath, where the analog band holds nothing but the
# pots' through-holes, the jack, and Task 3's stitching vias.
#
# The pots need no via at all: their pins are through-hole, so a B.Cu run
# ends in the wiper pad directly. Only the 0805 ties and dividers need one,
# and it goes 1.3..2.0 mm off the pad on the side away from that part's own
# stitching via.
#
# The clear bands this fan-out uses were measured off the placement, not
# guessed: y 59.95..64.1 between the upper pots' mounting lugs and the lower
# pots' pins (the four upper wipers ride it), y 66.5..70.5 between the lower
# pots' pins and their lugs (the three lower wipers), y 72.7..75.3 between
# the two tie rows, and the lug gaps at x 20.06..26.94, 33.56..40.44,
# 47.06..53.94 and 60.56..67.44 that each wiper climbs through.

# x 85.1, not 86.0: the 16:1's east column needs six descent lanes between
# this column and the y-72 stitching vias at x 89, and 86.0 left only four.
# Moving the column (and the wipers' step-downs with it) 0.9 mm west buys the
# two lanes that let CH11 and CH12 reach the northern spare row without
# crossing the southern one.
MUX16_L, MUX16_R = 85.1, 90.0    # via columns inside the 16:1's interior
MUX8_L, MUX8_R = 76.0, 77.4      # and inside the 8:1's

# --- 16:1 west column: four upper pot wipers ------------------------------
# Each leaves its via westward at the pin's own y, steps to its own lane in
# the y 60.3..61.65 band, runs west, and climbs to the wiper through the lug
# gap beside its pot. The step-down x values (84.0..85.35) are ordered so
# that no two of the four cross: a wiper whose lane is further south turns
# further west.
for _net, _py, _tx, _lane, _pot in (
        ("MUX16_CH6", 57.555, 84.0, 60.3, "RV4"),
        ("MUX16_CH4", 60.095, 84.45, 60.75, "RV3"),
        ("MUX16_CH2", 62.635, 83.55, 61.2, "RV2"),
        ("MUX16_CH0", 65.175, 83.1, 61.65, "RV1")):
    _wx = {"RV1": 23.5, "RV2": 37.0, "RV3": 50.5, "RV4": 64.0}[_pot]
    TRACKS += [
        (_net, "F.Cu", W_SIG, [(MUX16_L, _py), _p("U_MUX16", {
            "MUX16_CH6": 3, "MUX16_CH4": 5, "MUX16_CH2": 7, "MUX16_CH0": 9}[_net])]),
        (_net, "B.Cu", W_SIG, [(MUX16_L, _py), (_tx, _py), (_tx, _lane),
                               (_wx, _lane), _p(_pot, 2)]),
    ]
    VIAS.append((_net, (MUX16_L, _py)))

# --- 16:1 west column: the four neighbour ties ----------------------------
# CH1/CH3/CH5/CH7 sit on the west pads but their 0 R links are south-east of
# the package, so each leaves its via eastward at the pin's own y and drops.
#
# The drop order is forced and the first draft got it backwards, which DRC
# priced at three crossings. Four tracks leaving one via column eastward and
# then turning south cross whenever a NORTHERN pin turns west of a southern
# one -- the southern net's own eastward leg then runs straight through the
# northern net's descent. So the northern the pin, the further east it turns:
# CH1 at x 87.0, CH3 at 87.45, then CH5 and CH7 all the way out to x 95.0
# and 95.45, in the corridor between the package's east pads (edge x 93.675)
# and the bulk-cap column (edge 95.775). Those two go that far because their
# own southern legs then have to pass under everything: CH5 crosses back at
# y 74 between the tie rows, CH7 at y 79 along the board's bottom edge.
# CH1 joins them on F.Cu as the fourth and easternmost lane of the strip. It
# started as a B.Cu drop through the package interior and DRC rejected that
# twice -- the drop sat inside the corridor CH8/CH9/CH15 need, and no slot
# between the two via columns clears both their descents and the tie row's
# stitching vias. On F.Cu it costs nothing and the whole interior stays free.
# CH1 and CH3 do not use that strip at all: they walk EAST into the package's
# own interior instead, which is 7.25 mm of empty F.Cu between the two pad
# columns, and drop through the 0.775 mm gap the tie rows leave between
# R_HI1 and R_LO1 (x 87.6..89.4, minus the stitching via at x 89). That keeps
# two of the four strip lanes free -- and the 8:1's own channels need them.
TRACKS += [
    ("MUX16_CH1", "F.Cu", W_SIG, [_p("U_MUX16", 8), (87.5, 63.905),
                                  (87.5, 71.4), _p("R_HI1", 2)]),
    ("MUX16_CH3", "F.Cu", W_SIG, [_p("U_MUX16", 6), (88.0, 61.365),
                                  (88.0, 73.9), (92.0, 73.9), _p("R_LO1", 2)]),
]


# --- 16:1 east column: the reference dividers and the spare ties ---------
# Eight more channels, all of them wanting parts 20..30 mm to the WEST, and
# the package's own east pads facing the wrong way. They leave on vias in the
# x 90.0 interior column and drop into one of two bands -- x 86.7..88.05
# between the two via columns, or x 90.7..91.6 east of them -- and then run
# west underneath everything on B.Cu.
#
# The band assignment is what makes the whole group crossing-free, and it
# follows from two rules that pull in opposite directions until the pins are
# split the right way. Within a band that leaves WESTWARD, the northern pin
# must turn further west, and the westernmost turn then owns the northernmost
# lane -- so northern pin, northern lane. Within a band leaving EASTWARD it is
# the reverse. CH8 and CH9 have the northernmost pins AND the northernmost
# targets (the two reference dividers at y 65.5 and 69.5), so they belong in
# the westward band; CH10..CH12's targets sit in the spare row at y 73.5,
# which the eastward band's southern lanes reach directly.
for _net, _py, _pin in (("MUX16_CH8", 56.285, 23), ("MUX16_CH9", 57.555, 22)):
    TRACKS.append((_net, "F.Cu", W_SIG, [(MUX16_R, _py), _p("U_MUX16", _pin)]))
    VIAS.append((_net, (MUX16_R, _py)))

# CH8 -> the 10k/10k divider. The two resistors are 3 mm apart on the same
# row, so they are linked on F.Cu at their own y and the net comes up on a
# via in the middle of that link's approach.
TRACKS += [
    ("MUX16_CH8", "B.Cu", W_SIG, [(MUX16_R, 56.285), (85.725, 56.285),
                                  (85.725, 66.75), (62.5, 66.75), (62.5, 67.2)]),
    ("MUX16_CH8", "F.Cu", W_SIG, [(62.5, 67.2), (62.5, 65.5), _p("R_REFA1", 2)]),
    ("MUX16_CH8", "F.Cu", W_SIG, [(62.5, 65.5), _p("R_REFA2", 1)]),
]
VIAS.append(("MUX16_CH8", (62.5, 67.2)))

# CH9 -> the 1k/1k divider, whose two halves placement put 12.6 mm apart. The
# spine runs west at y 70.9 and taps both: R_REFB1 through a via at x 72.9 and
# an F.Cu hop that crosses CH8's lane on the other layer, R_REFB2 at the west
# end. Two vias, and no crossing with anything in the band above.
TRACKS += [
    ("MUX16_CH9", "B.Cu", W_SIG, [(MUX16_R, 57.555), (86.175, 57.555),
                                  (86.175, 70.9), (72.68, 70.9), (57.7, 70.9)]),
    ("MUX16_CH9", "F.Cu", W_SIG, [(72.68, 70.9), (72.68, 66.5), _p("R_REFB1", 2)]),
    ("MUX16_CH9", "F.Cu", W_SIG, [(57.7, 70.9), _p("R_REFB2", 1)]),
]
VIAS += [("MUX16_CH9", (72.68, 70.9)), ("MUX16_CH9", (57.7, 70.9))]

# The six spare-channel ties. CH10/CH11 join the westward band -- northern
# pins, northern lanes, and their 0 Rs are in the northern spare row at
# y 73.5, so the order carries straight through. CH12..CH15 leave eastward
# instead: that band's rule inverts (northern pin, southern lane), which is
# exactly what the southern spare row at y 77.5 wants. CH12 is the one net
# whose pin and target sit on opposite sides of that split, so it takes the
# southernmost lane of all, comes up at x 76 clear of the whole tie block,
# and walks back to its 0 R on F.Cu.
#
# The lanes are pinned between two rows of Task 3 stitching vias: y 76.625 is
# the floor set by the AGND/+3V3 vias at y 76, and y 78.125 the next clear
# line under the spare row's own vias at y 77.5. Nothing sits between them
# except CH15's lane at 76.8, which stops at x 71 and so never meets them.
for _net, _py, _pin, _bx, _lane, _tv, _tref, _tail in (
        ("MUX16_CH10", 58.825, 21, 86.625, 72.8, (61.0, 72.8), "R_SP10", []),
        ("MUX16_CH11", 60.095, 20, 87.075, 74.4, (66.0, 74.9), "R_SP11", []),
        ("MUX16_CH12", 61.365, 19, 87.525, 75.1, (71.0, 75.1), "R_SP12", []),
        ("MUX16_CH15", 65.175, 16, 87.975, 77.5, (71.0, 77.5), "R_SP15", []),
        ("MUX16_CH14", 63.905, 17, 90.7, 78.15, (66.0, 78.15), "R_SP14", []),
        ("MUX16_CH13", 62.635, 18, 91.15, 78.8, (61.0, 78.8), "R_SP13", [])):
    TRACKS += [
        (_net, "F.Cu", W_SIG, [(MUX16_R, _py), _p("U_MUX16", _pin)]),
        (_net, "B.Cu", W_SIG, [(MUX16_R, _py), (_bx, _py), (_bx, _lane),
                               (_tv[0], _lane), _tv]),
        (_net, "F.Cu", W_SIG, [_tv] + _tail + [_p(_tref, 2)]),
    ]
    VIAS += [(_net, (MUX16_R, _py)), (_net, _tv)]

# CH3, CH5 and CH7 take no via at all. Their pins are on the package's WEST
# column, which faces open F.Cu -- nothing stands between them and the empty
# strip at x 80.5..81.5 -- and the whole board south of the muxes is F.Cu
# laminate with only the tie pads on it. So each walks west out of its pin,
# down its own lane in that strip, east along its own lane between or below
# the tie rows, and into its 0 R from the side. Three nets, six corners, no
# layer change: cheaper than anything the B.Cu fan-out could have done, and
# it is what leaves the east half of the interior free for CH8..CH15.
#
# Their order in that strip is not free, and the first draft paid four
# crossings to find out. Three tracks leaving one pad column westward and
# then turning south cross whenever a northern pin turns EAST of a southern
# one; and once south, a net's eastward leg crosses every descent still
# standing west of it. Both rules together fix the assignment exactly:
# northern pin descends further west (CH7 80.5, CH5 81.0, CH3 81.5), and the
# easternmost descent takes the northernmost eastward lane (CH3 at y 73.9,
# CH5 at 77.35, CH7 at 78.2).
TRACKS += [
    ("MUX16_CH5", "F.Cu", W_SIG, [_p("U_MUX16", 4), (81.75, 58.825), (81.75, 74.35),
                                  (98.0, 74.35), (98.0, 72.0), _p("R_HI2", 2)]),
    ("MUX16_CH7", "F.Cu", W_SIG, [_p("U_MUX16", 2), (81.3, 56.285), (81.3, 74.8),
                                  (87.0, 74.8), _p("R_LO2", 2)]),
]


# --- the 8:1 -------------------------------------------------------------
# Same idea, smaller package: a via column at x 76.0 for the west pins and
# x 77.4 for the east ones, both inside the SOIC's own 3 mm interior. Three
# of the eight vias sit slightly off their pin's y (59.3, 60.7, 63.2) so the
# columns interleave without ever coming within 0.8 mm of each other -- the
# 1.27 mm pin pitch is too fine to carry two 0.6 mm via columns on the same
# lines.
#
# The four channels whose parts lie west (the three lower pots and the third
# reference divider) drop into the 4.1 mm-tall corridor between the lower
# pots' pins and their mounting lugs, at y 66.3..70.3. The two whose ties sit
# in the far-east column (R_HI4 and R_LO4 at x 96.5) cannot go east at all --
# the 16:1's own west-band descents stand across every lane between y 56 and
# y 77 -- so they climb NORTH instead, over the COM cluster at y 50.6 and
# 51.05, and come down the board's east edge.
MUX8_R = 77.4                    # the only via column inside the 8:1

# The four west-going channels start on F.Cu, and they have to: the 16:1's
# four wiper lanes lie across y 60.3..61.65 from x 23.5 all the way to 84.45,
# and every one of these nets has to get from a pin north of that band to a
# lane south of it. Nothing routes around the band -- its west end is at the
# first pot and its east end runs into the 16:1's own via column -- so each
# crosses it on the other layer and takes its via only once it is south.
# Two of the crossings run down the 8:1's interior (x 76.3 and 76.75) and two
# down the strip between the two packages (x 80.4 and 81.75).
TRACKS += [
    ("MUX8_CH4", "F.Cu", W_SIG, [_p("U_MUX8", 1), (75.85, 57.555), (75.85, 68.85), (75.2, 68.85)]),
    ("MUX8_CH4", "B.Cu", W_SIG, [(75.2, 68.85), (50.5, 68.85), _p("RV7", 2)]),
    ("MUX8_CH6", "F.Cu", W_SIG, [_p("U_MUX8", 2), (75.35, 58.825), (75.35, 68.1), (74.9, 68.1)]),
    ("MUX8_CH6", "B.Cu", W_SIG, [(74.9, 68.1), (67.5, 68.1)]),
    ("MUX8_CH6", "F.Cu", W_SIG, [(67.5, 68.1), (67.5, 69.5), _p("R_REFC1", 2)]),
    ("MUX8_CH6", "F.Cu", W_SIG, [(67.5, 69.5), _p("R_REFC2", 1)]),
    ("MUX8_CH2", "F.Cu", W_SIG, [_p("U_MUX8", 15), (76.35, 58.825), (76.35, 76.7)]),
    ("MUX8_CH2", "B.Cu", W_SIG, [(76.35, 76.7), (37.0, 76.7)]),
    ("MUX8_CH2", "F.Cu", W_SIG, [(37.0, 76.7), _p("RV6", 2)]),
    ("MUX8_CH0", "F.Cu", W_SIG, [_p("U_MUX8", 13), (80.4, 61.365), (80.4, 75.9),
                                 (79.6, 75.9)]),
    ("MUX8_CH0", "B.Cu", W_SIG, [(79.6, 75.9), (23.5, 75.9), _p("RV5", 2)]),
]
VIAS += [("MUX8_CH4", (75.2, 68.85)), ("MUX8_CH6", (74.9, 68.1)),
         ("MUX8_CH6", (67.5, 68.1)), ("MUX8_CH2", (76.35, 76.7)),
         ("MUX8_CH2", (37.0, 76.7)), ("MUX8_CH0", (79.6, 75.9))]

# The four neighbour ties, now that placement put them against the package.
# All four are direct F.Cu, no via, no layer change: CH7 is a 2.5 mm straight
# line from pin 4 to the tie west of the package, and the other three drop
# out of the package -- CH5 and CH3 through its own empty interior, CH1 down
# the x 80.5 lane east of it -- into the row 1.25 mm below its south edge.
# Task 4's first pass spent four vias and four 20 mm detours on these,
# because the ties were on the far side of the 16:1.
TRACKS += [
    # CH3 drops straight down the package's own interior into its 0 R; CH1
    # steps 2 mm east of the package first, into the 2.08 mm strip the two
    # SOICs leave between them, and drops there. Neither needs a via.
    ("MUX8_CH3", "F.Cu", W_SIG, [_p("U_MUX8", 12), (77.4, 62.635),
                                 _p("R_LO3", 2)]),
    ("MUX8_CH1", "F.Cu", W_SIG, [_p("U_MUX8", 14), (80.85, 60.095), (80.85, 78.6),
                                 (92.0, 78.6), _p("R_HI3", 2)]),
    # R_HI4 and R_LO4 are still in the board's east column, and the 16:1's
    # east-band descents stand between them and the 8:1 from y 56 to y 77.
    # These two go NORTH instead, over the COM cluster at y 50.6 and 51.05 --
    # the only two lanes that thread JP_GND's and C_COM16's stitching vias --
    # and come back down the board's east edge.
    ("MUX8_CH7", "F.Cu", W_SIG, [_p("U_MUX8", 4), (72.0, 61.365), (72.0, 51.7)]),
    ("MUX8_CH7", "B.Cu", W_SIG, [(72.0, 51.7), (72.0, 51.05), (94.2, 51.05),
                                 (94.2, 65.0)]),
    ("MUX8_CH7", "F.Cu", W_SIG, [(94.2, 65.0), _p("R_LO4", 2)]),
    ("MUX8_CH5", "F.Cu", W_SIG, [_p("U_MUX8", 5), (71.2, 62.635), (71.2, 50.6)]),
    ("MUX8_CH5", "B.Cu", W_SIG, [(71.2, 50.6), (95.0, 50.6), (95.0, 61.4)]),
    ("MUX8_CH5", "F.Cu", W_SIG, [(95.0, 61.4), _p("R_HI4", 2)]),
]
VIAS += [("MUX8_CH7", (72.0, 51.7)), ("MUX8_CH7", (94.2, 65.0)),
         ("MUX8_CH5", (71.2, 50.6)), ("MUX8_CH5", (95.0, 61.4))]


def pad_xy(board, ref, number):
    """Absolute board coordinates of one pad, in mm."""
    import pcbnew
    for fp in board.GetFootprints():
        if fp.GetReference() != ref:
            continue
        for pad in fp.Pads():
            if pad.GetNumber() == str(number):
                p = pad.GetPosition()
                return pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)
    raise KeyError("%s.%s" % (ref, number))


def resolve(board, points):
    return [pad_xy(board, p[1], p[2]) if p[0] == "pad" else (p[0], p[1])
            for p in points]


def apply(board, kipcb):
    """Draw every track and via. Returns (n_tracks, n_segments, n_vias)."""
    segments = 0
    for net, layer, width, points in TRACKS:
        xy = resolve(board, points)
        kipcb.add_track(board, layer, width, net, xy)
        segments += len(xy) - 1
    for net, at in VIAS:
        kipcb.add_via(board, at, net)
    return len(TRACKS), segments, len(VIAS)
