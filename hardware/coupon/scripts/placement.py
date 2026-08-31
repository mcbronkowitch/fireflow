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

**The SM is rotated 180 deg.** At 0 deg banks A and B both face the board's
top edge, where there is no room; at 180 deg they both face the board's
bottom edge, which is the only free strip on an 80x60 board. The sense pins
(A2/A3) and the audio pins (B1/B2) then look straight into the analog
region, and the unused bank C plus the shift-chain's latch/data-in (D1/D10)
take the top, where the digital cluster lives anyway. 90/270 deg are not
available at all: the pad field is 61.35 x 36.16 mm, so a quarter turn needs
61.35 mm of board height and there are 60.

Placed at (34, 20) the module's own outline (a 68 x 40 silkscreen rectangle,
read from the footprint) covers x 0..68, y 0..40 -- more than half the board.
The layout that follows is the consequence:

  * the bottom strip y 40..60 is the ANALOG region: audio jack at the left
    edge under B1/B2, three pots, both muxes and their COM clusters at the
    right end under A2/A3;
  * the module's own pad-free interior carries the DIGITAL cluster (the two
    595s, the 165, the LEDs and every decoupler) on the left and an ANALOG
    tongue -- four more pots and the tie/reference resistors -- on the right,
    all of it surface mount, all of it under a module that stands ~11 mm off
    the board on its sockets;
  * the right-hand column x 68..80 is DIGITAL: the Eurorack header beside the
    supply pins, the button, and the probe points that must stay reachable.

