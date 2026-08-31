#!/usr/bin/env python3
"""Every coupon part's position, as data. Millimetres, y grows downward,
origin top-left. Authored against the zone plan of the 2026-08-31 layout
spec; the checks in build_pcb.py and check_layout.py are the arbiter.

SM bank orientation per the Task-1 probe, whose printed pad coordinates
(footprint frame, mm) decide everything below:

    A1 (-32.08,-15.27)   A10 (-32.08,-12.73)   -- bank A, upper LEFT
    D1 (-32.08, 12.73)   D10 (-32.08, 15.27)   -- bank D, lower LEFT
    B1 ( 29.27,-18.08)                         -- bank B, upper RIGHT
    C1 ( 29.27,  7.92)                         -- bank C, lower RIGHT

So banks A and D share one physical edge of the module and B and C the
other -- NOT A/B against C/D as the pin letters suggest. Bank A carries the
supply pins (A1 -12V, A4/A7 GND, A5 +12V, A10 +3V3 OUT) *and* the two mux
sense pins A2/A3; bank B carries the audio outs B1/B2 *and* the shift-chain
data/clock B7/B8. The two things the layout most wants next to each other --
muxes at their sense pins, jack at the audio pins -- therefore sit on
opposite edges of the module no matter what. That is measured, not assumed.

**The SM is rotated 180 deg.** At 0 deg banks A and B both face the module's
top edge; at 180 deg they both face its bottom edge, which is where the
analog region is. The sense pins (A2/A3) and the audio pins (B1/B2) then look
straight down into that region, and the unused bank C plus the chain's
latch/data-in (D1/D10) take the top. 90/270 deg are not available at all: the
pad field is 61.35 x 36.16 mm, so a quarter turn needs 61.35 mm of board
height and there are 80 -- it would fit, but it would put the 61 mm dimension
across the short axis and leave nothing usable on either side.

**The consequence is a horizontal split, not the spec's left/right one.** Both
analog anchors are on the module's bottom edge, so analog is the bottom band
and digital is everything above it. What the spec asked for survives in
substance: the jack is on the left board edge under B1/B2, the muxes are under
A2/A3, and the Eurorack header is beside the SM's own supply pins.

Placed at (50, 26) the module's own outline -- a 68 x 40 mm silkscreen
rectangle, read out of the footprint -- covers x 16..84, y 6..46. On the
100 x 80 board (grown from 80 x 60 by controller ruling; see design.py for the
arithmetic) that leaves four clear regions and **nothing tall, connected or
probeable sits in the module's shadow**:

  * bottom band, y 48.7..79, full width: ANALOG. Jack at the left edge, seven
    pots in two rows, both muxes and their COM clusters at the right end
    under A2/A3, ties and references filling in around them.
  * left column, x 0..16: DIGITAL. The three shift-register packages in a
    stack with their decouplers, and the eight LEDs with their resistors in
    two narrow columns beside them.
  * right column, x 84..100: DIGITAL. Eurorack header beside the SM's supply
    pins, the bulk caps, the button, and the probe points that must stay
    reachable with the module plugged in.
  * the module's interior stays empty. It may legally hold surface-mount
    passives; on this board it does not have to.

**Rule 2 (audio 10 mm from SR_CLK and the LED nets) is measured outside the
SM footprint by controller ruling** -- the 3.59 mm between the module's own B2
and B8 pads is Electrosmith's spacing, not ours, and no placement can change
it. Outside the footprint the layout is built for that ruling: the audio jack
and its probe point own the board's bottom-left corner, the LED rows stop at
y 32 in the left column, and the two families keep 16.87 mm between them
(J_AUDIO.R to U_IN1's clock pin). Counting the SM's own audio pad against a
board part -- the strictest reading the ruling still allows -- it is 10.87 mm,
B2 to R_LED8.
"""

# --- the module -------------------------------------------------------------
SM = (50.0, 26.0, 180)        # body x 16..84, y 6..46; banks per docstring

