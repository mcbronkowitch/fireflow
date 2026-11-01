# SD use cases on Rev A: board measurements

Rev A #1, branch `claude/sd-firmware-5a16f8`. Spec:
[`2026-10-31-sd-use-cases-tests-design.md`](../superpowers/specs/2026-10-31-sd-use-cases-tests-design.md),
plan [`2026-10-31-sd-use-cases-tests.md`](../superpowers/plans/2026-10-31-sd-use-cases-tests.md).
The board printed every number below. The captures are not in the repo. Like the 11.2
captures, they sit in the moderating session's notes folder as
`sd-captures-2026-11-01/reva-sd-2026-11-01-*.txt`.

## 1. Setup, board session 1 (plan Task 7), 2026-11-01

- **Board and card.** Rev A #1 on USB, with C_SD2 not fitted. The card is FAT32, 64 GB (`total_mib=61039`), and its root holds no `*.bin`.
- **Patch.** Bastian's patch persisted in QSPI, `SHELL_PERSIST src=flash seq=581`. The value read back unchanged at the end, on main's listener.
- **Remote.** The panel was hands off and nobody listened; the Twin was not used.
- **Images.** All pairs were flashed QSPI first, then SRAM, from clean builds of committed trees. The common flags are `SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1 SHELL_REMOTE=1 SHELL_RAW_SPREAD=1 SHELL_SD=1`.
  - Burst image: `acb06b1`, `SHELL_SD_PROBE=burst`.
  - Stream image: `1a73231`, `SHELL_SD_PROBE=stream`, with three A3 arms.
- **Start and end.** The board was on main's listener `f0fed59` (`SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1 SHELL_REMOTE=1`) and went back to it after the session.
- **Boot card test.** It printed the same on every image: write 8 MiB at 7 910 KiB/s (worst `f_write` 15.9 ms) and verify at 10 919 KiB/s (worst `f_read` 5.8 ms), both `fr=0`, `verify=ok`.

## 2. P1: no firmware file in the card root

- **Root line.** `SHELL_SD root=4 bin=0 names=SYSTEM~1,AUTORUN.INF,AUTORUN.ICO,DAISY_~1.TXT` on the burst and stream images.
- **Test file.** The card test now names its file `FFSDTEST.DAT` and deletes the old `FFSDTEST.BIN`, so the Daisy bootloader finds no `*.bin` to flash at boot.
- **LED at boot (SOS or normal).** Not observed. Bastian was not asked to watch a boot in this session.

**Pass**, on the root line. The LED is unconfirmed.

## 3. A1: bursts with a pot hold

- **Bursts.** One verified 32 KB read every `per` ms:
  - `per=83`: 384 KB/s, float stereo;
  - `per=41`: 768 KB/s, two streams.
- **Hold.** Pot readings taken during a transfer are dropped, together with the tail of N blocks after it (N = 0, 1, 2).
- **Phases.** Burst and idle phases interleave.
- **Captures.** Two of 5 min each on Bastian's patch (`a1-run1`, `a1-run2`). `read_sd.py` gives, per sense pin, the worst / median of the kept readings' one-second raw spread across the pin's pots (rows), against the pin's band. The rows add up to 73 pots.

