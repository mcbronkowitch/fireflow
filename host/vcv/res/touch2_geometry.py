#!/usr/bin/env python3
"""Measured control centres of the Synthux Simple Touch 2, in millimetres.

Data only: no drawing, no Rack, no SVG. res/gen_flow_panel.py consumes this,
and res/test_touch2_geometry.py guards it on its own.

WHERE THE PIXELS LIVE
---------------------
SRC_IMAGE points OUTSIDE this repository on purpose. It is a photograph of a
Synthux product and mcbronkowitch/fireflow is public, so the image is committed
to the owner's private website repo instead and only its path travels with the
code. The guard does not read the image, so nothing here depends on it being
present; only the provenance is weaker than a committed file would be.

HOW THESE NUMBERS WERE OBTAINED
-------------------------------
Not by eye. A throwaway analysis script (numpy + PIL, not kept -- the numbers
below are its whole output) did this:

1. CALIBRATION. The four Eurorack mounting holes are bright annuli on black.
   Each centre was found by maximising a ring score -- mean brightness on a
   circle of radius r minus mean brightness on the circle at 0.5 r -- over a
   sub-pixel grid of (cx, cy, r). Ring profiles are flat to cv <= 0.16, so the
   centres are good to well under half a pixel:

       TL px(50.50, 29.75)   TR px(383.70, 30.20)
       BL px(48.00, 649.50)  BR px(382.30, 651.00)

   Those four points were mapped onto the Doepfer panel standard -- hole centres
   7.5 mm from the side edges, 3.0 mm from top and bottom -- by a homography,
   which absorbs the photo's slight off-axis tilt (the plate's left edge sits at
   px 12.9 at the top and px 10.2 at the bottom). Every centre below is a pixel
   position pushed through that homography.

2. THE CALIBRATION WAS CHECKED AGAINST A SECOND RULER. The Daisy Seed's
   0.1-inch header is visible in the photo. An FFT of its pad row gives a pitch
   of 12.86 px, i.e. 5.065 px/mm against 2.54 mm. The homography's own local
   scale at plate centre is 5.036 px/mm (x) and 5.064 px/mm (y). The two rulers
   agree to 0.6 %, which is what confirms both the 16 HP width and the assumed
   hole insets -- neither was taken on faith.

   Two more consistency checks fell out and were not used to fit anything:
   the knob field's centre lands 0.05 mm off the plate centre line, the two
   faders' midpoint 0.03 mm off it, and the four upper knobs sit on one row to
   within 0.16 mm at a column pitch of 15.83 / 15.90 / 15.84 mm.

3. PER-CLASS DETECTION. Each control class has its own signature:

   | class    | how it was found                          | residual        |
   |----------|-------------------------------------------|-----------------|
   | KNOBS    | gold silkscreen collar, ring score        | +/- 0.2 mm      |
   | JACKS    | metal nut ring, ring score                | +/- 0.2 mm      |
   | SWITCHES | knurled nut ring, ring score              | +/- 0.3 mm      |
   | FADERS   | midpoint of the two mounting screws       | +/- 0.2 mm      |
   | PADS     | see below -- NOT a clean measurement      | +/- 2 mm, and   |
   |          |                                           | partly derived  |

   On top of every residual sits a systematic scale uncertainty of about
   0.6 % (~0.5 mm across the plate) from the two rulers' disagreement.

THE PADS ARE THE WEAK ROW, AND HERE IS EXACTLY WHY
--------------------------------------------------
The Touch 2's lower third is an engraved gold "map" on black. Segmenting it
(Gaussian blur, threshold, 8-connected labelling; then again with grey-scale
morphological closing, which erases the artwork's 1-3 px hatching while keeping
the 5-15 px channels between plates) resolves the field into TEN copper cells,
not twelve, whatever the threshold:

    px area  centroid mm       what it is
     19265   ( 9.86,  97.08)   left column, incl. the silver "ice" wedge
     16966   (72.10,  96.87)   right column, incl. its silver wedge
      7389   (24.77, 121.71)   bottom-left island
      7201   (57.45, 105.89)   right-centre island
      6397   (39.49, 109.42)   narrow vertical strip, centre
      6189   (56.65, 122.46)   bottom-right island
      5250   (24.04, 102.62)   left-centre island
      1819   (54.77,  83.16)   top strip, right of centre
      1503   (39.99,  82.82)   top strip, centre
       629   (25.79,  79.39)   top strip, left of centre (a sliver)

The two edge cells are ~2.7x the median area, and the silver wedges are
continuous with the gold beside them -- there is no channel between them at any
threshold. So twelve electrodes (the MPR121 has exactly twelve, spec 2.3) do not
appear as twelve islands in this photograph.

PADS below is therefore MEASURED IN ITS STRUCTURE AND DERIVED IN ITS COUNT: one
place per detected cell, and the two remaining places go to the two oversized
edge cells, split by a 2-means partition of their own pixels. Every centre lands
on real copper -- that was checked by drawing the twelve crosses back onto the
photo -- but the twelfth-order split is imposed, not observed. The field itself
is solid: copper runs from y = 77.1 mm to the plate's bottom edge, full width.

Spec 11 already schedules a 600 dpi scan of the arrived board to replace this
table. This row is the reason that job exists.

WHAT THIS FILE IS NOT
---------------------
Read off a PHOTOGRAPH. Perspective and lens are in these numbers. They are good
enough for a Rack panel -- Rack draws at ~2.95 px/mm and nothing it renders can
resolve the error -- and they are NOT a manufacturing source. This file is not a
faceplate draft. When the board arrives, a 600 dpi flatbed scan replaces this
table and everything downstream regenerates.

Channel names are the board's own, from the TouchFX sketch's ASCII drawing
(Synthux-Academy/simple-touch-instruments, daisyduino/TouchFX):

    |-| (*)   (*)   (*)    (*) |-|
    | | S31   S32   S33    S34 | |
    |||                        |||
    |_| (*)                (*) |_|
    S36 S30                S35 S37

      S10 o o S09    o S07
                    o S08
"""

