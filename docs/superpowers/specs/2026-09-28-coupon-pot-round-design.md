# The coupon pot round — design (round four)

> **Status:** designed 2026-09-28 with Bastian, section by section; built and
> measured on the board the same day (plan beside it; captures under
> `docs/hardware/captures/pot-*-capture-*.txt`). Two things ran differently
> from this text: the pots were set to what `REF_A` reads on the bring-up
> image (about 31734 on its 63484-rail scale), not to 32768 — see §7; and
> §5's claim that the block estimate and the loop share one constant does not
> hold, because the firmware never calls the estimate — it is a host-side
> bound only. It is the round that closes Phase-0 Task 6 step 5b
> (`docs/superpowers/plans/2026-08-07-fireflow-phase-0-hardware-foundation.md`)
> on real pots, and it asks round three's question
> ([`wait-measured.md`](../../hardware/wait-measured.md)) of the same pots.
>
> **Every number here is derived or quoted, never measured by this round**, and
> each says which. The instruments are the settle probe
> (`shell/settle_probe.cpp`, round of 2026-09-17/18,
> [`settle-measured.md`](../../hardware/settle-measured.md)) and the wait probe
> (`shell/wait_probe.cpp`, spec
> [`2026-09-27-coupon-wait-sweep-probe-design.md`](2026-09-27-coupon-wait-sweep-probe-design.md)).
> Neither is redesigned here; both gain three victims behind one switch.

## 1. What this is

Every coupon round so far has measured fixed dividers. The pots have been on
the board since 2026-09-17 (`hardware/coupon/README.md`, "Bringing it up") and
have had one reading: a wiring check at the clockwise stop, where a pot's
source impedance is near zero and says nothing about settling.

`REF_A` and `REF_C` (10k/10k dividers, 5000 Ω + ~150 Ω switch Ron = 5150 Ω)
already stand in for a **20 k** pot at mid travel. What is not measured:

1. **Whether a real pot behaves like the divider** of the same source
   impedance — wiper contact, track, body capacitance are not in a divider.
   RV4 against `REF_A` on the same mux, in the same boot, is the direct test.
2. **The 10 k pot**, 2650 Ω at mid travel, which no reference channel covers:
   the board's dividers sit at 650 Ω (`REF_B`) and 5150 Ω.
3. **The wait effect on a pot.** Round three measured a scan that reads a
   5150 Ω channel once per ~2 ms block reading ~450 counts low at the working
   rung, and the 387.5-cycle rung removing it at every wait. Whether the rung
   does the same for a real pot is the number the scan firmware needs next.

**Correction carried by this spec:** `hardware/coupon/scripts/design.py:76`
calls `REF_A` "the source impedance of a 10k pot at mid travel". A 10k/10k
divider is 5 kΩ, which is a **20 k** pot at mid travel (R/4).
`shell/settle_plan.cpp:7-9` already says it correctly; `design.py`'s comment
gets fixed in this round's first task (comment only — the channel plan and the
netlist do not change).

## 2. The switch

One new build switch, **`SHELL_POT_ROUND`** (`0` default, `1`), valid with
either `SHELL_SETTLE_PROBE=1` or `SHELL_WAIT_PROBE=1` and an error with
neither (it would select nothing). It is written as a header by
`shell/write_shell_pot_round.py`, same shape and same reason as
`write_shell_wait_probe.py`: the header always defines the symbol, and the
script deletes the dependent objects itself whenever the content changes,
because make's one-second mtime resolution on this machine has shipped a
stale object before (memory `fireflow-bench-stale-object-trap`). The switch
is resolved while the Makefile is parsed, like every other one.

**At `SHELL_POT_ROUND=0` both images measure and print exactly what they do
today.** No field is added to an existing line; the pot round announces
itself with lines of its own (§6). `read_settle_guard` and `read_wait_guard`
stay green against their committed fixtures unchanged. Byte-identical images
are not promised; identical behaviour and output are.

**Build proof:** both switch positions of both images are built and `cmp`
must report them different before anything is flashed.

## 3. The pot table

A new header, `shell/pot_plan.h`, `inline constexpr` for the same reason
`kXtalkVictimTable` is (a constant expression may not read a merely-`const`
object from another translation unit):

| pot | group (mux) | ch | high neighbour | low neighbour | R_track (nominal) | R_src at mid (nominal) |
|---|---|---:|---:|---:|---:|---:|
| RV2 | 0 (4067, `ADC_9`) | 2 | 1 | 3 | 10 k | 2650 Ω |
| RV4 | 0 (4067, `ADC_9`) | 6 | 5 | 7 | 20 k | 5150 Ω |
| RV6 | 1 (4051, `ADC_10`) | 2 | 1 | 3 | 10 k | 2650 Ω |