| class | run | held | longest hold | S0 (64) | S1 (32) | S2 (32) | S3 (32) | pots over band | over 144 |
|---|---|---|---|---|---|---|---|---|---|
| idle | 1 | 0 % | 0 | 50/37 | 14/11 | 17/12 | 18/12 | 0 | 0 |
| idle | 2 | 0 % | 0 | 55/34 | 16/11 | 16/12 | 18/13 | 0 | 0 |
| 768 KB/s, N=0 | 1 | 10.1 % | 3 | 48/30 | 14/9 | 14/9 | 14/10 | 0 | 0 |
| 768 KB/s, N=0 | 2 | 10.2 % | 3 | 42/26.5 | 13/9 | 12/8 | 14/10 | 0 | 0 |
| 768 KB/s, N=1 | 1 | 14.8 % | 4 | 40/27 | 13/9 | 12/9 | 14/10 | 0 | 0 |
| 768 KB/s, N=1 | 2 | 14.3 % | 4 | 40/28 | 12/9 | 13/9 | 18/11 | 0 | 0 |
| 768 KB/s, N=2 | 1 | 20.1 % | 5 | 47/25.5 | 14/8 | 14/9 | 19/10 | 0 | 0 |
| 768 KB/s, N=2 | 2 | 19.1 % | 5 | 50/23.5 | 14/9 | 13/9.5 | 16/10 | 0 | 0 |
| 384 KB/s, N=0 | 1 | 4.7 % | 3 | 43/31.5 | 15/8.5 | 11/9 | 15/10 | 0 | 0 |
| 384 KB/s, N=0 | 2 | 5.3 % | 3 | 46/28 | 16/10 | 16/9 | 14/12 | 0 | 0 |
| 384 KB/s, N=1 | 1 | 7.4 % | 4 | 43/29.5 | 13/9 | 12/9.5 | 14/11 | 0 | 0 |
| 384 KB/s, N=1 | 2 | 7.3 % | 4 | 42/30.5 | 14/9 | 13/8 | 14/11 | 0 | 0 |
| 384 KB/s, N=2 | 1 | 9.5 % | 5 | 40/31.5 | 13/9 | 16/10 | 16/10 | 0 | 0 |
| 384 KB/s, N=2 | 2 | 9.8 % | 5 | 48/28 | 13/8 | 14/9 | 13/10 | 0 | 0 |

Throughput and worst read:
- **Throughput.** A transfer runs at 12.6-14.9 MiB/s.
- **Worst `f_read`.** 3.3-4.5 ms.
- **Errors.** No bad chunks, `fr=0` throughout.

**Result: tail 0 passes**, in both runs and at both rates.
- **Kept readings.** With the hold alone (no tail), the kept readings stay at the idle floor. No pot goes over its band, and none has a worst spread at or over `kPotTouch` (144). The streaming in fault R5 put all 73 pots over their bands.
- **Cost.** The hold costs 4.7-5.3 % of the blocks at 384 KB/s and about 10 % at 768 KB/s. The longest hold is 3 blocks.
- **Why it works.** The noise in R5 lives inside the transfers. A reading taken one block after a transfer is already clean.
- **For the tasks after this.** C, B and D go on with burst transfers and a hold with no tail, per the spec. Task 8's constants (`kTailBurst`, `kTailHoldTail`, `kTailBurstMs`, in `shell/sd_probe.cpp`) are set from this row before the C1/C2 board session.

## 4. A3: SD traffic against the callback

- **Stream mode.** Phases of ten one-second raw windows alternate idle and streaming. A streaming phase reads the 8 MiB test file back to back, at 10-12.5 MiB/s, far above any player.
- **Arms.** The on-phases rotate three arms:
  - `sdram16`: the SDRAM buffer, 16 KB reads;
  - `axi16`: an AXI SRAM buffer, 16 KB reads;
  - `sdram32`: the SDRAM buffer, 32 KB reads, the 11.2 pattern.
- **Contrasts.** Location (`axi16 - sdram16`) differs only in where the IDMA writes. Chunk size (`sdram32 - sdram16`) differs only in read size.
- **Pot hold.** The stream mode has none, so the pots are as in R5.
- **Runs.** Each workload ran 360 s via `remote_run.py run --load <id>`, giving about 6 on-phases per arm. The rotation order is fixed (sdram16 first), so the arm is mildly tied to time since the run began.
- **Callback measure.** A `SHELL_PLAY_WIN` frame is half a second. The tables give the mean of the frames' mean block (`sum / n`) in cycles, and the first frame after every class change is dropped.

`read_sd.py` gives the following. The Δ columns are the arm's mean minus the idle mean, in cycles. The verdict asks for |Δ| ≤ 9 600 (1 % of the 960 k-cycle block) and no frame's worst block over 945 000.

