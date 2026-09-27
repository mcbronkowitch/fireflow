# The wait — measured on the coupon

> **This document is the bench half of round three.** It records what the
> test coupon said on **2026-09-27**, on one board, in one session, two
> complete blocks, and nothing else. Its question is the one round two left
> open: a settled 5150 Ω channel reads up to 852 counts low as a function of
> how long the converter waited between conversions — what does that curve
> look like, and does either of the two cheap fixes pay it off?
>
> **The answer, in one line:** the shift grows over one to five milliseconds
> and saturates at about **−860 counts on `REF_A`** by 10 ms; the
> **387.5-cycle rung removes it completely**, at every wait; **one discarded
> conversion removes most of it but not all** — 30–41 counts stay behind at
> 5150 Ω.
>
> **Every number here is either measured or derived, and each one says
> which.** No mechanism is asserted anywhere in this document. Its
> predecessor is [`codec-tone-measured.md`](codec-tone-measured.md), whose §6
> found the shift and whose §9 asked for this instrument.
>
> The instrument is `shell/wait_probe.cpp`, built behind `SHELL_WAIT_PROBE`,
> spec [`2026-09-27-coupon-wait-sweep-probe-design.md`](../superpowers/specs/2026-09-27-coupon-wait-sweep-probe-design.md).
> The image that produced every figure below is commit **`e22a628`**, and the
> capture carries that stamp on every `SHELL_WAIT_CFG` line.

## 1. What the instrument is

Round one's five victims, four arms. Every repeat is: one conversion (the
prime, discarded), an idle of `W` from the prime's end with interrupts
enabled, [arm B only: one more conversion, discarded], then the measured
conversion. 64 repeats per point. `W` runs 0, 2, 5, 10, 20, 50, 100, 200,
500 µs, 1, 2, 5, 10, 20, 50 ms.

| arm | codec | rung | what it asks |
|---|---|---|---|
| **A** wait | stopped | victim's working rung (2.5 cycles at 5150 Ω, 1.5 below) | the curve |
| **B** discard | stopped | working rung | does one extra conversion pay it off |
| **L** long | stopped | 387.5 cycles | does a long acquisition window pay it off |
| **C** codec | running, zeros | working rung, `W` = 0 / 0.2 / 1 / 10 ms | the bridge to round two |

The reported quantity is the **shift**, `mean(W) − mean(W = 0)` within one
arm, one victim, one block — so a rung bias cancels inside each arm and never
enters a comparison between arms. Derived by `shell/read_wait.py`.

## 2. The run, and its gates

**Measured**, both blocks (`SHELL_WAIT_CFG`/`_CAL`/`_GATES`/`_HEALTH`):

| | block 1 | block 2 |
|---|---:|---:|
| `sweep_dir` | 1 (descending) | 0 (ascending) |
| `adc_khz` | 6146 | 6146 |
| `lat_min…lat_max` ns | 673…806 | 673…806 |
| `b0` (G2, bound 0…64) | 22 | 3 |
| `gates_ok`, G9 | 1, PASS | 1, PASS |
| `missed_blocks`, `timeouts` | 0, 0 | 0, 0 |
| `block_ms` | 89 644 | 89 646 |

`adc_khz = 6146` is the fourth image in a row to measure the same clock.
Jitter is 133 ns against G4's 200. The spec **estimated** ~90 s per block
(`wait_block_estimate_ms()`, 89.7 s); the board **measured** 89.6 s.

**What was not measured.** No second board, no second submodule, no pots.
The host connected mid-way through the boot's first block, so the capture's
first 243 lines are a partial block and are not in any table here; the one
place it is used is §6. That partial block's own `block_ms` was **112 568**,
23 s longer than the two complete blocks — recorded, not explained.

## 3. Arm A: the curve

Shift in counts, block 1 / block 2:

| `W` | `REF_A` 5150 Ω | `REF_C` 5150 Ω | `REF_B` 650 Ω | ties 150 Ω |
|---|---:|---:|---:|---:|
| 2 µs | +7 / +4 | −3 / −2 | +2 / +1 | 0 |
| 10 µs | −4 / −9 | −21 / −22 | −10 / −10 | 0 |
| 100 µs | −23 / −29 | −37 / −37 | −6 / −9 | 0 |
| 200 µs | −49 / −57 | −56 / −59 | −9 / −9 | 0 |
| 500 µs | −123 / −134 | −113 / −119 | −11 / −11 | 0 |
| 1 ms | −239 / −257 | −202 / −213 | −9 / −12 | 0 |
| 2 ms | −440 / −471 | −359 / −375 | −13 / −13 | 0 |
| 5 ms | −804 / −828 | −637 / −654 | −18 / −18 | 0 (one +1) |
| 10 ms | −856 / −861 | −678 / −679 | −17 / −20 | 0 |
| 20 ms | −855 / −858 | −677 / −680 | −16 / −20 | 0 |
| 50 ms | −855 / −862 | −679 / −678 | −16 / −19 | 0 |