PLACE = {
    "U_SM": SM,

    # --- digital: the left column (x 0..16) ---------------------------------
    # The chain and its decouplers, alternating down the column so every 100n
    # sits off its package's SHORT end. That is forced, not tidy: on SOIC-16
    # the VCC pad stops 0.80 mm short of the courtyard's end but 1.27 mm short
    # of its side, so a cap parked alongside can never come closer than
    # 2.24 mm however hard it is pushed, while the end placement lands at
    # 1.84 mm. 1.68 mm is the floor for this footprint pair at zero courtyard
    # clearance (5.245 + 1.925 - 1.038 - 4.45), so 1.84 is as much margin as
    # the parts allow -- the 1.5 mm the ruling asks for is not reachable with
    # an 0805 hand-solder pad against a SOIC courtyard.
    "C_SR1": (7.48, 2.925, 90),
    "U_SR1": (5.0, 10.245, 0),
    "C_SR2": (7.48, 17.565, 90),
    "U_SR2": (5.0, 24.885, 0),
    "C_IN1": (7.48, 32.205, 90),
    "U_IN1": (5.0, 39.525, 0),

    # The eight LEDs and their series resistors, two columns beside the chain.
    # They stop at y 32 on purpose: rule 2 measures from here to the audio net,
    # and the SM's B2 pad is at (20.73, 41.54). Every 4 mm further down cost
    # about 1 mm of that clearance.
    "D1": (11.0, 4.0, 90),
    "D2": (11.0, 8.0, 90),
    "D3": (11.0, 12.0, 90),
    "D4": (11.0, 16.0, 90),
    "D5": (11.0, 20.0, 90),
    "D6": (11.0, 24.0, 90),
    "D7": (11.0, 28.0, 90),
    "D8": (11.0, 32.0, 90),
    "R_LED1": (14.0, 4.0, 90),
    "R_LED2": (14.0, 8.0, 90),
    "R_LED3": (14.0, 12.0, 90),
    "R_LED4": (14.0, 16.0, 90),
    "R_LED5": (14.0, 20.0, 90),
    "R_LED6": (14.0, 24.0, 90),
    "R_LED7": (14.0, 28.0, 90),
    "R_LED8": (14.0, 32.0, 90),

    # --- digital: the right column (x 84..100) ------------------------------
    # The IDC header sits beside the SM's own supply pins (A5 at x 71.9, A1
    # and A10 at x 82.1, y 38.7..43.3), with both 10u bulk caps alongside it.
    # The two ADC probe points are under D8/D9 at the module's top-right.
    "TP_ADC11": (86.0, 3.0, 0),
    "TP_ADC12": (90.0, 3.0, 0),
    "TP_GND": (94.0, 3.0, 0),
    "TP_CLK": (97.0, 4.0, 0),
    "SW1": (86.0, 8.0, 0),
    "R_BTN": (97.0, 10.0, 90),
    "J_PWR": (88.0, 22.0, 0),
    "C_BP12": (96.5, 20.0, 90),
    "C_BN12": (96.5, 24.0, 90),
    "C_B3V3": (96.5, 28.0, 90),
    "TP_3V3": (96.5, 32.0, 0),

    # --- the seam -----------------------------------------------------------
    # Both jumpers straddle the plane split: pad A lands in the digital zone
    # (which ends at y 47.5), pad B in the analog one (which starts at 48.7),
    # so the 1.3 mm pad pitch bridges the 1.2 mm moat and nothing else does.
    # JP_GND is the star point and sits directly below the SM's own ground
    # pins A4/A7 (74.46, 41.27 and 40.73) with nothing between them. 6.08 mm
    # is the floor for that: the ground pads are 4.73 mm inside the module's
    # outline, and the jumper has to clear the outline plus its own courtyard.
    # JP_3V3 sits the same way under A10 (+3V3 OUT).
    "JP_GND": (74.46, 48.0, 270),
    "JP_3V3": (82.08, 48.0, 270),

    # --- analog: the bottom band, left (audio and the pots) -----------------
    # The jack's opening faces the left board edge -- its own footprint marks
    # where the edge belongs, at local x -1.8 -- directly under B1/B2.
    "J_AUDIO": (2.6, 56.0, 0),
    "TP_AUDIO_L": (6.0, 66.0, 0),
    "TP_AGND": (10.0, 66.0, 0),
    "TP_A3V3": (14.0, 66.0, 0),
    "RV1": (26.0, 50.5, 270),
    "RV2": (39.5, 50.5, 270),
    "RV3": (53.0, 50.5, 270),
    "RV4": (66.5, 50.5, 270),
    "RV5": (26.0, 65.0, 270),
    "RV6": (39.5, 65.0, 270),
    "RV7": (53.0, 65.0, 270),

    # Reference dividers and the spare-channel ties, between the pots and the
    # muxes. Both reference pairs stay together so the 10k/10k and 1k/1k
    # source impedances differ only in the resistors, not in the wiring.
    "R_REFA1": (60.0, 65.5, 0),
    "R_REFA2": (65.0, 65.5, 0),
    "R_REFB1": (70.0, 65.5, 0),
    "R_REFB2": (60.0, 69.5, 0),
    "R_REFC1": (65.0, 69.5, 0),
    "R_REFC2": (70.0, 69.5, 0),
    "R_SP10": (60.0, 73.5, 0),
    "R_SP11": (65.0, 73.5, 0),
    "R_SP12": (70.0, 73.5, 0),
    "R_SP13": (60.0, 77.5, 0),
    "R_SP14": (65.0, 77.5, 0),
    "R_SP15": (70.0, 77.5, 0),

    # --- analog: the bottom band, right (muxes and the COM clusters) --------
    # Both COM nets run mux pin -> test point -> DNP cap -> 0R -> sense pin
    # inside this block, so each stays a short single-layer run, and both 0R
    # links sit directly under their sense pin: R_S8 under A3 (77.00, 41.27)
    # and R_S16 under A2 (79.54, 41.27), 6.73 mm of straight vertical run each.
    "U_MUX8": (76.5, 62.0, 0),
    "C_M8": (78.98, 54.73, 90),
    "TP_COM8": (74.02, 55.3, 0),
    "C_COM8": (74.02, 51.8, 90),
    "R_S8": (77.0, 49.0, 90),
    "U_MUX16": (88.0, 62.0, 0),
    "C_M16": (92.65, 52.0, 90),
    "TP_COM16": (86.5, 52.0, 0),
    "C_COM16": (82.5, 52.0, 180),
    "R_S16": (79.54, 49.0, 90),
    "C_BA3V3": (96.5, 57.0, 90),

    # The eight 0R neighbour ties, packed around the two muxes rather than out
    # with the pots: "neighbour hard at the rail" has to be true physically,
    # which means the tie belongs at the mux pin and not at the pot.
    "R_HI1": (86.0, 72.0, 0),
    "R_LO1": (91.0, 72.0, 0),
    "R_HI2": (96.0, 72.0, 0),
    "R_LO2": (86.0, 76.0, 0),
    "R_HI3": (91.0, 76.0, 0),
    "R_LO3": (96.0, 76.0, 0),
    "R_HI4": (96.5, 62.0, 90),
    "R_LO4": (96.5, 66.0, 90),
}

