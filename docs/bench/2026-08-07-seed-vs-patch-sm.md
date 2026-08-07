# Seed gegen Patch Submodule — regress / axi / -O3

**Verdikt: die Zahlen übertragen nicht.**
Das Submodule ist auf jeder einzelnen gemessenen Zeile teurer als der Seed, und
die Verschiebung des Entscheidungs-Workloads (+0,53 bis +0,80 Prozentpunkte)
ist größer als das Wiederholband beider Boards zusammen — sie ist reproduzierbar,
nicht Rauschen.

| | Seed | Patch Submodule |
|---|---|---|
| Capture | [`2026-08-07-a74c876-regress-axi-o3-usb.md`](2026-08-07-a74c876-regress-axi-o3-usb.md) | [`2026-08-07-d0b3c08-regress-axi-o3-patch_sm-usb.md`](2026-08-07-d0b3c08-regress-axi-o3-patch_sm-usb.md) |
| Git-Hash | `a74c876` | `d0b3c08` |
| Device-Fingerprint | `1157cbd9…` | `26696514…` |
| Transport | USB-CDC | USB-CDC |
| Takt / Blockgröße / Caches | 480 MHz, 96, dcache+icache | 480 MHz, 96, dcache+icache |
| SRAM_EXEC des Images | 246 820 B (93,89 %) | 254 824 B (96,94 %) |

Gleiches Profil, gleiches Layout, gleiche Optimierung, gleicher Transport. Der
Transport ist ausdrücklich derselbe: gegen ein Semihosting-Capture verglichen
würde man die 6370 Zyklen SOF-Interrupt pro Block mitbezahlen, die
[`2026-08-07-transport-semihost-vs-usb.md`](2026-08-07-transport-semihost-vs-usb.md)
ausweist, und die hätten mit dem Board nichts zu tun.

## Der Entscheidungs-Workload

`instrument_worst_bbd_dtcm`, offline / im echten Callback:

| | Lauf 1 | Lauf 2 | Wiederholband | schlechtester Wert |
|---|---:|---:|---:|---:|
| Seed | 97,03 % / 97,18 % | 96,94 % / 97,10 % | 0,09 pt | **97,03 % offline** |
| Patch Submodule | 97,56 % / 97,75 % | 97,83 % / 97,78 % | 0,27 pt | **97,83 % offline** |

Die beiden Bänder berühren sich nicht. Zwischen 97,03 und 97,56 liegen
0,53 Punkte, in denen kein einziger gemessener Wert liegt. Damit ist die
Bedingung aus dem Plan erfüllt: die Bewegung ist größer als das Wiederholband,
also übertragen die Seed-Zahlen nicht.

**Beide Boards passen weiterhin.** Der Reservekeil schrumpft von 2,97 auf
**2,17 Punkte**.

Unangenehmer ist die AXI-Nachbarschaft: `instrument_worst_bbd` geht von
97,59 auf 99,25 %, `inst_bbd_engine_worst` von 97,83 auf 99,67 %. Beide sind
auf dem Submodule weniger als einen halben Punkt unter dem Blockbudget.

## Was gleich geblieben ist

Jede Zeilen-Prüfsumme der 24 Workloads ist identisch zwischen den beiden
Captures. Es wird auf beiden Boards dieselbe Rechnung gerechnet; nur der Preis
ist ein anderer. Die Firmware meldet auf beiden 480000000 Hz Kerntakt und
`dcache+icache`, also ist es kein Takt- und kein Cache-Unterschied.

## Das Muster

Der Aufschlag ist nicht gleichmäßig, aber er hat eine Form: rechenlastige
Einzelzeilen liegen bei etwa +0,4 %, die großen Instrument-Zeilen bei +1,1 bis
+1,5 %. `empty_callback` bewegt sich überhaupt nicht (2 Zyklen auf beiden).
`bbd_walk_sdram` zeigt mit +7,89 % den größten relativen Sprung, aber das sind
75 Zyklen auf 951 — bei der Größenordnung sagt der Prozentwert wenig.