Nothing tall and nothing probeable sits under the module -- except four pots
that have nowhere else to go. See the Task-2 report: 80 x 60 is 300-400 mm^2
short of what this part list needs, and design.py was out of scope here.
"""

# --- the module -------------------------------------------------------------
SM = (34.0, 20.0, 180)        # body x 0..68, y 0..40; banks per docstring

PLACE = {
    "U_SM": SM,

    # --- digital: the module's interior, left (x 9..28) ----------------------
    # Shift chain and its decouplers. Every 100n sits off its package's SHORT
    # end, never beside it, and that is forced rather than tidy: on SOIC-16
    # the VCC pad stops 0.80 mm short of the courtyard's end and 1.27 mm short
    # of its side, so a cap parked alongside lands at 2.24 mm however hard it
    # is pushed, and only the end placement comes in under the 2 mm rule
    # (1.82 mm measured). The two 595s are turned 180 deg for the same reason:
    # that puts pin 16 at the bottom, away from the LED rows above them.
    "U_SR1": (14.0, 14.5, 180),
    "C_SR1": (11.52, 21.8, 270),
    "U_SR2": (14.0, 30.0, 180),
    "C_SR2": (11.52, 37.3, 270),
    "U_IN1": (23.5, 20.5, 0),
    "C_IN1": (25.98, 13.2, 90),
    "R_BTN": (23.5, 28.5, 0),
    "C_B3V3": (23.5, 32.0, 0),

    # The eight LEDs and their series resistors, two rows along the module's
    # TOP interior edge: the switched load that makes the noise question
    # realistic, put as far from the audio corner as the board allows. Moving
    # them here from the bottom rows took the closest audio-to-LED pad
    # approach from 4.63 mm to 8.85 mm (measured on the built board). Rule 2
    # asks for 10 mm and this board cannot give it -- the SM's own B2 and B8
    # pads are 3.59 mm apart, which no placement can change.
    "D1": (9.5, 2.5, 90),
    "D2": (12.0, 2.5, 90),
    "D3": (14.5, 2.5, 90),
    "D4": (17.0, 2.5, 90),
    "D5": (19.5, 2.5, 90),
    "D6": (22.0, 2.5, 90),
    "D7": (24.5, 2.5, 90),
    "D8": (27.0, 2.5, 90),
    "R_LED1": (9.5, 6.5, 90),
    "R_LED2": (12.0, 6.5, 90),
    "R_LED3": (14.5, 6.5, 90),
    "R_LED4": (17.0, 6.5, 90),
    "R_LED5": (19.5, 6.5, 90),
    "R_LED6": (22.0, 6.5, 90),
    "R_LED7": (24.5, 6.5, 90),
    "R_LED8": (27.0, 6.5, 90),

    # --- digital: the right column (x 68..80) -------------------------------
    # The only board edge the module leaves free. The IDC header sits beside
    # the SM's own supply pins (A1/A5/A10 at x 56..66, y 33..35), the two
    # 10u bulk caps directly under it, and the probe points that have to stay
    # reachable with the module plugged in.
    "SW1": (70.0, 4.0, 0),
    "TP_ADC11": (69.5, 11.5, 0),
    "TP_ADC12": (72.5, 11.5, 0),
    "TP_GND": (75.5, 11.5, 0),
    "TP_CLK": (78.5, 11.5, 0),
    "J_PWR": (72.0, 19.5, 0),
    "C_BP12": (70.0, 36.8, 0),
    "C_BN12": (75.0, 36.8, 0),
    "TP_3V3": (78.3, 36.8, 0),

    # --- the seam -----------------------------------------------------------
    # Both jumpers straddle the plane split: pad A lands in the digital zone
    # (which ends at y 38.3), pad B in the analog one (which starts at 39.5),
    # so the 1.3 mm pad pitch bridges the 1.2 mm moat and nothing else does.
    # Both want to sit under the SM's own GND / +3V3 pins (A4/A7 at x 58.5,
    # A10 at x 66.1) and neither can: that whole stretch of the moat is taken
    # by the two COM clusters, whose 15 mm budget is the harder constraint.
    # They sit ~11 and ~22 mm west instead -- still the only place where the
    # two ground planes and the two supply planes meet.
    "JP_GND": (48.0, 38.9, 270),
    "JP_3V3": (44.0, 38.9, 270),

    # --- analog: the tongue under the module (x 29..55) ----------------------
    # Four pots that the bottom strip cannot hold, plus the reference dividers
    # and the spare-channel ties. Surface mount under a socketed module is
    # fine; the pots are not, and are the concession this board size forces.
    "RV1": (38.0, 5.5, 270),
    "RV3": (51.0, 5.5, 270),
    "RV5": (38.0, 19.5, 270),
    "RV7": (51.0, 19.5, 270),
    "R_REFA1": (30.0, 33.6, 0),
    "R_REFA2": (34.0, 33.6, 0),
    "R_REFB1": (38.0, 33.6, 0),
    "R_REFB2": (42.0, 33.6, 0),
    "R_REFC1": (46.0, 33.6, 0),
    "R_REFC2": (50.0, 33.6, 0),
    "R_SP10": (30.0, 36.6, 0),
    "R_SP11": (34.0, 36.6, 0),
    "R_SP12": (38.0, 36.6, 0),

    # --- analog: the bottom strip, left (audio + pots) -----------------------
    # The jack's opening faces the left board edge (its own footprint marks
    # the edge at local x -1.8), directly under B1/B2.
    "J_AUDIO": (2.6, 47.0, 0),
    "TP_AUDIO_L": (4.5, 56.0, 0),
    "TP_AGND": (8.5, 56.0, 0),
    "TP_A3V3": (12.5, 56.0, 0),
    "RV2": (24.45, 44.5, 270),
    "RV4": (37.50, 44.5, 270),
    "RV6": (50.55, 44.5, 270),
    "R_SP13": (20.0, 41.5, 0),
    "R_SP14": (26.0, 41.5, 0),
    "R_SP15": (32.0, 41.5, 0),

    # The eight 0R neighbour ties, in the strip below the pots.
    "R_HI1": (16.0, 58.5, 0),
    "R_LO1": (20.5, 58.5, 0),
    "R_HI2": (25.0, 58.5, 0),
    "R_LO2": (29.5, 58.5, 0),
    "R_HI3": (34.0, 58.5, 0),
    "R_LO3": (38.5, 58.5, 0),
    "R_HI4": (43.0, 58.5, 0),
    "R_LO4": (47.5, 58.5, 0),
    "C_BA3V3": (52.0, 58.5, 0),

    # --- analog: the bottom strip, right (muxes and the COM clusters) --------
    # Both COM nets run mux pin -> test point -> DNP cap -> 0R -> sense pin,
    # entirely inside this block, so each stays a short single-layer run. The
    # 0R lands 5-6 mm under A3 (SENSE_ADC10_MUX8) and A2 (SENSE_ADC9_MUX16).
    "U_MUX8": (58.6, 51.0, 0),
    "C_M8": (61.08, 43.73, 90),
    "TP_COM8": (56.12, 43.5, 0),
    "C_COM8": (56.12, 40.2, 90),
    "R_S8": (59.4, 40.2, 0),
    "U_MUX16": (69.5, 51.5, 0),
    "C_M16": (74.15, 41.48, 90),
    "TP_COM16": (63.5, 41.0, 0),
    "C_COM16": (66.9, 41.0, 0),
    "R_S16": (70.8, 41.0, 0),
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

# The digital zone is a U: the analog tongue (x 29..55, y 5..39.5) reaches up
# into the module's interior and splits the digital region in two, so the
# plane bridges over it along the board's top edge. Every pair keeps a 1 mm
# gap, and the two In1/In2 pairs share their outlines exactly -- a supply
# plane that did not follow its own return would be the one thing this coupon
# exists to measure.
_DIGITAL = [(1, 1), (79, 1), (79, 38.3), (56, 38.3),
            (56, 4), (28, 4), (28, 38.3), (1, 38.3)]
_ANALOG = [(1, 39.5), (29, 39.5), (29, 5), (55, 5), (55, 39.5),
           (79, 39.5), (79, 59), (1, 59)]

ZONE_RECTS = {
    ("In1.Cu", "AGND"): _ANALOG,
    ("In1.Cu", "GND"): _DIGITAL,
    ("In2.Cu", "A+3V3"): _ANALOG,
    ("In2.Cu", "+3V3"): _DIGITAL,
}

assert set(PLACE) == set(DOMAIN)