| workload | idle mean | Δ sdram16 | Δ axi16 | Δ sdram32 | location | chunk | max win (class) | verdict |
|---|---|---|---|---|---|---|---|---|
| 145 `bastian` | 685 405 | +2 455 | −6 042 | +19 982 | −8 497 | +17 527 | 954 478 (sdram16) | FAIL |
| 90 | 721 120 | +6 753 | +406 | +13 257 | −6 347 | +6 504 | 921 272 (sdram32) | FAIL |
| 99 | 581 704 | −5 968 | +8 937 | +16 322 | +14 905 | +22 290 | 880 074 (idle) | FAIL |
| 127 | 795 019 | +3 503 | +7 581 | +6 981 | +4 078 | +3 478 | 906 587 (idle) | PASS |
| 108 | 772 568 | +5 905 | +5 662 | +6 088 | −243 | +184 | 854 471 (axi16) | PASS |

The reader's per-arm p99 is the same as its max here. With about 80 frames, the nearest-rank p99 is the largest frame.

**How noisy these means are.** For each on-phase I took its mean minus the mean of the idle phases on either side, then the mean, sd and standard error of those differences per arm. This is a scratch script; it is not part of `read_sd.py`.

| workload | sdram16 | axi16 | sdram32 |
|---|---|---|---|
| 145 | −3 701 ± 17 438 (sd 46 k) | −10 094 ± 17 942 | +13 373 ± 12 137 |
| 90 | +8 787 ± 3 126 | +95 ± 3 874 | +12 858 ± 6 709 |
| 99 | +1 273 ± 4 520 | +12 062 ± 5 812 | +8 295 ± 5 761 |
| 127 | +1 292 ± 302 | +9 210 ± 627 | +8 261 ± 1 410 |
| 108 | +6 472 ± 1 407 | +4 959 ± 1 773 | +5 285 ± 946 |

Reading:
- **Size of the effect.** With 16 KB reads, streaming flat out stays within ±9 k cycles per block of idle on every workload (≤ 1 %, reader Δ). With 32 KB reads it reaches +13 to +20 k (1.4-2.1 %) on 145, 90 and 99.
- **Bastian's patch (145).** Its phase-to-phase spread (sd 30-46 k) is far larger than any arm difference, so its FAIL (sdram32 +20 k) is inside its own noise.
- **Location.** It has no consistent sign: AXI is cheaper on 145 and 90, more expensive on 99 and 127 (+7.9 k on 127, where the noise is ±0.7 k), and equal on 108. Moving the transfer buffer out of SDRAM does not reliably lower the cost. So the cost is not SDRAM bandwidth that the sampler, echo and BBD lose to the IDMA. What else it is was not measured.
- **Chunk size.** The chunk contrast is positive on all five workloads: +17.5, +6.5, +22.3, +3.5 and +0.2 k. That is the one consistent signal in A3. The per-phase noise resolves it only on 127 and 108, at +7 k and ±0. Why larger reads cost more was not measured.
- **Real players.** A player streams at 384-768 KB/s with bursts (A1), about 1/15-1/30 of this load. If the cost scales with the bytes moved, it falls to a few hundred cycles. That is an extrapolation, not a measurement.
- **Click line.** One frame crossed it: 954 478 in an sdram16 phase on 145. The idle max on the same run is 761 633, and on other workloads the idle max reaches 906 587. A single worst block moves ±135 k between runs (`docs/gotchas.md`), so this one frame does not decide anything.
- **Pots.** Unheld streaming put every pot over its band on every workload (S2 worst 540-588 counts). That is R5 again, as expected for this mode.

**Result.** The reader's verdict is FAIL on 145, 90 and 99, as printed, all from sdram32. A miss here prices the traffic, as the spec says; it does not stop the round.
- `sdram16` and `axi16` stay within ±9 k cycles of idle on every workload (≤ 1 %). In the per-phase check, axi16 on 99 is +12.1 ± 5.8 k.
- 32 KB chunks cost more than 16 KB on every workload, by up to +22 k, so 16 KB is the chunk size to keep.
- Location (SDRAM against AXI SRAM) shows no consistent sign.
- The FAIL on 145 is inside that run's own phase-to-phase noise. On 90 and 99 the noise does not explain the sdram32 excess.