| family | workload | Seed avg cyc | Patch SM avg cyc | delta cyc | delta % | Seed max % | Patch SM max % | delta pt |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| system | `empty_callback` | 2 | 2 | +0 | +0.00 | 0.00 | 0.00 | +0.00 |
| system | `mod_plane_2x_center` | 66085 | 66760 | +675 | +1.02 | 7.10 | 7.19 | +0.09 |
| system | `synth_1_voice` | 51896 | 52111 | +215 | +0.41 | 5.49 | 5.59 | +0.10 |
| system | `synth_2_voices` | 90853 | 91219 | +366 | +0.40 | 9.61 | 9.68 | +0.07 |
| system | `synth_4_voices` | 163884 | 164544 | +660 | +0.40 | 17.36 | 17.45 | +0.09 |
| system | `synth_2x4` | 328659 | 329987 | +1328 | +0.40 | 34.78 | 34.97 | +0.19 |
| system | `wave_2x4` | 284988 | 285819 | +831 | +0.29 | 30.19 | 30.38 | +0.19 |
| system | `fx_none` | 21630 | 21721 | +91 | +0.42 | 2.26 | 2.36 | +0.10 |
| system | `fx_grit` | 45679 | 45881 | +202 | +0.44 | 4.77 | 4.88 | +0.11 |
| system | `fx_flux_sdram` | 102926 | 103089 | +163 | +0.16 | 11.39 | 11.69 | +0.30 |
| system | `fx_comp` | 28979 | 29107 | +128 | +0.44 | 3.02 | 3.14 | +0.12 |
| system | `oliverb_solo_sram` | 95485 | 95615 | +130 | +0.14 | 10.05 | 10.12 | +0.07 |
| system | `instrument_init` | 634563 | 639262 | +4699 | +0.74 | 77.41 | 77.96 | +0.55 |
| system | `instrument_worst` | 970825 | 981819 | +10994 | +1.13 | 107.00 | 108.62 | +1.62 |
| system | `inst_worst_deck_bus` | 730216 | 737565 | +7349 | +1.01 | 81.32 | 81.55 | +0.23 |
| system | `instrument_worst_bbd` | 895567 | 909163 | +13596 | +1.52 | 97.59 | 99.25 | +1.66 |
| system | `instrument_worst_bbd_dtcm` | 888833 | 900657 | +11824 | +1.33 | 97.03 | 97.56 | +0.53 |
| system | `inst_bbd_engine_worst` | 895593 | 909107 | +13514 | +1.51 | 97.83 | 99.67 | +1.84 |
| bbd | `bbd_ceiling` | 51690 | 51917 | +227 | +0.44 | 5.55 | 5.66 | +0.11 |
| bbd | `bbd_line_only` | 33598 | 33743 | +145 | +0.43 | 3.55 | 3.66 | +0.11 |
| bbd | `bbd_line_tap` | 32713 | 32854 | +141 | +0.43 | 3.41 | 3.52 | +0.11 |
| bbd | `bbd_line_tap_half` | 24051 | 24139 | +88 | +0.37 | 2.51 | 2.61 | +0.10 |
| bbd | `bbd_walk_sdram` | 951 | 1026 | +75 | +7.89 | 0.41 | 0.43 | +0.02 |
| bbd | `bbd_line_stage_walk` | 35642 | 35796 | +154 | +0.43 | 3.72 | 3.82 | +0.10 |

## Was dieser Vergleich nicht zeigt

**Kein Mechanismus.** Die Ursache des Aufschlags ist hier nicht gemessen, und
sie wird deshalb auch nicht benannt. Was gemessen ist: es ist nicht der Takt
und nicht die Cache-Konfiguration, beide meldet die Firmware auf beiden Boards
gleich, und es ist nicht die Rechnung selbst, dafür stehen die identischen
Prüfsummen.

**Die beiden Images sind nicht dieselben Bytes.** Der `patch_sm`-Build linkt
rund 8000 B mehr nach SRAM_EXEC (die DaisyPatchSM-Peripherie), also liegt der
Code an anderen Adressen. Wie viel des Aufschlags auf das Board und wie viel
auf die veränderte Codeplatzierung entfällt, trennt diese Messung **nicht**.
Dass das keine akademische Sorge ist, zeigt ein Nebenbefund vom selben Tag:
eine Vier-Byte-Änderung in `bench/main.cpp` verschob jedes Symbol dahinter und
damit 100951 Byte des SRAM-Images (siehe den Kommentar an der Leerlaufschleife
in `bench/main.cpp`).

Die Trennung wäre messbar — ein Seed-Image, das auf dieselbe Größe aufgefüllt
ist, oder ein `patch_sm`-Image im `itcm-hot`-Layout — aber sie ist heute nicht
gemessen worden. Bis dahin gilt die Zahl als das, was sie ist: der Preis, den
das M6-Zielboard mit dem Image bezahlt, das es tatsächlich tragen wird.

## Konsequenz

Ab hier darf **keine Seed-Zahl mehr für eine Aussage über das Submodule
zitiert werden** — auch nicht mit Sternchen, auch nicht als Näherung. Der
Aufschlag ist klein, aber er ist systematisch und geht in genau eine Richtung,
und die Reserve, gegen die er gerechnet wird, ist 2,17 Punkte.

Für die Shell-Messung (Phase 0 Task 6) heißt das: sie läuft auf dem Submodule,
oder ihr Ergebnis ist keine Aussage über das Submodule.