DOMAIN = {
    "U_SM": "seam", "JP_GND": "seam", "JP_3V3": "seam",

    "U_SR1": "digital", "U_SR2": "digital", "U_IN1": "digital",
    "C_SR1": "digital", "C_SR2": "digital", "C_IN1": "digital",
    "SW1": "digital", "R_BTN": "digital", "C_B3V3": "digital",
    "C_BP12": "digital", "C_BN12": "digital", "J_PWR": "digital",
    "TP_3V3": "digital", "TP_GND": "digital", "TP_CLK": "digital",
    "TP_ADC11": "digital", "TP_ADC12": "digital",
    "D1": "digital", "D2": "digital", "D3": "digital", "D4": "digital",
    "D5": "digital", "D6": "digital", "D7": "digital", "D8": "digital",
    "R_LED1": "digital", "R_LED2": "digital", "R_LED3": "digital",
    "R_LED4": "digital", "R_LED5": "digital", "R_LED6": "digital",
    "R_LED7": "digital", "R_LED8": "digital",

    "U_MUX16": "analog", "U_MUX8": "analog",
    "C_M16": "analog", "C_M8": "analog", "C_BA3V3": "analog",
    "C_COM16": "analog", "C_COM8": "analog",
    "TP_COM16": "analog", "TP_COM8": "analog",
    "R_S16": "analog", "R_S8": "analog",
    "RV1": "analog", "RV2": "analog", "RV3": "analog", "RV4": "analog",
    "RV5": "analog", "RV6": "analog", "RV7": "analog",
    "R_HI1": "analog", "R_HI2": "analog", "R_HI3": "analog",
    "R_HI4": "analog", "R_LO1": "analog", "R_LO2": "analog",
    "R_LO3": "analog", "R_LO4": "analog",
    "R_REFA1": "analog", "R_REFA2": "analog", "R_REFB1": "analog",
    "R_REFB2": "analog", "R_REFC1": "analog", "R_REFC2": "analog",
    "R_SP10": "analog", "R_SP11": "analog", "R_SP12": "analog",
    "R_SP13": "analog", "R_SP14": "analog", "R_SP15": "analog",
    "J_AUDIO": "analog", "TP_AUDIO_L": "analog",
    "TP_A3V3": "analog", "TP_AGND": "analog",
}

# Two full-width bands with a 1.2 mm moat between them, and the same outlines
# on In1 and In2 -- a supply plane that did not follow its own return would be
# the one thing this coupon exists to measure. Full-width is the point: an
# earlier draft on the smaller board had the analog region reach up into the
# module's interior, which left the digital plane bridging over it through a
# 3 mm neck. Here neither plane is ever narrower than the board.
_DIGITAL = [(1, 1), (99, 1), (99, 47.5), (1, 47.5)]
_ANALOG = [(1, 48.7), (99, 48.7), (99, 79), (1, 79)]

ZONE_RECTS = {
    ("In1.Cu", "AGND"): _ANALOG,
    ("In1.Cu", "GND"): _DIGITAL,
    ("In2.Cu", "A+3V3"): _ANALOG,
    ("In2.Cu", "+3V3"): _DIGITAL,
}

assert set(PLACE) == set(DOMAIN)