## 5. Setup, board session 2 (plan Task 10), 2026-11-01

- **Board, card, patch, remote.** As in §1. `SHELL_PERSIST src=flash seq=581` on every boot of every image.
- **Images.** Pairs flashed QSPI first, from clean builds of committed trees, each after a `remote_run.py boot --expect-build`.
  - C1: `de5faea`, `SHELL_SD_PROBE=rtail` (the §1 flags). C2: `de5faea`, `SHELL_SD_PROBE=wtail`.
  - B1: `de5faea`, `SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1 SHELL_REMOTE=1 SHELL_RGRAIN=1`, no SD code.
  - B2: `f39d228` (B1's slope in `kRgrainCycles`), the same flags with `SHELL_RGRAIN=2`.
- **Hold.** C1 and C2 run A1's bursts with the hold and tail 0 (§3); C2's `wmax` runs back to back and is unmarked.
- **End.** See §10.

## 6. C1: read latency tail and seeks

- **Mode.** `rtail` writes `FFSDBIG.DAT` once (512 MiB in 72.8 s, `fr=0`), then reads 32 KB chunks in A1's burst pattern at 384 KB/s: sequential (`seq`), random offsets with FatFs fast seek off (`rand`), and random offsets with fast seek on (`randfs`), 10 min each.
- **Contiguity.** `clmt at=pre frags=1` and `at=randfs frags=1` in both runs: the file is one fragment.
- **Captures.** Two runs of 34 min (`c1-run1`, `c1-run2`). A first run-2 attempt was discarded: the controller left out the `boot` step, so `flash` found no DFU device and the capture holds only lines after the probe had finished.

`read_sd.py`, per pattern. Buckets are powers of two × 64 µs; "≥ 8 ms" counts the calls of 8 192 µs and longer.

| pattern | run | calls | worst `f_read` | p99.9 | ≥ 8 ms | worst `f_lseek` | ring at 384 KB/s |
|---|---|---|---|---|---|---|---|
| seq | 1 | 15 257 | 15 810 µs | 4 096 µs | 1 | 0 | 32 KB |
| seq | 2 | 15 341 | 24 459 µs | 4 096 µs | 1 | 0 | 32 KB |
| rand | 1 | 13 607 | 5 584 µs | 4 096 µs | 0 | 115 671 µs | 32 KB |
| rand | 2 | 12 481 | 14 003 µs | 8 192 µs | 1 | 126 465 µs | 32 KB |
| randfs | 1 | 15 176 | 4 003 µs | 4 003 µs | 0 | 1 144 µs | 32 KB |
| randfs | 2 | 15 231 | 4 016 µs | 4 016 µs | 0 | 1 385 µs | 32 KB |

No guard trip in either run, no `fr` other than 0.

**Result.**
- **Agreement within 2×.** `seq` 1.55× and `randfs` 1.00× pass. `rand` fails at 2.51× (5.6 against 14.0 ms), as printed. Its seeks agree (1.09×).
- **Ring size.** 2 × 24.5 ms × 384 KB/s is 18.8 KB, so a 32 KB ring covers a sequential stream on its own.
- **Seeks.** Without fast seek a random seek in this file walks the FAT chain and takes up to 126 ms, more than a 32 KB ring holds (83 ms at 384 KB/s). With fast seek it stays at 1.4 ms. A player that jumps in a large file needs fast seek, or a ring sized for the walk.

## 7. C2: write latency tail

- **Mode.** `wtail` writes 32 KB chunks to a new file in A1's burst pattern at the rates of 16-bit (`w16`, 192 KB/s) and 24-bit (`w24`, 288 KB/s) stereo recording, then back to back (`wmax`), 10 min each. The reader sizes every ring at 288 KB/s, the higher rate.
- **Captures.** Two runs of 31 min (`c2-run1`, `c2-run2`).

| pattern | run | calls | worst `f_write` | p99.9 | ≥ 64 ms | ring at 288 KB/s |
|---|---|---|---|---|---|---|
| w16 | 1 | 3 852 | 305 917 µs | 131 072 µs | 8 | 192 KB |
| w16 | 2 | 3 832 | 1 969 428 µs | 65 536 µs | 3 | 1 152 KB |
| w24 | 1 | 5 689 | 88 743 µs | 88 743 µs | 10 | 64 KB |
| w24 | 2 | 5 693 | 80 783 µs | 80 783 µs | 9 | 64 KB |
| wmax | 1 | 104 200 | 178 705 µs | 131 072 µs | 216 | 128 KB |
| wmax | 2 | 102 749 | 183 960 µs | 131 072 µs | 199 | 128 KB |

No guard trip in either run, no `fr` other than 0.

**Result.**
- **Agreement within 2×.** `w24` 0.91× and `wmax` 1.03× pass. `w16` fails at 6.4×: run 2 holds one write of 1.97 s, run 1's worst is 0.31 s. Run 2's next-longest call is in the 131-262 ms bucket, so the 2 s stall is a single event.
- **Writes stall far longer than reads.** p99.9 lies at 65-131 ms for writes, at 4-8 ms for reads. These are card-internal pauses that the 8 MiB bring-up write did not reach.
- **Ring size.** The 2 s stall sets it: 2 × 1.97 s × 288 KB/s is 1 134 KB, so 1 152 KB. A card's garbage-collection stall does not depend on the sample format, so it can hit 24-bit writes too; `w24`'s own 64 KB is not enough to plan with.
- **For D3.** The duplex probe (Task 12) serves both rings from one foreground thread, so a write stall also stops the reads. Both its rings are 1 152 KB, and the read ring is topped up whenever a 16 KB chunk fits, so it holds about 3.0 s at 384 KB/s when a write stalls.

## 8. B1: the price of a reverb grain

- **Mode.** `SHELL_RGRAIN=1`: N fixed grains (0, 8, 2, 4, 4, 2, 8, 0 per 20 frames) of `spky::Grain` into the reverb input, on the listener image without SD code.
- **Runs.** 240 s each via `remote_run.py run --load <id>`, on 145, 90, 99, 127 and 108. `read_rgrain.py` fits one slope per workload against the frames' mean block and leaves out every frame from the first overrun-guard trip on.

| workload | slope (first / second half) | mean at N = 0 / 2 / 4 / 8 | worst at N = 8 | room to 945 k (mean / p99 / worst) | verdict |
|---|---|---|---|---|---|
| 145 `bastian` | 16 584 (16 815 / 16 353) | 764 955 / 813 569 / 844 523 / 901 307 | 967 594 | 10 / 7 / 7 | PASS |
| 108 | 16 065 (16 005 / 16 124) | 766 515 / 800 255 / 832 473 / 895 381 | 952 944 | 11 / 8 / 8 | PASS |
| 90 | voided (trips 25) | 737 383 / – / – / 837 835 (8 frames) | 983 546 | 16 / 3 / 3 (from 42 frames) | not fitted |
| 99 | voided (trips 35) | 808 498 / – / – / – | – | – | FAIL (one N) |
| 127 | voided (trips 11) | – | – | – | FAIL (nothing counted) |

**Why three runs are voided.** `rgrain_n()` counts frames since boot, not since the load, so a run starts wherever the sequence stands. 90 and 127 started in an N = 8 phase and tripped the overrun guard within seconds (`by=period`). 99 tripped `by=cb` in an N = 0 phase after 23 s, without grains. After a trip `remote_run.py` voids the run and the reader counts nothing more.

**Result.**
- **Slope.** 16 584 and 16 065 cycles per grain on the two counted workloads, halves within 2.8 % and 0.7 %: PASS. `kRgrainCycles` is their median rounded up to 100, **16 400** (commit `f39d2286`). The round-2 estimate was 17 900.
- **Room.** On 145 and 108, 7-8 grains fit under 945 k at the worst block, 10-11 at the mean. On 90, 99 and 127, eight grains do not fit: the guard trips.
- **Caveat on placement.** The layer's grains and buffer object sit in DTCM, its code in SRAM, while the sampler's engine runs from ITCM and AXI SRAM. The Task 9 review put that at about 1 % cheaper on the slope. SamplerEngine's own per-grain overhead (its scheduling, its voice loop) is not in this price.

## 9. B2: a load-dependent grain cap

- **Mode.** `SHELL_RGRAIN=2`: per block, `rgrain_grant()` grants floor((920 000 − previous block's cycles) / 16 400) grains, at most 8. The cap is off and on in alternating 60-frame phases, off first.
- **Runs.** 1 200 s each via `remote_run.py run --load <id>`, on 90, 99, 127, 117 and 145. None was voided.
- **Measure.** `read_rgrain.py` per workload and phase: frames, worst `win`, mean of the frames' mean block, guard trips, and the per-frame mean grant. Each capture also holds one frame of the previous load (the frame counter runs across loads); the reader's verdict then names that workload too. The table judges each workload from its own frames only.
- **Pass.** Worst with the cap ≤ worst without grains + 16 400, and trips with the cap ≤ trips without.

| workload | off: worst / mean | cap: worst / mean | cap − off worst | trips off / cap | mean grant per frame (frames) | largest swing | verdict |
|---|---|---|---|---|---|---|---|
| 90 | 933 765 / 830 126 | 972 079 / 905 279 | +38 314 | 0 / 0 | 2:11 3:231 4:177 5:73 6:46 7:74 8:48 | 8 | FAIL |
| 99 | 915 421 / 840 541 | 966 401 / 911 795 | +50 980 | 0 / 0 | 4:513 5:148 | 5 | FAIL |
| 127 | 933 977 / 807 690 | 1 044 425 / 911 780 | +110 448 | 0 / 0 | 6:612 7:86 | 7 | FAIL |
| 117 | 940 744 / 814 291 | 1 021 371 / 911 770 | +80 627 | 0 / 0 | 5:6 6:655 7:9 | 8 | FAIL |
| 145 `bastian` | 826 551 / 776 565 | 945 957 / 905 301 | +119 406 | 0 / 0 | 6:5 7:35 8:652 | 8 | FAIL |

Swing is the largest change of the grant from one block to the next within a frame. Most frames swing by 2-4 grains on 90, 99, 127 and 117, and by 1 on 145.

**Result: FAIL on all five workloads**, as printed.
- **What the cap does.** It fills every workload to the same mean, 905-912 k, whatever the workload costs without grains. So the worst block with the cap is that level plus the block-to-block spike, not the no-grain worst plus one grain. The spec's criterion assumed the second.
- **Late blocks.** On 127 and 117 the worst block with the cap (1 044 k, 1 021 k) runs past the 960 k block. Without grains their worst is 934-941 k.
- **No trips.** The guard did not trip once in 10 runs. With a fixed 8 grains (B1) it tripped on 90, 99 and 127.
- **The known risk is real.** A grant from the previous block cannot see this block's spike (spec, B2). With a 920 k budget the spikes reach 1 044 k. A budget low enough to leave room for them, or a grant from more than one block, was not measured.

## 10. End of board session 2

- **Listener check.** Main's listener `f0fed59` was flashed after B2: `SHELL_PERSIST src=flash seq=581`, `chg=0`.
- **Image left on the board.** The top session's play image with a boot-only USB listener, `4f50d99` (`SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1`, `SHELL_BOOTCMD=1`). Its five checks:
  1. `remote_run.py flash … --expect-build 4f50d99` confirmed the stamp.
  2. Boot printed `SHELL_PERSIST boot ex=0,0 eng=1,1 take=1` and `src=flash seq=581`. `cb_last` read 500-731 k (mean 609 k over 49 lines). That is below the 790-850 k the top session expected, and matches main's listener in the same minute (723-729 k).
  3. `PING` answered `SHELL_REMOTE pong`.
  4. `remote_run.py boot --expect-build 4f50d99` reached DFU with no hands on the board.
  5. The same pair flashed again, and step 1 confirmed again, with `seq=581` and `take=1`.
- **Next flash.** It needs no hands: `remote_run.py boot --expect-build 4f50d99`.
