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

### Freerouting (Task 4)

**Install.** Bastian approved both downloads. Both live outside the repo in
`%LOCALAPPDATA%\fireflow-tools\`: `freerouting.jar` = freerouting-2.4.1.jar
(64,076,787 bytes, github.com/freerouting/freerouting release v2.4.1,
published 2026-09-03) and `jre/` = Eclipse Temurin
OpenJDK25U-jre_x64_windows_hotspot_25.0.4.1_1.zip (58,475,080 bytes).
Freerouting 2.4 requires Java 25.

    "$LOCALAPPDATA/fireflow-tools/jre/bin/java.exe" -version
    openjdk version "25.0.4.1" 2026-08-18 LTS
    OpenJDK Runtime Environment Temurin-25.0.4.1+1 (build 25.0.4.1+1-LTS)
    OpenJDK 64-Bit Server VM Temurin-25.0.4.1+1 (build 25.0.4.1+1-LTS, mixed mode, sharing)

    "$LOCALAPPDATA/fireflow-tools/jre/bin/java.exe" -jar "$LOCALAPPDATA/fireflow-tools/freerouting.jar" -l en -help
    USAGE
      freerouting [PARAMETERS]
    PARAMETERS
      -de <design.dsn>          Load the Specctra .dsn file
      -di <design_directory>    Set the default folder used by open-design dialogs
      -dr <design_rules_file>   Read routing rules from a .rules file
      -do <output_file>         Save the board (.dsn), session (.ses), or
                                Autodesk Fusion script (.scr) at the end
      -mp <number_of_passes>    Set the maximum number of auto-routing passes to perform
      -l  <language>            "en" for English, "de" for German, or "zh" for Simplified
                                Chinese; otherwise, use the system default. English is
                                used by default for unsupported languages.
      -mt <number_of_threads>   Set the thread-pool size for route optimization. The
                                default is one fewer than the number of logical
                                processors in the system.
      -us <updating_strategy>   Greedy, Global, or Hybrid. Set the board update strategy
                                for route optimization. The default is greedy. When
                                Hybrid is selected, use the "hr" option to specify the
                                hybrid ratio.
      -hr <m:n>                 Set the hybrid ratio in the format of
                                #_global_optimal_passes:#_prioritized_passes. The
                                default is 1:1. This option is effective only when the
                                Hybrid strategy is selected.
      -is <selection_strategy>  Sequential, random, or prioritized. Set the item-selection
                                strategy for route optimization. The default is
                                prioritized, which selects items based on scores
                                calculated during the previous round.
      -h                        Display this help

(Two-column layout condensed here; the wording is Freerouting's.) `-help`
does not list `-da`, `-inc` or `--gui.enabled=false`. The jar's argument
parser (`app/freerouting/settings/GlobalSettings.class`, string constants
read with a zipfile script) accepts `-de -di -do -drc -dr -mp -mt -oit -us
-is -hr -l -dl -da -host -inc -dct -ll`, `--help`, `--compare-boards=`,
and a generic `--<section>.<key>=<value>` form (plus `FREEROUTING__*`
environment variables) over the `freerouting.json` settings tree. The
probe runs below are the authority for what each spelling does.

**Where to look.** Freerouting appends every run to one log file,
`%LOCALAPPDATA%\freerouting\logs\freerouting.log`, at DEBUG level. The
console shows INFO and above only, so the analytics line below never
appears on the console. One run's section is the text from the last
`INFO   Freerouting v` banner to the end of the file; the probes cut it out
that way right after each run (`log[log.rfind("INFO   Freerouting v"):]`).

**What the switches do (probed).** 29 routing runs in all. The first,
aborted, run of `probe_fr.py` is one of them: its guard looked for the
analytics line on the console, found nothing, and stopped after `fr1`; the
guard was moved to the log section and the probe re-run. Counts over the
shared log after the last run (16:08:42):

    grep -c "INFO   Freerouting v"   "$LOCALAPPDATA/freerouting/logs/freerouting.log"   -> 30
    grep -c "Analytics are disabled" "$LOCALAPPDATA/freerouting/logs/freerouting.log"   -> 29
    grep -c "Screen:"                "$LOCALAPPDATA/freerouting/logs/freerouting.log"   -> 1
    grep -c "No new version"         "$LOCALAPPDATA/freerouting/logs/freerouting.log"   -> 29

30 banners = the first `-help` call (15:59:24) + 29 runs. (The second
`-help` call, with `-l en`, logged elsewhere, see below.)

- `-da`: every run logs `DEBUG  Analytics are disabled` right after the
  startup banner, before the board loads (29 of 29, count above). `-da` is
  per run only: `%APPDATA%\freerouting\freerouting.json` (created by the
  first `-help` call) still read `"allow_telemetry": true` and
  `"disable_analytics": false` after all 29 runs and was not rewritten by
  them. Afterwards, at Bastian's decision, the controller changed that file
  to `allow_telemetry=false`, `allow_contact=false`,
  `disable_analytics=true`; runs still pass `-da`.
- `--gui.enabled=false`: headless -- rc 0, no window, and the only
  `Screen:` line in the log is the `-help` call's (`Screen: 3440x1440, 96
  DPI`). Run time, banner to "Successfully saved output file", from
  `grep -n "INFO   Freerouting v\|Successfully saved output file" <log>`:
  `fr1` 16:03:00.473 -> 16:03:02.632 (2.16 s); over all 29 pairs of that
  grep the shortest is `v_k12_noLwires` (16:06:04.392 -> 16:06:06.050,
  1.66 s) and the longest `v_k12_LFonly` (16:07:30.791 -> 16:07:32.956,
  2.17 s). The routing stage itself takes about 0.5 s of that
  (`Auto-routing stage completed ... completed in 0.50 seconds`).
- `-mp 20` / `-mt 1`, from `grep -h "Applied CLI router setting" $S/fr1.log`:

      2026-09-29 16:03:01.593 DEBUG  Applied CLI router setting: router.max_passes = 20
      2026-09-29 16:03:01.593 DEBUG  Applied CLI router setting: router.max_threads = 1
      2026-09-29 16:03:01.593 DEBUG  Applied CLI router setting: router.enabled = true (implicit from -de/-do batch mode)

  Whether 20 passes are enough on the real strip is unknown; on the probe
  boards the router stopped before 20 on its own
  (`grep -h "Stopping the auto-router" $S/fr1.log $S/nk1.log`):

      INFO   [629BBE\AC39E8] The router's best score (666.64) has not improved by more than 0.5 points since pass #8. Stopping the auto-router after 18 passes (1 item still unconnect...
      INFO   [6FAB30\8E695D] The router's score (999.97) has not improved by more than 0.5 points in the last 10 passes (0 items still unconnected). Stopping the auto-router.
- `-inc Other`: accepted, no error, **no effect**. On `nk.dsn` (below) the
  SES with `-inc Other` is byte-identical to the one without it, and net B
  (class `Other`) is routed in both. `--router.ignore_net_classes=Other`
  is logged as `Applied CLI router setting: router.ignore_net_classes =
  Other` and is equally byte-identical (raw output under "DSN variants").
  *Inferred, not probed:* the string `ignoreNetClasses` occurs, outside the
  settings classes, only in `gui/board/GuiManager.class`, which suggests it
  is applied only in the GUI path.
- `-l en`: switches the help and GUI language, but **also moved the log
  file** to `<cwd>\en\freerouting.log` (startup log: `log file :
  C:\Users\bernd\Documents\AI\FireFlow\en\freerouting.log`). The stray
  folder was deleted; `-l` is not part of `FR_ARGS`.
- Not switchable, found in the run logs: every routing run makes a version
  check (`DEBUG  No new version available. Current version is up to date:
  v2.4.1`); the jar's `util/VersionChecker` requests
  `https://api.github.com/repos/freerouting/freerouting/releases/latest`
  with the User-Agent `Freerouting-Version-Checker` (string constants of
  that class). No setting for it was found in the jar's settings classes.
  It is not the analytics client. Bastian accepted this version check on
  2026-09-29.