**The curve saturates.** From 10 ms to 50 ms `REF_A` holds within 3 counts
in each block, `REF_C` within 3. The saturated values — about −858 and −679 —
are round two's 100 Hz numbers (−852 and −671, `codec-tone-measured.md` §6)
to within 1.2 %. *Derived:* half of `REF_A`'s saturated value is reached
between 1 and 2 ms in both blocks.

**It is ordered by source impedance, as round two found:** the 650 Ω divider
saturates near −18, the 150 Ω ties do not move. One tie point reads +1
(`R_SP10`, `W` = 5 ms, block 2) against 0 on its other 244 points;
`read_wait.py` now reports a tie only beyond ±1 for that reason.

**The two sweep directions agree** to within 31 counts at the steepest point
(2 ms) and within 7 counts at saturation, and the descending block reads
*less* negative throughout the rising part. A drift across the sweep is
therefore a small additive term, not the shape — the same conclusion
`settle-measured.md` §6 reached for its own curves.

**Characterised, not explained: a step between 5 and 10 µs.** All three
dividers move 10–16 counts down between those two points in both blocks
(`REF_A` +7/+5 → −4/−9, `REF_C` −5/−6 → −21/−22, `REF_B` 0/+1 → −10/−10).
In arm A only `REF_B` partly comes back at 20–50 µs; in arm B (§4) all three
read lowest at 10 µs and recover by 20 µs. Nothing here names it.

## 4. Arm B: one discarded conversion

| `W` | `REF_A` | `REF_C` | `REF_B` |
|---|---:|---:|---:|
| 10 µs | −22 / −20 | −26 / −25 | −16 / −18 |
| 200 µs | −15 / −16 | −19 / −22 | −11 / −8 |
| 1 ms | −31 / −19 | −22 / −24 | −12 / −8 |
| 5 ms | −33 / −38 | −28 / −31 | −13 / −13 |
| 10 ms | −41 / −41 | −28 / −32 | −12 / −11 |
| 50 ms | −39 / −41 | −30 / −30 | −11 / −10 |

**One discard pays off about 95 % of it, and the rest stays.** At
saturation `REF_A` goes from −858 to about −40, `REF_C` from −679 to about
−30. *Derived:* 40 counts is 2.5 LSB of 12 bit, five times the half-LSB
criterion every earlier round used. The residue is not noise — arm L's
flatness (§5) puts this instrument's floor at ±3. It is already −7…−26 below
20 µs, where arm A has barely begun, and then creeps from about −15 at
200 µs to about −40 at 10 ms: it does not have arm A's shape, and it is not
arm A scaled down.

This is consistent with `settle-measured.md` §7, where one discard collapsed
a first-arrival deficit of −866 to **−41** on the same victim. Two different
instruments, three weeks apart, the same residue. Stated as a
correspondence; no mechanism is attached.

## 5. Arm L: the 387.5-cycle rung

**Flat.** Every victim, every `W`, both blocks: the largest shift anywhere in
arm L is **3 counts** (`REF_B`, 5 ms, block 2). The long rung reads the same
value after a 50 ms wait as back to back.

That was one of the outcomes spec §10 named in advance, and it licenses
exactly what §10 said it would: a long acquisition window pays the effect off
at every wait. `settle-measured.md` §7 had already found that the long rung
lands on the divider's true value (`REF_A` 32761…32765 against an ideal
32767.5); this run adds that it does so independently of the wait.

## 6. Arm C, and the bridge to round two

| `W` | `REF_A` | `REF_C` | `REF_B` | round two, `REF_A`, four boots |
|---|---:|---:|---:|---|
| 200 µs | −58 / −59 | −62 / −66 | −9 / −9 | −69.2 / −69.8 / −75.2 / −92.1 |
| 1 ms | −261 / −283 | −217 / −236 | −14 / −14 | −283.4 / −280.1 / −307.9 / −370.7 |
| 10 ms | **−865 / −861** | −680 / −681 | −21 / −19 | −849.7 / −850.7 / −851.4 / −854.0 |

