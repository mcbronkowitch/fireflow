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

2. THE CALIBRATION WAS CROSS-CHECKED TWICE, AND ONLY THE SECOND CHECK CARRIES
   THE 16 HP WIDTH. The homography fits all four holes exactly by construction,
   so it cannot check itself; something outside it has to.

   (a) The Daisy Seed's 0.1-inch header. An FFT of its pad row gives a pitch of
   12.84-12.94 px across a range of row bands, i.e. 5.05-5.09 px/mm against
   2.54 mm. Read that carefully before leaning on it. Those pads run left to
   right, so it is an X-direction ruler -- and it agrees with the homography's
   Y scale (5.064, within 0.1-0.2 %) while disagreeing with its X scale (5.036)
   by 0.4-0.8 %, on the very axis it measures. It is also biased in a known
   direction: the Daisy is mounted on the FRONT of the panel, a few millimetres
   nearer the camera than the plate, so it images slightly large. A 0.6 %
   excess is what a standoff of 0.6 % of the camera distance produces -- a
   couple of millimetres at product-photo range, which is exactly where the
   board sits. So this check rules out a GROSS error and nothing finer: an
   eyeballed reading of the same header suggested ~4 px/mm, which would have
   made the plate 20 HP, and the FFT killed that. It does NOT confirm the
   assumed hole insets, and it is not evidence for 16 HP over 15.5 or 16.5.

   (b) The hole rectangle's aspect. The four centres span 333.75 px x 620.27 px.
   Assume only that the photo's scale is isotropic -- no homography involved,
   nothing fitted -- and that the vertical inset is the Doepfer 3.0 mm: the
   620.27 px cover 128.5 - 2*3.0 = 122.5 mm, i.e. 5.0635 px/mm, which puts the
   horizontal hole span at 65.91 mm and, with the 7.5 mm side inset, the plate
   at 80.91 mm = 15.93 HP. That is 16 HP to 0.5 %, and it is the argument that
   actually supports the width. It still assumes both insets; if Synthux used
   different ones, every number in this file moves by a common factor.

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
   0.6 % (~0.5 mm across the plate): that is the spread between the
   homography's own x and y scales at plate centre and the header ruler.

THE PADS ARE THE WEAK ROW, AND HERE IS EXACTLY WHY
--------------------------------------------------
The Touch 2's lower third is an engraved gold "map" on black. Segmenting it
resolves the field into TEN copper cells, not twelve. The table below is the
run these numbers came from, and it reproduces at exactly these settings and
not at "any threshold": mean-RGB grey, Gaussian blur sigma 1.0, grey-scale
morphological closing (MaxFilter(3) then MinFilter(3), which erases the
artwork's 1-3 px hatching while keeping the 5-15 px channels between plates),
threshold 102/255, 4-connected labelling, minimum cell area 400 px, field taken
from y >= 77.1 mm downwards. Re-running that produces ten cells whose centroids
sit 0.39 mm from the ones below on average and 0.87 mm at worst.

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

TEN IS NOT AN INVARIANT. Sweeping the same pipeline over blur in {0, 1.0},
closing k in {3,5,7,9}, threshold 60..198, 4- and 8-connectivity and minimum
area in {300, 500, 629} px, the count runs from 1 to 15 over 3360 settings; ten
comes up in 142 of them. Six to ten covers the settings where the field still
reads as plates. Counts above ten appear only above threshold ~160, where the
field is disintegrating rather than resolving: total copper falls from ~72 000
px to 30-50 000 px and the extra cells are 300-1000 px crumbs lying beside two
edge cells still measuring 12-17 000 px.

What IS invariant across that whole sweep is the part that matters. The two
edge cells stay the two largest and never split, and the silver "ice" wedges
never separate from the gold beside them -- there is no channel between them at
any setting. The larger edge cell is 3.06x the median cell area (19265 / 6293)
and the smaller 2.70x. No setting resolves this photograph into twelve
comparable plates, so the MPR121's twelve electrodes (spec 2.3) are not visible
as twelve islands here at all.

PADS below is therefore MEASURED IN ITS STRUCTURE AND DERIVED IN ITS COUNT: one
place per detected cell, and the two remaining places come from splitting each
of the two oversized edge cells by a 2-means partition of its own pixels
(PADS[0]/PADS[5] from the left cell, PADS[4]/PADS[9] from the right). Every
centre lands on bright plate material -- that was checked by drawing the twelve
crosses back onto the photo -- but not on gold in every case: PADS[0] and
PADS[4], the upper halves of the two 2-means splits, sit on the silver wedges
(sampled RGB (179,192,199) and (207,226,234), against (110,88,34)..(216,188,134)
for the ten gold ones), and whether those wedges are electrodes at all is
unknown -- see the swappable-art-plate caveat below. The twelfth-order split is
imposed, not observed. The field itself is solid: copper runs from y = 77.1 mm
to the plate's bottom edge, full width.

One hypothesis nobody has closed: the lower third may be one of Touch 2's five
SWAPPABLE art plates (spec 2.4). If it is, the visible gold is decoration and
the MPR121 electrodes sit underneath it, unphotographable -- which is why the
silver wedges' status above is "unknown" rather than "probably not a pad".
There is no evidence either way in this image. It would not change what Task 3
draws; it would change what "the true pad centres" means.

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
# 417 px frame -- so the naive 417 / 81.28 = 5.130 sits 1.89 % above this
# x scale (2.06 % if you put the frame's 417 px against the 408.6 px the
# plate actually occupies across the top edge).
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

# Left to right within three bands (five, five, two) -- the board's own
# grouping, not a grid and NOT reading order: the bands overlap vertically, so
# e.g. PADS[1] sits 7.90 mm ABOVE PADS[0]. See "THE PADS ARE THE WEAK ROW"
# above before trusting any single entry to better than a couple of millimetres.
PADS = [
    (12.25, 87.29), (25.79, 79.39), (39.99, 82.82), (54.77, 83.16),
    (70.23, 87.61),
    (6.60, 110.45), (24.04, 102.62), (39.49, 109.42), (57.45, 105.89),
    (74.92, 110.86),
    (24.77, 121.71), (56.65, 122.46),
]