# Outside the repo on purpose -- see "WHERE THE PIXELS LIVE" above.
SRC_IMAGE = (r"C:\Users\bernd\Documents\AI\FireFlow_Website"
             r"\docs\reference\touch2-fx-2026-08-11.png")

PLATE_W = 81.28          # 16 HP
PLATE_H = 128.5          # Eurorack 3U

# Local scale of the homography at plate centre, x axis. NOT image_width /
# PLATE_W: the photo is not edge to edge. The plate's own corners land at
# px (12.9, 14.6) and (421.5, 15.1) -- the right edge is cropped off the
# 417 px frame -- so the naive ratio would be wrong by 1.6 %.
PX_PER_MM = 5.036        # y axis measures 5.064; the two differ by 0.6 %

# Upper row S31 S32 S33 S34 (left to right), then lower row S30, S35.
KNOBS = [
    (16.90, 45.42), (32.73, 45.48), (48.63, 45.34), (64.47, 45.50),
    (16.89, 62.82), (64.46, 62.83),
]

# S36 (left), S37 (right). FADER_TRAVEL is the drawn slot outline, outer edge
# to outer edge (128 px on both faders). The electrical travel is shorter --
# the two mounting screws sit 31.4 mm apart, which reads like a 20 mm unit.
FADERS = [(4.61, 56.75), (76.74, 56.80)]
FADER_TRAVEL = 25.29

# S09/S10 (left), S07/S08 (right) -- both sit INSIDE the pad field.
SWITCHES = [(30.34, 86.37), (45.25, 92.88)]

# Upper, lower.
JACKS = [(4.31, 15.15), (4.33, 30.60)]

# Reading order, top-left to bottom-right. Five, five, two -- the board's own
# grouping, not a grid. See "THE PADS ARE THE WEAK ROW" above before trusting
# any single entry to better than a couple of millimetres.
PADS = [
    (12.25, 87.29), (25.79, 79.39), (39.99, 82.82), (54.77, 83.16),
    (70.23, 87.61),
    (6.60, 110.45), (24.04, 102.62), (39.49, 109.42), (57.45, 105.89),
    (74.92, 110.86),
    (24.77, 121.71), (56.65, 122.46),
]
