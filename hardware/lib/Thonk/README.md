# Thonk footprints (vendored)

`Thonk.pretty/SW_Push_LP_Button.kicad_mod` — Thonk's low-profile push button
(DPDT; momentary OFF-(ON) or latching), from Thonk's own KiCad package
https://www.thonk.co.uk/wp-content/uploads/2024/08/THONK-SW-Push-LP-Button.zip,
downloaded 2026-09-29 and committed unchanged. The 3D model in that ZIP is
not vendored; the footprint's model line points at `${THONK_3D_MODELS}`.

Contacts (Thonk datasheet, LOW-PROFILE-PUSH-BUTTONS.pdf): free, 2–3 and 5–6
are closed; pushed, 1–2 and 4–5. Rev A uses pin 2 as common and pin 1 as the
contact that closes when pressed; 3–6 stay unconnected. Panel cutout 6.2 mm;
16.5 mm above the board unlatched.
