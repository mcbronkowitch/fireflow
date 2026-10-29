# CPU hunt on Rev A: board measurements

Rev A #1, branch `feat/cpu-hunt-remote`. Spec:
[`2026-10-29-cpu-hunt-remote-design.md`](../superpowers/specs/2026-10-29-cpu-hunt-remote-design.md),
plan [`2026-10-29-cpu-hunt-remote.md`](../superpowers/plans/2026-10-29-cpu-hunt-remote.md).
Every number below was printed by the board; captures in [`captures/`](captures/)
as `cpu-hunt-2026-10-29-*.txt`.

## 1. Setup visit (plan Task 7), 2026-10-29

Bastian at the board. Cabling: board outputs on Twin inputs 1 + 2; the board's
audio input is unconnected. Images: the measurement image
(`SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1 SHELL_REMOTE=1 SHELL_REMOTE_MEASURE=1`,
clean build), first `f53409a`, then `80b37ce` (adds workload `bastian`). Every
flash was a pair, QSPI first. The persisted patch survived all of them
(`SHELL_PERSIST src=flash seq=579` before and after).

| Step | Verdict |
|---|---|
| 1, build and first flash | **pass**: Bastian put the board in DFU; the runner flashed the pair and confirmed `build=f53409a` |
| 3, PING and Bastian's vector | **pass**: `pong`; `SHELL_PLAY_V` identical over all 39 frames of 20 s; stored as workload `bastian` (id 145) |
| 4, remote flash rehearsal | **pass**: `boot` → DFU → `flash` → `build=80b37ce` confirmed, no hands |
| 5, entry-to-entry period | 959 826 - 960 094 cycles on quiet frames: 960 000 minus a crystal offset of ~100, jitter under 300. The 965 000 period threshold stands |
| 5a, guard under a full burn | **pass** (on `r4_noon`): `guard tripped by=cb`, `over=8`, the log kept coming, the burn stopped (`cb` ~30 k); `TOG 30 0` then `LOAD` → audio back |
| 5b, BOOT near saturation | **pass**: with `LATE 36` the report frames took ~4x as long (`n=1045` blocks per frame instead of ~250), `over=851`, `gap` up to 982 703; `BOOT` put the board in DFU within 1 s |
| 5c, full saturation, guard off | **wedges, as documented**: happened by accident (below); no serial for 20 s, RESET by hand brought it back |
| 6, self-recovery | **pass for this pairing**: `80b37ce` SRAM over `f53409a` QSPI → DFU device present for all 40 s watched, no CDC port; the right pair then flashed remotely |
| 6b, IWDG across a bootloader reset | not run (needs a probe image; optional) |
| 7, power cycle by plug | skipped: no plug (spec §9) |

### Bastian's patch trips the overrun guard

On the persisted patch (WAVE + SYNTH, no excite) the guard trips by itself:
8 callbacks in a row over 950 000 cycles. After both boots it tripped at block
8788 and 8789 (~17.6 s), with `cb_max` 965 790 and 968 494. After an `RST` it
tripped again about 20 s later (block 18 801, `cb_max` 959 181). Until the next
`RST`, `LOAD` or `HOME` the output is silent, so on this patch the board is
mostly silent, and runs of workload `bastian` will come back `tripped=1`.
Quiet frames run at `cb_last` ~873 000 - 912 000, `busy_p50` ~890 000.
Captures: `…-bastian-patch.txt`, `…-bastian-retrip.txt`.

### Pot takes during remote runs

`void=1` showed up in runs nobody was meant to touch: rows 35 `COMP_B` and 72
`REV_MIX_B` during a run, then rows 6, 26, 50 and 64 right after a `LOAD` while
a 1 000 000-cycle burn was running. No hand was on the panel (Bastian). Both
bursts came in the first blocks of a burn longer than the block period (bit 30
repeating `LATE 1000`), right before the guard tripped. No take appeared in
any other capture of the visit: Bastian's patch, `r4_noon` at ~910 000 cycles,
and the near-saturated 5b runs at `LATE 30` and `LATE 36`, where callbacks
still ended inside the period. So far takes have come only with callbacks
running past the period; the mechanism is not measured.

### The 5c accident and the 5b order

Bit 30 repeats the *last* `LATE`. Step 5b as written (`TOG 31 1`, `TOG 30 1`,
`LATE k`) therefore repeats the previous step's `LATE 1000` between the second
and third command, with the guard already off: that is 5c. The board wedged
and needed RESET by hand. The working order is `TOG 31 1`, `LATE k`, then
`TOG 30 1`; the plan text is corrected.
