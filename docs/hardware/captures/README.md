# Raw board captures — 2026-09-18/19 codec-tone campaign

Serial output from the Daisy Patch Submodule, as captured. Unedited.

These are here because committed files cite them **as the provenance of
numbers that ship**, and a citation pointing into deletable scratch is not a
citation:

| capture | what depends on it |
|---|---|
| `task-3-board-capture-1c15987.txt` | boot 1 of the four-boot boot-virgin floor |
| `task-4-board-capture-438fd51.txt` | boot 2 of the same |
| `task-4-board-capture-c5631f4.txt` | boot 3 of the same |
| `task-5-board-capture.txt` | boot 4, plus the whole window sweep |

The four-boot floor is `BOOT_VIRGIN_FLOOR` in `../../../shell/read_tone.py`,
which gate G8(b) compares against, and the same four columns appear in the
codec-tone design spec's section 7. Each figure is the `settled_mean_spread`
field of the `SHELL_TONE_STAT` lines at `case=-1` through `case=-5` — the
negative case numbers are the per-victim boot-virgin rows, emitted once per
boot before any `StartAudio()` and re-emitted verbatim every block.

`task-5-board-capture.txt` opens mid-block and closes mid-block; **lines 898
to 1852 are the one complete block**, and that block is also vendored as
`../../../shell/testdata/tone-block-86070d9.txt`, which the reader's guard
parses on every `ctest` run.

The round-one crosstalk capture of 2026-09-19 is **not** here: at 2 MB it is
an order of magnitude larger than these four, and its first complete block is
already committed in derived form as `../2026-09-19-xtalk.csv` and
`../2026-09-19-xtalk.csv.meta.csv`. Its `g6=0` readings across ten blocks are
recorded in `../crosstalk-measured.md` and exist in raw form only in the
session workspace.

A capture is evidence, not a document. Nothing here is edited to read better,
and where a capture contradicts a write-up, the capture wins.