- The two `-help` calls (one by the controller, one here) ran without
  `-da`; their logs contain neither `Analytics are disabled` nor a version
  check line, so whether they sent anything cannot be told from the log.

**Probe board.** `probe_fr.py` (scratchpad; the brief's skeleton plus
logging), under `KIPY`: 40 x 20 mm, 2 layers, SMD test-point pads. Nets A
(5,10)-(35,10) and B (20,3)-(20,17) must cross; net L (5,15)-(35,15) is
drawn on F.Cu and B.Cu and locked; a keepout covers x 26..30, y 0..12 on
both layers; classes `Sig` = {A}, `Other` = {B}.

`$S` is the session scratchpad folder `...\scratchpad\task4`; the probe
echoes a filtered part of each run's log section (the `|` lines; the first
24 characters, the timestamp, are cut off). Full output of the second,
complete run:

    "/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe" "$S/probe_fr.py"
    export True
    resolution line: ['(resolution um 10)']
    fix wires in DSN: 2 keepouts: 2
    class lines: ['(class kicad_default L', '(class Sig A', '(class Other B']
    RUN java.exe -jar freerouting.jar -de fr.dsn -do fr1.ses --gui.enabled=false -da -mp 20 -mt 1
    freerouting rc 0 ses written True
       | DEBUG  Command line arguments: '-de fr.dsn -do fr1.ses --gui.enabled=false -da -mp 20 -mt 1'
       | DEBUG  GUI Language: de_DE
       | DEBUG  Analytics are disabled
       | DEBUG  Set DEFAULT clearance (all layers): 2000 (0.05 mm) from DSN value 200.0
       | DEBUG  Set clearance (all layers): smd_smd = 500 (0.0125 mm), classes [2,2]
       | DEBUG  Set DEFAULT clearance (all layers): 2000 (0.05 mm) from DSN value 200.0
       | DEBUG  Set clearance (all layers): smd_smd = 500 (0.0125 mm), classes [2,2]
       | DEBUG  No new version available. Current version is up to date: v2.4.1
       | DEBUG  Set DEFAULT clearance (all layers): 2000 (0.05 mm) from DSN value 200.0
       | DEBUG  Set clearance (all layers): smd_smd = 500 (0.0125 mm), classes [2,2]
       | DEBUG  Applied copper-to-edge clearance override: 500.0 um (5000 board units).
       | DEBUG  Set DEFAULT clearance (all layers): 2000 (0.05 mm) from DSN value 200.0
       | DEBUG  Set clearance (all layers): smd_smd = 500 (0.0125 mm), classes [2,2]
       | INFO   [629BBE\AC39E8] Fanout stage completed: started with 6 total SMD pins, completed in 0.09 seconds, escaped pins: 6/6 (100.0%), using 0.08 total CPU seconds, 0.09 GB total allocated, and 123.4 MB peak
       | s could not be routed -- please review your design (e.g. check pad clearances, trace width rules, and available routing space):
       | INFO   [629BBE\AC39E8] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.50 seconds, final score: 666.65 (1 unrouted and 0 violations), using 0.00 total CPU seconds, 0.00 GB total a
       | INFO   [629BBE\AC39E8] Optimization stage completed: started with score 666.65 (1 unrouted and 0 violations), completed in 0.01 seconds, final score: 666.65 (1 unrouted and 0 violations), using 0.00 total C
    RUN java.exe -jar freerouting.jar -de fr.dsn -do fr2.ses --gui.enabled=false -da -mp 20 -mt 1
    freerouting rc 0 ses written True
       | (the same lines; job id F02A4B\07FA12, auto-routing 0.49 s, final score 666.65 (1 unrouted and 0 violations))
    two runs byte-identical: True
    import True
    locked geometry unchanged: True
    still locked after import: True count 2
      L before: [('L', 'B.Cu', 5000000, 15000000, 35000000, 15000000, True), ('L', 'F.Cu', 5000000, 15000000, 35000000, 15000000, True)]
      L after : [('L', 'B.Cu', 5000000, 15000000, 35000000, 15000000, True), ('L', 'F.Cu', 5000000, 15000000, 35000000, 15000000, True)]
    tracks starting inside the keepout: 0
    unconnected after import: 2
    RUN java.exe -jar freerouting.jar -de fr-cc.dsn -do fr-cc.ses --gui.enabled=false -da -mp 20 -mt 1
    freerouting rc 0 ses written True
       | (the same lines; job id 7E75F0\E8C097, auto-routing 0.55 s, final score 666.65 (1 unrouted and 0 violations))
    class_class SES written: True
    RUN java.exe -jar freerouting.jar -de fr.dsn -do fr-inc.ses --gui.enabled=false -da -mp 20 -mt 1 -inc Other
    freerouting rc 0 ses written True
       | (the same lines; job id 36B433\7432E5, auto-routing 0.50 s, final score 666.65 (1 unrouted and 0 violations))
    -inc Other: SES mentions net B wires: 1 net A: 0
    exit=0