Channels and neighbours are `design.py`'s `MUX16_CHANNELS`/`MUX8_CHANNELS`;
R_src at mid is R_track/4 + 150 Ω, the same Ron every other table here
carries. RV4 is 20 k and RV2/RV6 are 10 k per `design.py`'s `POT_VALUES`, and
that is what is fitted (`order-bom.md`).

**R_track is nominal and stays nominal.** The pot ends hang on `A+3V3` and
`AGND` together with three dividers and `R_BTN`, so the track cannot be
metered in circuit. Every derived impedance below inherits the part's
unmeasured tolerance; the RV4-against-`REF_A` comparison (§4) is read with
that in mind, and the write-up says so wherever it quotes an R_src.

## 4. The settle half

With `SHELL_POT_ROUND=1`, `kSettlePlan` grows from six pairs to twelve. The
first six are today's, unchanged in order and content, so P0–P5 mean what
they meant. The new six:

| pair | group | from | to | R_src | 9.01 τ node prediction |
|---|---:|---:|---:|---:|---:|
| P6 | 0 | 1 (A+3V3) | 2 (RV2) | 2650 | 1552 ns |
| P7 | 0 | 3 (AGND) | 2 (RV2) | 2650 | 1552 ns |
| P8 | 0 | 5 (A+3V3) | 6 (RV4) | 5150 | 3016 ns |
| P9 | 0 | 7 (AGND) | 6 (RV4) | 5150 | 3016 ns |
| P10 | 1 | 1 (A+3V3) | 2 (RV6) | 2650 | 955 ns |
| P11 | 1 | 3 (AGND) | 2 (RV6) | 2650 | 955 ns |

The predictions are **derived** from `tools/settle_budget.py`'s `terms()`
(term A, the node settle, which is what the table's `tau9_ns` column has always
carried), run 2026-09-28. They are nominal-impedance predictions; the pot
sits wherever Bastian turned it (§7).

**The sampling rung** is chosen per pair by `sample_time_index_for()` from the
table's R_src, as today. Probed 2026-09-28 against the current source: 2650 Ω
selects index 0 (1.5 cycles), 5150 Ω index 1 (2.5 cycles). So P6/P7/P10/P11
run at the 991 ns offset of the 150 Ω and 650 Ω pairs, and P8/P9 at the
1154 ns offset of the 5150 Ω pairs (`settle-measured.md` §1).

**What is expected, stated before the run:** `settle-measured.md` §9 found
true settle 1.3–1.5× the model on all three resolvable divider pairs. Carried
over unchanged, that predicts roughly 2.0–2.3 µs for RV2, 3.9–4.5 µs for RV4
and 1.2–1.4 µs for RV6. **RV6 is expected at the instrument's floor**: 1.2 µs
true settle minus a 991 ns offset is a knee at grid index 1 or 2. If it lands
at or below the offset, the reader's existing `at_or_below_offset` flag says
so and the write-up reports a bound, not a settle time. That outcome is named
here so it cannot be read as a failure afterwards.

**Gates stay the instrument's, not the pots'.** G1 (the two reference pairs)
and G4 (jitter) do not read P6–P11 and are unchanged. **G3** — the settled
region's peak-to-peak spread across pairs — is computed over **P0–P5 only**
at either switch position, so a wiper that wanders more than a divider cannot
refuse the run that is measuring it. The pot pairs' own settled-region spread
is printed per pair on the existing `SHELL_SETTLE_BAND` line and reported,
not gated. G2 is unchanged.

**What 5b's answer is:** a true settle time (knee + offset) per pot pair,
compared two ways — against the model (does 1.3–1.5× hold on a real pot?) and
RV4 against `REF_A`'s P1/P2 in the same boot (does a real pot differ from the
divider at nominally the same impedance?). The second comparison is the new
finding; the first is 5b's own question.

## 5. The wait half

With `SHELL_POT_ROUND=1` the wait probe runs **eight** victims instead of five:
round one's five (`kXtalkVictimTable`), unchanged and first, then RV2, RV4,
RV6 from `pot_plan.h`, through **all four arms** (A wait, B discard, L long
rung, C codec bridge). The working rung is chosen per victim the way it is
today, so RV2/RV6 run arms A/B/C at 1.5 cycles and RV4 at 2.5; arm L is
387.5 cycles for every victim.

Case numbering stays `arm × victims + victim`, so at `SHELL_POT_ROUND=0` it is
exactly today's 0…19 and at 1 it is 0…31. The wait probe keeps
`kXtalkVictimTable` as its source for victims 0–4 and does not copy it.

**Gates:** G2, G4, G7 and G9 are unchanged. **G5** (address) judges every
victim against `coupon_expect`; the pot channels are `Expect::Unchecked`
there (`shell/coupon_expect.cpp:12-20`), so G5 passes them trivially. Their
position is judged by PG1 instead (§7), which is where it belongs: a pot has
no fixed expected value, only a window.

