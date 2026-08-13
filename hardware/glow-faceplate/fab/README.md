# Glow faceplate fabrication package

No fabrication package is present. The current project is limited to verified
reference capture and non-production development.

## Export condition

Gerbers, drill files, and the 1:1 production proof may be generated only from
the reviewed KiCad physical master after all four production gates in the parent
[README](../README.md) are supported by linked evidence. Until then, no release
may be sent to a fabricator.

## Required package after approval

- Gerber X2 files from the approved KiCad revision.
- Excellon drill files, including non-plated holes and slots.
- A 1:1 mechanical PDF proof.
- The confirmed Synthux manufacturing profile and the captured KiCad CLI path
  and version.
- An independent Gerber-viewer review covering outline closure, dimensions,
  holes, slots, copper and mask polarity, exposed copper, text orientation,
  edge clearance, and minimum features.

The official DXF/PDF remains a mechanical reference pending Synthux
confirmation; it is not a production-profile substitute.
