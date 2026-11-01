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
- **Size of the effect.** Streaming flat out costs the callback about 0-13 k cycles per block on average, 0-1.4 % of the block. That sits at the spec's 1 % line, above it on some workload/arm pairs and below on others.
- **Bastian's patch (145).** Its phase-to-phase spread (sd 30-46 k) is far larger than any arm difference, so its FAIL (sdram32 +20 k) is inside its own noise.
- **Location.** It has no consistent sign: AXI is cheaper on 145 and 90, more expensive on 99 and 127 (+7.9 k on 127, where the noise is ±0.7 k), and equal on 108. Moving the transfer buffer out of SDRAM does not reliably lower the cost. So the cost is not SDRAM bandwidth that the sampler, echo and BBD lose to the IDMA. What else it is was not measured.
- **Chunk size.** 32 KB is never cheaper than 16 KB, by 0-22 k depending on the workload. Only 127 and 108 resolve it, at +7 k and ±0.
- **Real players.** A player streams at 384-768 KB/s with bursts (A1), about 1/15-1/30 of this load. If the cost scales with the bytes moved, it falls to a few hundred cycles. That is an extrapolation, not a measurement.
- **Click line.** One frame crossed it: 954 478 in an sdram16 phase on 145. The idle max on the same run is 761 633, and on other workloads the idle max reaches 906 587. A single worst block moves ±135 k between runs (`docs/gotchas.md`), so this one frame does not decide anything.
- **Pots.** Unheld streaming put every pot over its band on every workload (S2 worst 540-588 counts). That is R5 again, as expected for this mode.

**Result.** The reader's verdict is FAIL on 3 of 5 workloads. Read against the per-phase noise, the traffic costs at most about 1 % of the block on average while streaming flat out. The cost does not depend on whether the IDMA writes into SDRAM or AXI SRAM. A miss here prices the traffic, as the spec says; it does not stop the round.
