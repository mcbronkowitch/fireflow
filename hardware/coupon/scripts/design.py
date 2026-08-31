#!/usr/bin/env python3
"""The FireFlow test coupon, as data. This file is the source of truth.

The coupon is the measuring place for Phase-0 Task 6 step 5b and for the
requirement list in the envelope spec section 5 (addendum 2026-08-30). It is not
a prototype of the instrument: it carries the smallest circuit on which each of
those eight points can actually be measured.

Read `REQUIREMENTS` below against that list -- each entry names the parts that
serve it, and `scripts/test_coupon.py` walks them.

Every net name says what the net IS, not which peripheral the Patch SM symbol
happens to name the pin after: the submodule's symbol calls our four raw ADC
inputs UART_RX/UART_TX/SPI_MISO/SPI_MOSI and our four chain GPIOs
I2C1_SCL/I2C1_SDA/SPI_NSS/SPI_SCK. See hardware/lib/README.md.
"""

# --- board ------------------------------------------------------------------
# The hardware roadmap sizes the coupon at "~5x5 cm". It cannot be: the Patch SM
# landing pattern alone is 61.35 mm wide, and the module's body outline covers
# 68 x 40 mm of whatever board it sits on.
#
# 80 x 60 was tried and measured short. The parts that cannot live in the
# module's shadow -- it stands ~11 mm off the board on its sockets, so every
# pot shaft, connector, button and probe point has to be outside it -- want
# 1839 mm^2, and 80 x 60 leaves about 2100 mm^2 of L-shaped scraps that pack to
# six of the seven pots at best. 100 x 80 leaves 5280 mm^2 and is still inside
# the cheap fab tier (<=100 x 100 mm at the usual vendors -- confirm at order
# time). See scripts/placement.py for what the extra 40 mm bought.
BOARD_W_MM = 100.0
BOARD_H_MM = 80.0
LAYERS = 4          # ground plane on In1, supply on In2; see README

# --- the parts the eight points need ----------------------------------------
# Point numbering follows envelope spec section 5, addendum 2026-08-30.
REQUIREMENTS = {
    1: ("both chip footprints, 8:1 and 16:1", ["U_MUX16", "U_MUX8"]),
    2: ("COM capacitor as an unpopulated footprint", ["C_COM16", "C_COM8"]),
    3: ("probe point directly on COM", ["TP_COM16", "TP_COM8"]),
    4: ("10k and 20k side by side on one mux", ["RV1", "RV2", "RV3", "RV4"]),
    5: ("two neighbours hard-wired to the rails", ["R_HI1", "R_LO1", "R_HI2", "R_LO2"]),
    6: ("fixed-divider reference channels", ["R_REFA1", "R_REFA2", "R_REFB1", "R_REFB2"]),
    7: ("the real 595 chain on B7/B8/D1/D10", ["U_SR1", "U_SR2", "U_IN1"]),
    8: ("supply topology as a switchable 0R link", ["JP_GND", "JP_3V3"]),
}

# --- nets -------------------------------------------------------------------
# Power. AGND/A3V3 are the analog side of the two links in point 8.
GND, AGND = "GND", "AGND"
P3V3, A3V3 = "+3V3", "A+3V3"
P12, N12, P5 = "+12V", "-12V", "+5V"

# The four raw ADC pins of io-budget section 3, named for their real use.
SENSE_16 = "SENSE_ADC9_MUX16"    # A2  = ADC_9,  carries the 74HC4067
SENSE_8 = "SENSE_ADC10_MUX8"     # A3  = ADC_10, carries the 74HC4051
SENSE_SPARE_1 = "SENSE_ADC11_TP"  # D8  = ADC_11, brought to a test point
SENSE_SPARE_2 = "SENSE_ADC12_TP"  # D9  = ADC_12, brought to a test point

# The four chain GPIOs.
SR_DATA, SR_CLK = "SR_DATA_OUT", "SR_CLK"      # B7, B8
SR_LATCH, SR_DIN = "SR_LATCH", "SR_DATA_IN"    # D1, D10

# Mux addressing, all four lines driven from the 595 chain and shared.
ADDR = ["MUX_A0", "MUX_A1", "MUX_A2", "MUX_A3"]
EN16, EN8 = "MUX16_EN_N", "MUX8_EN_N"

COM16, COM8 = "MUX16_COM", "MUX8_COM"
AUDIO_L, AUDIO_R = "AUDIO_OUT_L", "AUDIO_OUT_R"

# --- channel plan -----------------------------------------------------------
# The measured channels are the ones whose two neighbours sit at opposite rails,
# which is the rig Phase-0 step 5b calls for: only then does short settling show
# up at all. CH2 and CH6 on the 16:1 are those channels.
#
# The two reference dividers are the difference between "the scan is noisy" and
# "the pot is noisy": REF_A has the source impedance of a 10k pot at mid travel,
# REF_B is a tenth of that. If REF_B is quiet and REF_A is not, the noise came
# in through the source impedance and not through the scan.
MUX16_CHANNELS = [
    (0,  "RV1 wiper, 10k"),
    (1,  "tied to A+3V3  (neighbour high)"),
    (2,  "RV2 wiper, 10k -- MEASURED, neighbours 1 and 3 at opposite rails"),
    (3,  "tied to AGND   (neighbour low)"),
    (4,  "RV3 wiper, 20k"),
    (5,  "tied to A+3V3  (neighbour high)"),
    (6,  "RV4 wiper, 20k -- MEASURED, neighbours 5 and 7 at opposite rails"),
    (7,  "tied to AGND   (neighbour low)"),
    (8,  "REF_A: 10k/10k divider, mid scale, 5k source impedance"),
    (9,  "REF_B: 1k/1k divider, mid scale, 500R source impedance"),
    (10, "spare, tied to AGND"),
    (11, "spare, tied to AGND"),
    (12, "spare, tied to AGND"),
    (13, "spare, tied to AGND"),
    (14, "spare, tied to AGND"),
    (15, "spare, tied to AGND"),
]

# The 8:1 exists only to answer "does the chip choice change the timing", so it
# repeats the shape rather than the whole plan: one measured 10k channel with
# opposite neighbours, and one reference.
MUX8_CHANNELS = [
    (0, "RV5 wiper, 10k"),
    (1, "tied to A+3V3  (neighbour high)"),
    (2, "RV6 wiper, 10k -- MEASURED, neighbours 1 and 3 at opposite rails"),
    (3, "tied to AGND   (neighbour low)"),
    (4, "RV7 wiper, 20k"),
    (5, "tied to A+3V3"),
    (6, "REF_C: 10k/10k divider, mid scale"),
    (7, "tied to AGND"),
]

POT_VALUES = {"RV1": "10k", "RV2": "10k", "RV3": "20k", "RV4": "20k",
              "RV5": "10k", "RV6": "10k", "RV7": "20k"}

N_LEDS = 8          # the digital load that makes the noise question realistic