**Cost, derived:** `wait_block_estimate_ms()` scales linearly with the victim
count; it estimates 89.7 s at five and the board measured 89.6 s
(`wait-measured.md` §2). At eight: **~143.5 s**. The function takes the victim
count as an argument; the firmware does not call it, so it is a host-side
bound only. `block_ms` is printed and is the measurement.

**What is expected, stated before the run:** RV4 saturates near `REF_A`'s
−858 counts, give or take the pot's tolerance; RV2 and RV6 land somewhere
between `REF_B` (−18 at 650 Ω) and `REF_A`, and **no model predicts where** —
round three characterised the impedance ordering, it did not fit it. The
number the scan firmware needs is arm L: **does the long rung hold every pot
within ±3 counts at every wait**, as it held every divider?

## 6. Output

At `SHELL_POT_ROUND=0`: nothing new.

At `SHELL_POT_ROUND=1`, both images print, once per block, **directly after**
their existing configuration line (`SHELL_SETTLE_CFG` / `SHELL_WAIT_CFG`).
After, not before: both readers open a block at that line and drop anything
printed ahead of it (`read_settle.py` `parse_block()`, `read_wait.py`
`_read_one_block()`), so a line printed before it would never be read.

```
SHELL_POT_CFG round=1 pots=3 git=<hash>
SHELL_POT_ID idx=0 name=RV2 group=0 ch=2 r_track=10000 hi=1 lo=3
SHELL_POT_ID idx=1 name=RV4 group=0 ch=6 r_track=20000 hi=5 lo=7
SHELL_POT_ID idx=2 name=RV6 group=1 ch=2 r_track=10000 hi=1 lo=3
```

Everything else rides on the existing lines: the settle image's pairs P6–P11
on `SHELL_SETTLE`/`_OFFSET`/`_KNEE`, the wait image's victims 5–7 on
`SHELL_WAIT_CASE`/`SHELL_WAIT`. The readers take the pair and victim count
from what arrived — `read_settle.py` already does
(`num_pairs = max(pair) + 1`); `read_wait.py`'s `VICTIMS = 5` becomes
`5 + pots` where `pots` is 0 when no `SHELL_POT_CFG` line is in the block.

## 7. Setting the pots, and the two position gates

The pots have no centre mark and do not need one: on a linear pot, a reading
of half scale **is** the 50/50 wiper position, which is where the source
impedance peaks at R_track/4.

**Setting them:** flash the coupon bring-up image
(`SHELL_COUPON_PROBE=1`, no other probe), which rescans every channel live
(`shell/coupon_scan.cpp`), and turn RV2, RV4 and RV6 until each reads about
32768. The pots hang on their three solder joints with no glue
(`order-bom.md`): turn gently, and do not touch the board again until both
measuring images have run.

**Derived per run:** x = reading / 65535 and
R_src = R_track · x · (1 − x) + 150 Ω, with R_track nominal (§3).

**PG1 — position.** Each pot's reading must lie in **[28180, 37355]** counts,
inclusive — defined in counts, symmetric about 32767.5 (±4587.5), which is
x ≈ 0.43…0.57. At the edges x(1 − x) = 0.2451, 98.0 % of its 0.25
peak, so every admitted position is within 2 % of the peak impedance. The
reading PG1 uses: in the settle image the mean of the pot's two pairs'
settled references (P6/P7, P8/P9, P10/P11); in the wait image arm L's
`W = 0` mean. PG1 runs in `read_settle.py` and `read_wait.py`, each on its
own image, and a failure exits 1 and names the pot.

**PG2 — not moved between the images.** For each pot,
|settle reading − wait reading| (the two PG1 readings) must be **≤ 1024
counts**. Why that bound: 1024 counts is Δx = 0.0156, which moves
x(1 − x) by at most 0.1 % around mid travel — no impedance change this round
could resolve — while a pot knocked in handling moves thousands of counts.
The two readings come from different conditions (a short rung after a 20 µs
park against the long rung back to back), and how far those two conditions
alone move a reading has **not been measured** for any channel. So
`read_pots.py` prints the same difference for `REF_A` and `REF_C` from the
same two images, beside each pot's: that is the instrument's own share, and
if it approaches 1024 the gate is uninformative and the write-up says so
instead of reading PG2. PG2 needs both images' outputs, so it lives in the
joining reader (§8).

A run that fails PG1 or PG2 is read for what failed and not for its numbers.

## 8. The joining reader

`shell/read_pots.py` takes the files the two existing readers write
(`settle.csv.meta.csv` from `read_settle.py`; `wait.csv` and, new in this
round, `wait.csv.meta.csv` from `read_wait.py` — the wait reader gains the
same `scope,pair,key,value` metadata file the settle reader already writes,
because `wait.csv` alone carries no gate verdict) and prints the round's
answer in one place. Where a value it needs is not yet
in those files, the plan adds the column to the reader that owns it rather
than having `read_pots.py` re-parse a capture:

- per pot: derived x and R_src in each image; PG1 both; PG2;
- the settle table: true settle per pot pair, the model's prediction, the
  ratio, and RV4's P8/P9 beside `REF_A`'s P1/P2 from the same boot;
- the wait table: per pot, arm A's shift at 2 ms and at saturation (mean of
  10/20/50 ms), arm B's residue at saturation, and arm L's largest |shift|
  across all waits, beside `REF_A`, `REF_C` and `REF_B` from the same block.

It exits 1 when PG2 fails or either input's own gates failed. It computes
nothing the two readers already compute; it joins and compares.

## 9. How this can go red

Host tests, each proved red once before it is trusted (CLAUDE.md: a test that
cannot go red gets fixed):

| test | what it pins | the sabotage that must turn it red |
|---|---|---|
| `pot_plan` vs `coupon_expect` | each pot's `hi` neighbour is `Expect::High` and `lo` is `Expect::Low` on its mux; the pot channel itself is `Unchecked` | swap one pot's `hi` and `lo` |
| settle plan size | 6 pairs at switch 0, 12 at switch 1; P0–P5 identical at both | reorder P6 before P5 |
| G3 scope | G3 reads P0–P5 only: a synthetic run with a wide P8 band still passes G3 | let G3 iterate all pairs |
| wait estimate | 8 victims give 8/5 of the 5-victim estimate | hard-code 5 in the estimate |
| `read_wait` back-compat | the committed `wait-block-e22a628.txt` parses as 5 victims and `read_wait_guard` pins stay | set `VICTIMS = 8` unconditionally |
| PG1 | 28180 and 37355 pass; 28179 and 37356 fail | widen the window by one count |
| PG2 | 1024 passes, 1025 fails | use `<` instead of `≤` |

The firmware side's check is the build proof (§2) and the board run itself:
`SHELL_POT_CFG` must appear in both captures, P6–P11 and victims 5–7 must be
complete, and the existing gates must pass.

## 10. The board session

Nothing below has been run. Firmware in a shell **without** `env.sh`
(CLAUDE.md), host readers in any shell. `COM5` stands for whichever port the
board enumerates on.

1. **Set the pots.**
   ```bash
   PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1
   ```
   Flash (`dfu-util -a 0 -s 0x90040000:leave -D shell/build/shell-sram.bin`),
   read the live scan, turn RV2/RV4/RV6 to ~32768.
2. **Settle image, two complete blocks.**
   ```bash
   PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_SETTLE_PROBE=1 SHELL_POT_ROUND=1
   ```
   Flash, then `python shell/read_settle.py COM5 settle.csv 300`. The
   explicit 300 s: the reader's default is 60 s, and a twelve-pair block runs
   roughly twice a six-pair one — how long that is has not been measured, and
   a host that connects mid-block waits for the next one.
3. **Wait image, two complete blocks** (one per sweep direction).
   ```bash
   PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_WAIT_PROBE=1 SHELL_POT_ROUND=1
   ```
   Flash, then `python shell/read_wait.py COM5 wait.csv`.
4. **Join:** `python shell/read_pots.py settle.csv.meta.csv wait.csv`.

Between steps 1 and 4 the board is not touched; only the DFU button and the
USB cable are. Estimated time at the board: 15–20 minutes, most of it the two
wait blocks at ~143 s each.

Order of reading: each image's own gates, then PG1, then PG2, then the tables.
The first complete block of each image is vendored under `shell/testdata/`
so the guards parse a real pot-round block.

## 11. What the result would license

- **Settle:** if the pot pairs land inside 1.3–1.5× the model, 5b is closed:
  the model plus the measured factor is the settle constant for the scan
  budget (`docs/hardware/io-budget.md`), for both pot values and both chips.
  If RV4 departs from `REF_A` by more than the two `REF_A` pairs differ from
  each other, a real pot is not a divider, and the budget has to be done from
  pot pairs rather than divider pairs.
- **Wait:** if arm L holds every pot within ±3 counts, the long rung is
  licensed for pots as round three licensed it for dividers — still not a
  verdict on libDaisy's free-running DMA, which is a different pattern
  (`wait-measured.md` §9). If it does not, the rung is not the fix for pots,
  and that outranks everything else this round prints.

## 12. Out of scope

- Round one's four `RV4` row-6 crosstalk cases. Reachable today with
  `SHELL_XTALK_RV4=1`; their own board run.
- Any position other than mid travel. At a stop the source impedance is
  near zero and the ties already cover it.
- Any mechanism, a second board, a second submodule.
- The scan-budget arithmetic for the long rung (63 µs of acquisition per
  conversion times the panel's channels). It needs this round's arm L answer
  first.