The unrouted connection, `grep -A2 "could not be routed" $S/fr1.console.txt`:

    The following connections could not be routed -- please review your design (e.g. check pad ...
      Net 'A' (1 unrouted connection):
        - TP_A0-1  ->  TP_A1-1

- Units: `grep -n "(resolution\|(unit" $S/fr.dsn` prints `8:  (resolution
  um 10)` and `9:  (unit um)`. Values in the DSN are in um (`(width 250)` is
  KiCad's 0.25 mm), so the brief's `(clearance 3000)` means 3 mm.
  `resolution um 10` is the internal grid, 10 steps per um; the SES writes
  its coordinates in those steps (width `2500` = 0.25 mm). Freerouting's own
  DEBUG lines label its units wrongly (`DSN=250.0 -> board=2500 (0.0625
  mm)`, `DEFAULT clearance ... 2000 (0.05 mm) from DSN value 200.0`).
- Locked tracks: the SES carries **no** wire of net L at all (`fr1.ses`:
  `network_out` holds only `(net B ...)`). They survive because KiCad's
  import keeps locked tracks; unlocked, the import deletes them:

      "/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe" "$S/probe_fr6.py"
      L tracks before import: 2 locked: [False, False]
      import True
      L tracks after import: 0
      B tracks after import: 9

  Proven for two straight locked tracks only; locked vias and zones were not
  tested.
- `unconnected after import: 2` is net A's one unrouted connection plus one
  L-to-L pair: L's B.Cu copy touches no pad (the pads are SMD, F.Cu only),
  so it is an island of net L, separate from the F.Cu copy. The DRC
  `unconnected_items` block lists these two items, not two pads of A
  (`probe_fr5.py` saves the imported board and runs `kicad-cli pcb drc`;
  `pcb_proof.unconnected_by_net()` reads pads only, which is why it named A
  alone):

      "/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe" "$S/probe_fr5.py" "$S/fr1.ses"
      import fr1 True
        live unconnected: 2
        vias: [('B', (5.5621, 16.2196)), ('B', (15.4688, 3.8923))]
        drc: {'track_dangling': 1, 'unconnected_items': 2}
        unconnected by net: {'A': {'TP_A0.1', 'TP_A1.1'}}

      $S/drc-fr1.rpt, lines 5-18:
      ** Found 1 DRC violations **
      [track_dangling]: Track has unconnected end
          Local override; warning
          @(5.0000 mm, 15.0000 mm): Track [L] on B.Cu, length 30.0000 mm

      ** Found 2 unconnected pads **
      [unconnected_items]: Missing connection between items
          Local override; error
          @(5.0000 mm, 10.0000 mm): Pad 1 [A] of TP_A0 on F.Cu
          @(35.0000 mm, 10.0000 mm): Pad 1 [A] of TP_A1 on F.Cu
      [unconnected_items]: Missing connection between items
          Local override; error
          @(5.0000 mm, 15.0000 mm): Track [L] on F.Cu, length 30.0000 mm
          @(5.0000 mm, 15.0000 mm): Track [L] on B.Cu, length 30.0000 mm

  Before any import the same board counts 3 (A, B, and the L island):

      "/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe" "$S/probe_fr5.py" "$S/none.ses"
        L tracks: [('B.Cu', (5.0, 15.0), (35.0, 15.0), True), ('F.Cu', (5.0, 15.0), (35.0, 15.0), True)]
        pad TP_L1 (35.0, 15.0) L F
        pad TP_L0 (5.0, 15.0) L F
        live unconnected: 3
        vias: []
        drc: {'track_dangling': 1, 'unconnected_items': 3}
        unconnected by net: {'A': {'TP_A1.1', 'TP_A0.1'}, 'L': {'TP_L0.1'}, 'B': {'TP_B0.1', 'TP_B1.1'}}

- Keepout: `tracks starting inside the keepout: 0` is **vacuous** on this
  board -- A is unrouted and B's route runs nowhere near x 26..30. What the
  DSN variants below do show: (1) with the keepout on F.Cu only
  (`k12_FCu`), A runs on F.Cu up to x 25.46, drops to B.Cu under the keepout
  and returns to F.Cu at x 30.53 -- it avoids the keepout's layer and uses
  the free one; (2) in every variant with the full two-layer keepout, A
  stayed unrouted rather than cross it (no violation reported); (3) `nk1`
  (keepout removed from the DSN only) runs A straight through the area, and
  KiCad's DRC on the imported board flags it (`items_not_allowed 1`) -- that
  shows the DRC check, not Freerouting. So Freerouting honoured the keepout
  in the one positive case (1); a wider test of keepout respect was not run.

**Surprise: net A is never routed on the brief's board.** Freerouting
reports `1 unrouted` (A) in every run with the keepout and L's locked
F.Cu wire at y 15, although a 2.9 mm corridor (y 12..14.875) is free.

*DSN variants.* All are text edits of `$S/fr.dsn` (DSN y = -KiCad y, in um).
`probe_fr2.py`:

    keep = re.findall(r"\n\s*\(keepout [^\n]*\)", txt)          # nk: both keepout lines removed
    nob = re.sub(r"\n\s*\(net B\n\s*\(pins [^)]*\)\n\s*\)", "", txt, count=1)   # noB
    nkcc: nk with "(network" -> "(network\n    (class_class (classes Sig Other) (rule (clearance 3000)))"
    nkinc: nk.dsn + ["-inc", "Other"];  nkrinc: nk.dsn + ["--router.ignore_net_classes=Other"]
    nkmt1, nkmt2: nk.dsn with "-mt", "1" dropped from the arguments

`probe_fr3.py` (`base` = fr.dsn with both keepout lines removed; `keep(y)`
re-inserts `(keepout "" (polygon <layer> 0  26000 0  30000 0  30000 -<y>
26000 -<y>  26000 0))` for F.Cu and B.Cu before the first `(via` line;
`k12` = `base` + `keep(12000)`, i.e. the export again):

    "k12_Lnotfix":  k12.replace("(net L)(type fix)", "(net L)")
    "k12_Lprotect": k12.replace("(type fix)", "(type protect)")
    "k12_L17":      L wires "5000 -15000  35000 -15000" -> "5000 -17000  35000 -17000"
    "k12_L18.5":    L wires -> "5000 -18500  35000 -18500"
    "k12_Lwest":    L wires -> "5000 -15000  20000 -15000"
    "k12_Least":    L wires -> "20000 -15000  35000 -15000"
    "k12_LBonly":   the F.Cu L wire line removed
    "k12_LFonly":   the B.Cu L wire line removed
    "k12":          keep(12000)
    "k11", "k10.5", "k9": keep(11000), keep(10500), keep(9000)
    "k12_noLwires": both L wire lines removed, keep(12000)
    "k12_FCu":      keep(12000, ("F.Cu",)) -- F.Cu keepout only

Raw output, `probe_fr2.py route`:

    "/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe" "$S/probe_fr2.py" route
    keepout lines removed for nk: 2
    net B removed for noB: True
    RUN java.exe -jar freerouting.jar -de nk.dsn -do nk1.ses --gui.enabled=false -da -mp 20 -mt 1
    freerouting rc 0 ses written True
       | DEBUG  Analytics are disabled
       | INFO   [6FAB30\8E695D] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.37 seconds, final score: 999.97 (0 unrouted and 0 violations), using 0.00 tota
       wires per net: {'A': 3, 'B': 3}
    RUN java.exe -jar freerouting.jar -de nk.dsn -do nk2.ses --gui.enabled=false -da -mp 20 -mt 1
    freerouting rc 0 ses written True
       | DEBUG  Analytics are disabled
       | INFO   [0D83BE\B5ED96] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.37 seconds, final score: 999.97 (0 unrouted and 0 violations), using 0.00 tota
       wires per net: {'A': 3, 'B': 3}
    RUN java.exe -jar freerouting.jar -de nkcc.dsn -do nkcc.ses --gui.enabled=false -da -mp 20 -mt 1
    freerouting rc 0 ses written True
       | DEBUG  Analytics are disabled
       | INFO   [1335B8\773861] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.33 seconds, final score: 999.97 (0 unrouted and 0 violations), using 0.00 tota
       wires per net: {'A': 3, 'B': 3}
    RUN java.exe -jar freerouting.jar -de noB.dsn -do noB.ses --gui.enabled=false -da -mp 20 -mt 1
    freerouting rc 0 ses written True
       | DEBUG  Analytics are disabled
       | INFO   [05C270\516741] Auto-routing stage completed: started with 1 unrouted nets, completed in 0.39 seconds, final score: 499.99 (1 unrouted and 0 violations), using 0.00 tota
       wires per net: {}
    RUN java.exe -jar freerouting.jar -de nk.dsn -do nkinc.ses --gui.enabled=false -da -mp 20 -mt 1 -inc Other
    freerouting rc 0 ses written True
       | DEBUG  Analytics are disabled
       | INFO   [2DB63B\EBB964] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.37 seconds, final score: 999.97 (0 unrouted and 0 violations), using 0.00 tota
       wires per net: {'A': 3, 'B': 3}
    RUN java.exe -jar freerouting.jar -de nk.dsn -do nkrinc.ses --gui.enabled=false -da -mp 20 -mt 1 --router.ignore_net_classes=Other
    freerouting rc 0 ses written True
       | DEBUG  Command line arguments: '-de nk.dsn -do nkrinc.ses --gui.enabled=false -da -mp 20 -mt 1 --router.ignore_net_classes=Other'
       | DEBUG  Analytics are disabled
       | DEBUG  Applied CLI router setting: router.ignore_net_classes = Other
       | INFO   [B2B057\626BCD] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.36 seconds, final score: 999.97 (0 unrouted and 0 violations), using 0.00 tota
       wires per net: {'A': 3, 'B': 3}
    byte-identical nk1.ses nk2.ses: True
    byte-identical nk1.ses nkinc.ses: True
    byte-identical nk1.ses nkrinc.ses: True
    byte-identical nk1.ses nkcc.ses: False
    RUN java.exe -jar freerouting.jar -de nk.dsn -do nkmt1.ses --gui.enabled=false -da -mp 20
    freerouting rc 0 ses written True
       | DEBUG  Analytics are disabled
       | INFO   [DD93CD\314DAD] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.37 seconds, final score: 999.97 (0 unrouted and 0 violations), using 0.00 tota
    RUN java.exe -jar freerouting.jar -de nk.dsn -do nkmt2.ses --gui.enabled=false -da -mp 20
    freerouting rc 0 ses written True
       | DEBUG  Analytics are disabled
       | INFO   [142896\D22CB4] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.37 seconds, final score: 999.97 (0 unrouted and 0 violations), using 0.00 tota
    byte-identical nkmt1.ses nkmt2.ses (no -mt): True
    exit=0

Raw output, `probe_fr3.py` (three invocations; each run also echoes its
`Analytics are disabled` line, dropped here after the first):

    "/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe" "$S/probe_fr3.py"
    == variant k12 keepout lines 2 fix wires 2
    RUN java.exe -jar freerouting.jar -de v_k12.dsn -do v_k12.ses --gui.enabled=false -da -mp 20 -mt 1
    freerouting rc 0 ses written True
       | DEBUG  Analytics are disabled
       | INFO   [4F6D6A\AC849F] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.49 seconds, final score: 666.65 (1 unrouted and 0 violations), using 0.00 tota
       wires per net: {'B': 3}
    == variant k11 keepout lines 2 fix wires 2
       | INFO   [79A0A3\5D8481] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.47 seconds, final score: 666.65 (1 unrouted and 0 violations), using 0.00 tota
       wires per net: {'B': 3}
    == variant k10.5 keepout lines 2 fix wires 2
       | INFO   [C75903\A4DD88] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.47 seconds, final score: 666.65 (1 unrouted and 0 violations), using 0.00 tota
       wires per net: {'B': 3}
    == variant k9 keepout lines 2 fix wires 2
       | INFO   [C3CDD3\87FEFC] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.34 seconds, final score: 999.97 (0 unrouted and 0 violations), using 0.00 tota
       wires per net: {'A': 3, 'B': 3}
    == variant k12_noLwires keepout lines 2 fix wires 0
       | INFO   [13C522\9D0B5D] Auto-routing stage completed: started with 3 unrouted nets, completed in 0.18 seconds, final score: 999.98 (0 unrouted and 0 violations), using 0.00 tota
       wires per net: {'A': 3, 'B': 1, 'L': 1}
    == variant k12_FCu keepout lines 1 fix wires 2
       | INFO   [7D8752\F3D44B] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.48 seconds, final score: 999.97 (0 unrouted and 0 violations), using 0.00 tota
       wires per net: {'A': 3, 'B': 3}
    exit=0

    "/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe" "$S/probe_fr3.py" k12_Lnotfix k12_Lprotect k12_L17 k12_L18.5
    == variant k12_Lnotfix keepout lines 2 fix wires 0
       | INFO   [615A7E\644415] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.35 seconds, final score: 666.66 (1 unrouted and 0 violations), using 0.00 tota
       wires per net: {'B': 3, 'L': 1}
    == variant k12_Lprotect keepout lines 2 fix wires 0
       | INFO   [F86297\83FA24] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.50 seconds, final score: 666.65 (1 unrouted and 0 violations), using 0.00 tota
       wires per net: {'B': 3, 'L': 2}
    == variant k12_L17 keepout lines 2 fix wires 2
       | INFO   [B70D8E\42D015] Auto-routing stage completed: started with 3 unrouted nets, completed in 0.69 seconds, final score: 599.99 (1 unrouted and 1 violation), using 0.00 total
       wires per net: {'A': 1, 'L': 2}
    == variant k12_L18.5 keepout lines 2 fix wires 2
       | INFO   [242D5C\FE34DB] Auto-routing stage completed: started with 3 unrouted nets, completed in 0.39 seconds, final score: 999.98 (0 unrouted and 0 violations), using 0.00 tota
       wires per net: {'A': 3, 'B': 2, 'L': 2}
    exit=0

    "/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe" "$S/probe_fr3.py" k12_Lwest k12_Least k12_LBonly k12_LFonly
    == variant k12_Lwest keepout lines 2 fix wires 2
       | INFO   [488948\F23BB5] Auto-routing stage completed: started with 3 unrouted nets, completed in 0.41 seconds, final score: 999.97 (0 unrouted and 0 violations), using 0.00 tota
       wires per net: {'A': 3, 'B': 3, 'L': 2}
    == variant k12_Least keepout lines 2 fix wires 2
       | INFO   [5B31A3\E3BD02] Auto-routing stage completed: started with 3 unrouted nets, completed in 0.37 seconds, final score: 999.97 (0 unrouted and 0 violations), using 0.00 tota
       wires per net: {'A': 3, 'B': 3, 'L': 2}
    == variant k12_LBonly keepout lines 2 fix wires 1
       | INFO   [B38E3C\9A3733] Auto-routing stage completed: started with 3 unrouted nets, completed in 0.48 seconds, final score: 999.98 (0 unrouted and 0 violations), using 0.00 tota
       wires per net: {'A': 3, 'B': 1, 'L': 2}
    == variant k12_LFonly keepout lines 2 fix wires 1
       | INFO   [9536FE\6E8ED9] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.36 seconds, final score: 666.66 (1 unrouted and 0 violations), using 0.00 tota
       wires per net: {'B': 3}
    exit=0

A's routes in the two variants the keepout text cites (SES `network_out`,
whitespace collapsed, units 0.1 um, y negated):

    v_k12_noLwires  (net A (wire (path B.Cu 2500 59017 -109017 73416 -123416 313433 -123416 336852 -99997 ) ) ...
    v_k12_FCu       (net A (wire (path F.Cu 2500 50000 -100000 254596 -100000 ) )
                           (wire (path B.Cu 2500 305295 -99997 254599 -99997 254596 -100000 ) )
                           (wire (path F.Cu 2500 350000 -100000 349997 -99997 305295 -99997 ) ) ...

Summary (unrouted = the final `N unrouted` of the run; wires per net as
printed):

    variant                                   unrouted  wires
    as exported (keepout y<=12, L F+B)          1       B 3
    keepout removed ("nk")                      0       A 3, B 3
    net B removed, keepout kept                 1       (none)
    keepout y<=11 / y<=10.5                     1       B 3
    keepout y<=9 (A's straight line is free)    0       A 3, B 3
    keepout on F.Cu only                        0       A 3, B 3 (A under it on B.Cu)
    L wires removed                             0       A 3, B 1, L 1 (A at y 12.34 on B.Cu)
    L wires not fixed / type protect            1       B 3, L 1 / L 2
    L on B.Cu only                              0       A 3, B 1, L 2
    L on F.Cu only                              1       B 3
    L only x 5..20 / only x 20..35              0       A 3, B 3, L 2
    L moved to y 17 / y 18.5                    1 / 0   A 1, L 2 (1 violation) / A 3, B 2, L 2

So with L's full-length F.Cu wire in place, A cannot detour around the
keepout even with 4.375 mm of room (keepout edge y 10.5 to L's copper edge
y 14.875), and without L it takes
that corridor at once. Mechanism not established. For Task 7: Freerouting
can leave a connection unrouted where the grid router would see room, when
a keepout and a fixed wire bound the same corridor.

**Determinism.** Byte-identical SES on two small probe boards (routing
stage about 0.5 s): `fr1`/`fr2` (`two runs byte-identical: True`, above)
and `nk1`/`nk2` (`byte-identical nk1.ses nk2.ses: True`). Two `nk.dsn` runs
*without* `-mt` were also byte-identical (`byte-identical nkmt1.ses
nkmt2.ses (no -mt): True`); the log of those runs names no thread count
(no `router.max_threads` line), so the pool size there is *inferred* from
`-help` ("one fewer than the number of logical processors") and the log's
`Hardware: 32 CPU cores` -- 31, not measured. Task 7 must re-run the real
strip twice and `cmp` the SES files; `-mt 1` stays in `FR_ARGS`.

**Leftover vias (inferred mechanism).** Imported Freerouting boards carry
vias that connect on one layer only:

    "/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe" "$S/probe_fr5.py" "$S/nk1.ses"
    import nk1 True
      live unconnected: 1
      vias: [('B', (5.5621, 16.2196)), ('B', (15.4688, 3.8923)), ('A', (33.7281, 9.9997)), ('A', (5.9019, 9.0981))]
      drc: {'items_not_allowed': 1, 'via_dangling': 2, 'track_dangling': 1, 'unconnected_items': 1}
      unconnected by net: {}

    $S/drc-nk1.rpt:
    [items_not_allowed]: Items not allowed
        Local override; error
        @(5.9019 mm, 9.0981 mm): Track [A] on F.Cu, length 26.9246 mm
    [via_dangling]: Via is not connected or connected on only one layer
        Local override; warning
        @(5.9019 mm, 9.0981 mm): Via [A] on F.Cu - B.Cu
    [via_dangling]: Via is not connected or connected on only one layer
        Local override; warning
        @(33.7281 mm, 9.9997 mm): Via [A] on F.Cu - B.Cu
    [track_dangling]: Track has unconnected end
        Local override; warning
        @(5.0000 mm, 15.0000 mm): Track [L] on B.Cu, length 30.0000 mm
    [unconnected_items]: Missing connection between items
        Local override; error
        @(5.0000 mm, 15.0000 mm): Track [L] on F.Cu, length 30.0000 mm
        @(5.0000 mm, 15.0000 mm): Track [L] on B.Cu, length 30.0000 mm

(`unconnected_items 1` here is the L island again; A and B are complete.)
And `par.ses` (below), `grep -n '(via "' $S/par.ses` and `grep -n "(path"
$S/par.ses`:

    47:        (via "Via[0-1]_600:300_um" 59050 -69050
    55:        (via "Via[0-1]_600:300_um" 340950 -69050
    71:        (via "Via[0-1]_600:300_um" 129096 -75904
    79:        (via "Via[0-1]_600:300_um" 270904 -75904
    36:          (path F.Cu 2500
    42:          (path F.Cu 2500
    50:          (path F.Cu 2500
    60:          (path F.Cu 2500
    66:          (path F.Cu 2500
    74:          (path F.Cu 2500

Four vias, every wire on F.Cu. That these are left over from the fanout
stage (`Fanout ... +5 extra vias`) is an inference from the log, not
probed. Task 7 should expect `via_dangling` warnings on imported boards.

**class_class.** On the brief's board A is unrouted, so no A-B distance
exists there. On `nk.dsn` Freerouting's own spacing is already large, so a
3 mm rule cannot show anything there (one process per SES, since
`new_board()` reseeds the KIIDs):

    K="/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe"
    for s in fr1 fr-cc nk1 nkcc; do "$K" "$S/probe_fr2.py" measure "$S/$s.ses"; done
    import fr1.ses True
      unconnected after import: 2
      segments A 0 B 7
      same-layer A x B pairs: 0
      vias A/B: 2  min via-centre to other-net centreline: n/a
      A/B segment ends inside the keepout box: 0
    import fr-cc.ses True
      unconnected after import: 2
      segments A 0 B 9
      same-layer A x B pairs: 0
      vias A/B: 2  min via-centre to other-net centreline: n/a
      A/B segment ends inside the keepout box: 0
    import nk1.ses True
      unconnected after import: 1
      segments A 5 B 7
      same-layer A x B pairs: 20
      min A-B centreline distance: 5.206 mm on F.Cu
      vias A/B: 4  min via-centre to other-net centreline: 3.084 mm
      A/B segment ends inside the keepout box: 0
    import nkcc.ses True
      unconnected after import: 1
      segments A 5 B 9
      same-layer A x B pairs: 15
      min A-B centreline distance: 6.107 mm on F.Cu
      vias A/B: 4  min via-centre to other-net centreline: 4.549 mm
      A/B segment ends inside the keepout box: 0

`probe_fr4.py` therefore moves the pads (DSN text of `nk.dsn`, no keepout:
`TP_A0 5000 -10000` -> `5000 -6000`, `TP_A1 35000 -10000` -> `35000 -6000`,
`TP_B0 20000 -3000` -> `12000 -8500`, `TP_B1 20000 -17000` -> `28000 -8500`)
so the natural routes run 2.5 mm apart -- A (5,6)-(35,6), B (12,8.5)-(28,8.5).
It measures straight from the SES geometry:

    "/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe" "$S/probe_fr4.py"
    RUN java.exe -jar freerouting.jar -de par.dsn -do par.ses --gui.enabled=false -da -mp 20 -mt 1
    freerouting rc 0 ses written True
       | DEBUG  Analytics are disabled
       | INFO   [4F06EA\0F7DE5] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.36 seconds, final score: 999.98 (0 unrouted and 0 violations), using 0.00 tota
      segments: {'A': 3, 'B': 3} vias: 4
        A F.Cu (5.905, 6.905) -> (34.095, 6.905)
        A F.Cu (5.905, 6.905) -> (5.000, 6.000)
        A F.Cu (34.095, 6.905) -> (35.000, 6.000)
        B F.Cu (12.910, 7.590) -> (27.090, 7.590)
        B F.Cu (12.910, 7.590) -> (12.000, 8.500)
        B F.Cu (27.090, 7.590) -> (28.000, 8.500)
      par: min A-B track centreline 0.685 mm -> edge-to-edge 0.435 mm (9 same-layer pairs)
      par: min track-edge to other-net pad-edge 0.720 mm
    RUN java.exe -jar freerouting.jar -de parcc.dsn -do parcc.ses --gui.enabled=false -da -mp 20 -mt 1
    freerouting rc 0 ses written True
       | DEBUG  Analytics are disabled
       | INFO   [3407F6\F77952] Auto-routing stage completed: started with 2 unrouted nets, completed in 0.32 seconds, final score: 999.98 (0 unrouted and 0 violations), using 0.00 tota
      segments: {'A': 6, 'B': 1} vias: 2
        A F.Cu (6.286, 5.573) -> (7.267, 4.592)
        A F.Cu (7.267, 4.592) -> (32.319, 4.592)
        A F.Cu (32.319, 4.592) -> (33.727, 6.000)
        A F.Cu (6.286, 5.573) -> (5.427, 5.573)
        A F.Cu (5.427, 5.573) -> (5.000, 6.000)
        A F.Cu (33.727, 6.000) -> (35.000, 6.000)
        B F.Cu (12.000, 8.500) -> (28.000, 8.500)
      parcc: min A-B track centreline 3.908 mm -> edge-to-edge 3.658 mm (6 same-layer pairs)
      parcc: min track-edge to other-net pad-edge 3.033 mm
    exit=0

With the rule A dips to y 4.592 around B's pads and track; without it A and
B run 0.435 mm apart edge to edge. The rule
`(class_class (classes Sig Other) (rule (clearance 3000)))`, inserted as the
first child of `(network`, is honoured as an edge-to-edge clearance that
covers pads as well as tracks.

**For Task 7:**

1. `FR_ARGS` = `["--gui.enabled=false", "-da", "-mp", "20", "-mt", "1"]`,
   after `-jar freerouting.jar -de <in.dsn> -do <out.ses>`; no `-l`, no
   `-inc` (it does nothing headless). After each run, check for
   `Analytics are disabled`: it is a DEBUG line, written only to
   `%LOCALAPPDATA%\freerouting\logs\freerouting.log`, never to the console;
   that run's section is the text from the last `INFO   Freerouting v`
   banner to the end of the file. Whether `-mp 20` suffices on the real
   strip is unknown.
2. No `lock_tracks()` after the import: locked tracks come back unchanged
   and still locked. Locking *before* the export is mandatory -- an
   unlocked track that is not in the SES is deleted by the import. Proven
   for two straight locked tracks; locked vias and zones not tested.
3. Byte-identical SES on two small probe boards (0.5 s runs), with `-mt 1`
   (and, on `nk.dsn`, without it). Task 7 must re-run the real strip twice
   and `cmp`.
4. `class_class` is honoured: A-B edge-to-edge 0.435 mm without it, 3.658 mm
   (tracks) and 3.033 mm (track to pad) with a 3 mm rule.
5. Keepouts: honoured in the one positive case (A switched to B.Cu under an
   F.Cu-only keepout); on the brief's board the check was vacuous. Expect
   Freerouting to leave connections unrouted where a keepout and a fixed
   wire bound a corridor, and expect `via_dangling` warnings after import.

### Our own router, 2 layers (Task 6)

`hardware/reva/spike/own.py` feeds the strip to `hardware/gen/route.py`
(PITCH 0.2, VIA_RADIUS 0.3, VIA_COST 8.0, MAX_ITERS 30, default `run()`
parameters, the router's own span order); `run.finish()` records the
unrouted count, adds the GND fill on F.Cu and B.Cu and fills. Before the
first run `via_dangling` joined the gated classes in `proof.py` (Freerouting
leaves dangling fanout vias, Task 4; gated for both methods).

    KIPY hardware/reva/spike/run.py --method own --layers 2

| run | change | seconds | failed | conflicts | iterations | vias | length mm | unrouted before fill | gated reds |
|---|---|---|---|---|---|---|---|---|---|
| 1 | none (first complete run) | 14.2 | 0 | 0 | 6 | 74 | 2682.9 | 0 | none (then ungated: `items_not_allowed 1`) |
| 1b | adapter fix: footprint rule areas fed to the router (not a budget change) | 13.8 | 0 | 0 | 6 | 74 | 2682.9 | 0 | none |

Every gated step was green on the first run, so the budget stopped there.
Run 1b is run 1 plus two fixes ruled by the controller after review
(below); it is the method's result. The router's own check (`conflicts 0`,
`failed []`) printed before the proof; the full proof takes 3.1 s, well
under the 30 s that would call for a separate partial checker.

**Fix 1: footprint rule areas (adapter).** Run 1's DRC report carried a
then-ungated `items_not_allowed 1`: `Track [MOD2_B] on F.Cu` at
(213.1, 112.3), inside the F.Cu rule area (no tracks, no vias) that the
PJ398SM footprint carries itself -- J13's is x 212.80..215.81,
y 112.50..115.50, and each of the six jacks has one (probe
`probe_ruleareas.py`, scratchpad). `own.py` fed the strip's keepouts to the
router but not the footprints' rule areas. It now passes every footprint
rule area that forbids tracks as a netless rectangle (its bounding box) on
each copper layer it covers. MOD2_B then runs at x 212.30 instead of
213.10; the length and via count do not change (the detour is a parallel
shift). `items_not_allowed` is now gated in `proof.py`: copper inside a rule
area is how a router would drive through the strip's foreign-pad keepouts.

**Fix 2: the `ratsnest` sabotage deletes the longest candidate.** On run 1
the sabotage deleted the first candidate track, a 0.102 mm pad-centre stub
(`PITCH_B B.Cu (248.800,118.920)-(248.900,118.900)`) lying entirely inside
J16.T's 2.13 mm pad, so nothing disconnected and the step stayed green
(exit 0) -- a vacuous RED. Probe (`probe_sab_ratsnest.py`, scratchpad): 712
candidates, 255 of them shorter than 0.3 mm; deleting the longest
(`LED15 B.Cu (222.500,19.700)-(258.500,19.700)`, 36.0 mm) left 1
unconnected. `_sab_ratsnest` now deletes the longest eligible segment.

Final run (1b):

    own router: {'nets': 49, 'failed': [], 'conflicts': 0, 'iterations': 6, 'vias': 74, 'length_mm': 2682.9, 'seconds': 13.8}
    unrouted before fill: 0
    wrote hardware\reva\spike\out\own-2L.kicad_pcb
        1. nets       52 nets, 0 differ from the intent node for node
        2. anchors    36 panel parts, worst 0.0000 mm off its hole (limit 0.01)
        3. decoupling 3 decouplers, worst 1.778 mm (limit 2.0)
        4. locked     14 locked segments unchanged on SENSE_1, OUT_L, OUT_R
        5. copper     gated: shorting_items 0, clearance 0, hole_clearance 0, hole_to_hole 0, tracks_crossing 0, track_dangling 0, via_dangling 0, items_not_allowed 0 (not gated: copper_edge_clearance 15, courtyards_overlap 6, pth_inside_courtyard 2, silk_edge_clearance 23, silk_over_copper 44, silk_overlap 20, starved_thermal 8)
        6. ratsnest   unconnected: 0 before fill, 0 live, 0 kicad-cli
        7. audio      nearest LED track to audio: 2.71 mm (OUT_L to LED16; coupon rule 10.0, not gated)
        8. render     rendered own-2L-top.png, own-2L-bottom.png
    proof took 3.1 s -- GREEN

**Routed-stage sabotages** (`--sabotage <name>` on the final configuration,
each exit 1):

    --sabotage ratsnest
    RED 5. copper     gated: shorting_items 0, clearance 0, hole_clearance 0, hole_to_hole 0, tracks_crossing 0, track_dangling 1, via_dangling 0, items_not_allowed 0 (not gated: copper_edge_clearance 15, courtyards_overlap 6, pth_inside_courtyard 2, silk_edge_clearance 23, silk_over_copper 44, silk_overlap 20, starved_thermal 8, unconnected_items 1)
            track_dangling: 1
    RED 6. ratsnest   unconnected: 0 before fill, 1 live, 1 kicad-cli
    proof took 3.1 s -- RED
    --sabotage audio_missing
    RED 7. audio      examined 7 audio and 0 LED segments
    proof took 3.1 s -- RED

The ratsnest sabotage also turns `copper` red: the neighbours of the
deleted segment are left as a dangling track. Under `RED 6.` the per-net
list from `unconnected_by_net()` printed no line, although kicad-cli counted
1 -- the parser did not match this report's entry (not investigated).

**The `items_not_allowed` gate can fail.** With the rule-area block in
`own.py` commented out (Edit tool), then restored the same way:

    KIPY hardware/reva/spike/run.py --method own --layers 2
    RED 5. copper     gated: shorting_items 0, clearance 0, hole_clearance 0, hole_to_hole 0, tracks_crossing 0, track_dangling 0, via_dangling 0, items_not_allowed 1 (not gated: copper_edge_clearance 15, courtyards_overlap 6, pth_inside_courtyard 2, silk_edge_clearance 23, silk_over_copper 44, silk_overlap 20, starved_thermal 8)
            items_not_allowed: 1
    proof took 3.2 s -- RED            (exit 1)

After the restore the run is GREEN again (exit 0). The placement stage
(`--method none`) stays GREEN with the two new gated classes:
`via_dangling 0, items_not_allowed 0`, proof 2.7 s.

**Reproducibility.** Two runs of the final configuration, the first board
copied to the scratchpad in between (router 14.9 s and 15.0 s):

    cmp "$S/own-2L-first.kicad_pcb" hardware/reva/spike/out/own-2L.kicad_pcb
    (no output, exit 0)

**The pictures** (`routing-spike/own-2L-top.png`,
`routing-spike/own-2L-bottom.png`): the top side carries a vertical bundle of
four to six tracks down the middle of the strip between the upper pot rows
and a set of long 45-degree runs in the lower-left quarter between the
jacks and the RV67/RV70 pots; the bottom side fans out into the port column
along the west edge, with a diagonal bundle running from east of U_MUX5
towards the lower ports and long straight runs such as LED15's 36 mm along
y 19.7. Nothing hugs the outline except the port approaches and the locked
OUT_R run; as far as the render's resolution shows, the vias sit singly at
the ends of top-side runs, not in clusters. Run 1b's bottom render is
byte-identical to run 1's; the top render differs (MOD2_B moved off J13's
rule area; whether other nets moved as well was not diffed).

![own router, top](routing-spike/own-2L-top.png)
![own router, bottom](routing-spike/own-2L-bottom.png)