**G9 passes in both blocks** (−865, −861 against [−900, −800]). The new
instrument measures what round two measured: at 10 ms it lands 7–15 counts
beyond round two's four boots, at 1 ms at or just short of their range
(−261 and −283 against −280…−371), at 200 µs a little short of it. Round two's 1 kHz and 5 kHz values drifted 23–91 counts between
boots; this run's two blocks drift 1–22.

**The codec makes no difference that this run can separate from the
blocks.** Arm C against arm A at the same `W`, same block: `REF_A` −865 vs
−856 and −861 vs −861 at 10 ms, −261 vs −239 and −283 vs −257 at 1 ms. The
1 ms difference (22–26 counts, arm C deeper, both blocks) is the largest and
is smaller than arm A's own difference between its two blocks at 2 ms.
Recorded, not attributed.

**The "stopped" history does not show either.** Spec §3 flags that a boot's
first block runs arms A, B and L on a codec never started. The capture's
partial first block has three complete arm-A cases (`REF_B` and both ties):
`REF_B` reads −10 / −14 / −18 / −18 at 1 / 5 / 10 / 50 ms there, against
−12 / −18 / −20 / −19 in block 2. The two 5150 Ω victims' boot-block cases
are incomplete, so this is a check on one divider, not on the effect.

## 7. What this rests on

| claim | class |
|---|---|
| Every table in §3–§6 | **measured**, `SHELL_WAIT` lines, two blocks |
| Every shift | **derived**, `read_wait.py`, `mean(W) − mean(0)` within a case |
| Saturation by 10 ms | **measured**, 10/20/50 ms within 3 counts |
| "Half by 1–2 ms" | **derived**, from the 1 and 2 ms points |
| 2.5 LSB, 95 % | **derived** |
| That `W` is the commanded wait to within microseconds | **reasoned** — interrupts run during the wait and are not timed; spec §9 |
| That arm L's rung fixes the shipping firmware | **not claimed** — the shipping firmware runs libDaisy's free-running DMA across twelve channels, a different pattern (§9) |
| Any mechanism | **none** |

## 8. Open, characterised, deliberately unexplained

- **Arm B's 30–41 count residue.** Present from 2 µs on, growing slowly
  with `W`, impedance-ordered (~40 / ~30 / ~11 on `REF_A` / `REF_C` / `REF_B`), and the
  same size as `settle-measured.md` §7's one-discard figure.
- **The step between 5 and 10 µs** in arms A and B, 10–26 counts on all
  three dividers.
- **The boot block's 112.6 s.** 23 s longer than the next two blocks,
  printed and unexplained; a host not yet draining the port is the first thing
  to rule out.
- **Whether the saturated value is "the S&H arriving empty".** −858 here
  against −866 for a first arrival from an AGND tie (`settle-measured.md` §7)
  is close. It is a correspondence; nothing measured here tests it.

## 9. What it means for the design

**For any scan that reads a pot channel once per audio block — a ~2 ms wait
— the default rung reads a 5150 Ω pot about 450 counts low** (arm A, 2 ms),
i.e. 28 LSB of 12 bit, 0.7 % of full scale — on every read, on the same
side, for every pot at that impedance.

**The long rung removes it at every wait.** One discard reduces it to
~40 counts, which is still five times the half-LSB criterion. So between the
two cheap fixes, the evidence here favours the rung.

What this does **not** license: a verdict on the shipping firmware as it
stands. libDaisy's free-running DMA converts twelve channels back to back,
with no idle, each one following a *different* channel — `settle-measured.md`
§7's first-arrival question, measured there, with its own answer
(`SPEED_16CYCLES_5` pays off a 19-count deficit at pot impedance). Nor does it
say what a 387.5-cycle rung costs in a real scan: 63 µs of acquisition per
conversion, times every channel the panel reads. That budget is a panel
firmware question and has not been done.

## 10. How to repeat it

The capture is committed as
[`captures/wait-capture-e22a628.txt`](captures/wait-capture-e22a628.txt)
(797 lines: a partial boot block, then two complete ones at lines 244–520
and 521–797). Block 1 is vendored as `shell/testdata/wait-block-e22a628.txt`,
and `read_wait_guard` parses it and pins §3's `REF_A` saturation, §5's
flatness, §4's residue band and G9. From the repository root:

```bash
python -c "import sys; sys.path.insert(0, 'shell'); import read_wait as r; raise SystemExit(r.report(r.parse_block(open('shell/testdata/wait-block-e22a628.txt')), 'wait.csv'))"
```

It exits 0 and prints the four arm tables, the bridge and G9. On the board:
spec §11.
