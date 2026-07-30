# O3/LTO CPU experiment

## 1. Purpose

The retained DTCM state and ITCM audio hotset reduced the accepted BBD gate to
98.65 % average and 102.64--102.71 % maximum offline. The accepted real
callback is 98.81--98.82 % average and 102.78--102.79 % maximum. Four voices
per deck, the DSP expressions, parameters, sample rate, block size, and buffers
remain fixed.

This round asks:

> Can a whole-program compiler optimization reduce the remaining maximum below
> 100 % without changing any benchmark checksum or invalidating the retained
> DTCM/ITCM placements?

That was the pre-registered question. The final selection uses the
human-approved checksum-policy amendment in section 5: a deterministic
cross-mode output change is reported, but it is not an automatic rejection
when changed sound is explicitly accepted.

The experiment evaluates global `-O3` first. Link-time optimization is
evaluated only if `-O3` remains over budget and can preserve a provable ITCM
layout.

## 2. Compared variants

All viable variants are built from one committed source state with the retained
DTCM instrument storage and ITCM audio hotset:

1. `o2`: current `-O2` control.
2. `o3`: global `-O3` for the benchmark and engine sources.
3. `o3-lto`: global `-O3 -flto`, considered only if `o3` does not pass the CPU
   budget.

The optimization is global rather than being added to hand-picked functions.
This gives an honest result for the shipping compiler recipe and avoids
maintaining a fragile list of compiler attributes.

The prebuilt platform libraries are not rebuilt in this experiment. The
comparison changes the optimization of the application and engine sources
that are already built by the Spotykach makefiles.

The root and benchmark makefiles currently contain
`C_USR_FLAGS = -ffast-math -funroll-loops`, but libDaisy consumes the
differently named `C_USER_FLAGS` and `CPP_USER_FLAGS`. A dry-run compile
confirms that neither intended flag reaches the compiler today. The `o2`
control therefore means the actual retained `-O2` build, without those two
dormant flags. This round must not activate them accidentally; doing so would
mix three compiler changes into the `-O3` comparison.

## 3. LTO safety boundary

The supplementary ITCM linker script currently selects code by input object
name. LTO may replace those inputs with generated link-time objects and thereby
make the selection ineffective.

`o3-lto` is therefore not eligible for hardware measurement until static
evidence proves all of the following:

- the same representative audio symbols still execute from
  `0x00000100..0x0000ffff`;
- the linked ITCM footprint, including the reserved 256-byte prefix, remains
  within 65,536 bytes;
- `g_dtcm_instrument_storage` remains in DTCM;
- the ELF contains a loadable ITCM segment;
- compile and link records show `-O3 -flto`.

If ordinary LTO breaks the object-based placement, the variant is rejected for
this round. It must not be measured as though it retained ITCM. A future
LTO-safe placement mechanism would be a separate design, not an implicit
change to this experiment.

## 4. Fail-closed optimization identity

Every `BENCH_BEGIN` record gains an optimization identity:

- `o2`
- `o3`
- `o3-lto`

The build generates this value and makes it a real compilation dependency so
switching modes cannot silently reuse stale object files. The host controller:

- accepts only the three known values;
- requires every repeated capture to report the same value;
- requires the reported value to match the requested command-line mode;
- retains the existing AXI/ITCM layout check independently;
- records layout and optimization mode in filenames, CSV data, and Markdown
  evidence.

Any old header, mixed capture, stale binary, or requested/reported mismatch is
rejected before evidence is written.

## 5. Correctness and static gates

The `o2` control and every candidate must retain:

- four voices per deck;
- the accepted DTCM instrument state;
- the accepted ITCM audio hotset;
- identical benchmark inputs, sample rate, block size, and repeat count;
- all existing overflow and timing checks.

The original pre-registration required all 16 row checksums in each of two
hardware runs to match the `o2` control, including
`instrument_worst_bbd_dtcm=483e8e82`. The measured O3 result triggered an
explicit human decision to accept changed sound. The superseding final policy
therefore requires deterministic checksums within each mode and full reporting
of every cross-mode mismatch, but a deterministic cross-mode mismatch is not
an automatic rejection. Hashes do not establish inaudibility, sound quality,
or perceptual equivalence.

Before hardware use, each candidate must pass the existing controller suite
and linker-contract checks. New checks also prove the requested optimization
flags, ITCM symbol range and capacity, DTCM placement, load segment, and
fail-closed identity.

An optimization variant that does not link, overflows ITCM, loses the retained
memory layout, or has nondeterministic within-mode checksums is rejected
without being credited for speed. Deterministic cross-mode checksum changes
are retained for audit and evaluated under the amended human decision above.

## 6. Hardware comparison

For each viable mode:

1. Build the exact committed source.
2. Bind the QSPI receipt to that exact ELF.
3. Verify a clean worktree.
4. Collect two full hardware runs.

The `o2` control is rebuilt and remeasured in the same implementation commit;
the previous ITCM evidence remains a sanity anchor rather than serving as the
formal control. `o3` is measured next. `o3-lto` is measured only if `o3` still
exceeds the budget and passes every static LTO gate. In the executed round, O3
cleared the CPU budget but initially failed the now-superseded exact
cross-mode checksum gate, so O3+LTO reached its static gate; it failed there
and was never run on hardware.

Each candidate run is compared both with its paired `o2` run and with the
slower of the two `o2` controls. This prevents normal run-to-run variation from
being reported as a compiler gain.

## 7. Decision rule

A candidate is eligible only if every static placement gate and the
within-mode checksum-determinism gate pass. Cross-mode checksum differences
remain selection evidence under the section 5 amendment.

- Retain a candidate only if both of its runs save at least 0.50 CPU points in
  both average and maximum against both `o2` runs.
- If more than one candidate passes, retain the fastest correct candidate.
- Stop the CPU ladder only if both offline runs and the real callback are below
  100 % in both average and maximum.
- If the fastest correct candidate still exceeds 100 %, proceed to the agreed
  half-rate-reverb round.
- If no candidate clears the 0.50-point threshold, retain `o2` and proceed to
  half-rate reverb.

The threshold keeps a global compiler change only when its gain is larger than
measurement noise and meaningful against the remaining 2.7-point maximum.

## 8. Product boundary

The benchmark establishes the cycle effect using the same engine sources as
the product. Once a winner is accepted, the production makefile adopts the
same optimization recipe and must pass its normal build, size, and link-map
checks.

The accepted recipe is expressed through the compiler's existing `OPT`
interface. It does not rename or activate the dormant `C_USR_FLAGS` setting;
that would require its own bit-exact performance experiment.

This round does not add the firmware-shell ITCM loader. The earlier ITCM design
still requires the later shell integration either to load the ITCM section
directly or copy it before use. Until that integration exists, hardware timing
evidence continues to come from the SRAM benchmark.

## 9. Evidence and constraints

Accepted evidence is stored under `docs/bench/` with source commit, layout, and
optimization mode in its identity. The final record includes:

- all two-run averages, maxima, checksums, and pairwise deltas;
- the selected or rejected status of each mode;
- linked ITCM and DTCM placement evidence;
- the production build result for an accepted compiler recipe.

Work remains on `codex/perf-o3-lto`, never directly on `main`. No DSP
expression, parameter, voice count, buffer size, or sample rate changes in
this round.

Commit trailer is exactly
`Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`,
with nothing after it.

## 10. Measured result and production decision

All percentages below are points of the 960,000-cycle block budget. O2 and O3
each have two complete hardware captures; O3+LTO has no hardware rows because
its mandatory static gate failed.

| Mode | Run | Offline avg/max | Real callback avg/max | Gate checksum |
|---|---:|---:|---:|---|
| O2 | 1 | 98.65 / 102.66 | 98.81 / 102.74 | `483e8e82` |
| O2 | 2 | 98.65 / 102.71 | 98.81 / 102.75 | `483e8e82` |
| O3 | 1 | 95.44 / 99.46 | 95.63 / 99.52 | `3ad2d267` |
| O3 | 2 | 95.45 / 99.51 | 95.63 / 99.54 | `3ad2d267` |
| O3+LTO | 1--2 | not measured | not measured | not measured |

Every O3 run was compared with every O2 control. The four average/maximum
savings were 3.21/3.20, 3.21/3.25, 3.20/3.15, and 3.20/3.20 points. The strict
minimum saving is therefore **3.20 average CPU points and 3.15 maximum CPU
points**. O3+LTO has no CPU deltas because it did not reach hardware.

O2 and O3 each had 16 deterministic workload checksums with zero within-mode
drift. Twelve checksums match across modes and these four differ:

```text
fx_flux_sdram=5b9094c3->28a535f0
instrument_worst=4c4a29ce->5f0f7b2a
instrument_worst_bbd=483e8e82->3ad2d267
instrument_worst_bbd_dtcm=483e8e82->3ad2d267
```

The O2 control confirms the required historical gate value
`instrument_worst_bbd_dtcm=483e8e82`. The human owner explicitly accepts a
changed sound for O3, so these deterministic cross-mode changes are preserved
but do not reject O3. No claim of inaudibility or perceptual equivalence is
made from the hashes.

Static placement inspection produced:

| Mode | `.itcm_audio_hot` | ITCM load/symbol verdict | DTCM instrument storage |
|---|---|---|---|
| O2 | `0xa400` bytes at VMA/LMA `0x00000100` | `LOAD` at `0x00000100`; all ten representative symbols in ITCM | `0x200005c8`, size `0xc280` |
| O3 | `0xd8e0` bytes at VMA/LMA `0x00000100` | `LOAD` at `0x00000100`; all ten representative symbols in ITCM | `0x200005c8`, size `0xc280` |
| O3+LTO | empty (`0x0`) at VMA/LMA `0x00000100` | no ITCM `LOAD`; seven symbols missing and three in AXI SRAM | `0x20000528`, size `0xc280` |

The mode decisions are:

- **O2 rejected:** stable and statically valid, but both offline maxima
  (102.66/102.71 %) and callback maxima (102.74/102.75 %) exceed 100 %.
- **O3 selected:** statically valid, deterministic within mode, clears the
  strict 0.50-point savings gate, and both offline and callback runs remain
  below 100 % in average and maximum.
- **O3+LTO rejected before hardware:** the empty ITCM hot section, missing ITCM
  load segment, and representative-symbol failures invalidate the retained
  layout. There is no LTO capture or QSPI receipt, and LTO is not part of the
  production recipe.

The production build uses root `OPT = -O3`. Its actual 45 compile commands all
contained `-O3`; no compile or link command contained `-flto`, and the dormant
`C_USR_FLAGS` values did not reach the compiler. `build/spotykach.elf` linked
successfully with `build/spotykach.map` and no region overflow. The largest
reported usages were SRAM_EXEC 217,512/262,880 bytes (82.74 %), SDRAM
52,288/65,536 KiB (79.79 %), RAM_D2 24,828/32,768 bytes (75.77 %), SRAM
101,484/261,408 bytes (38.82 %), and DTCMRAM 1,564/131,072 bytes (1.19 %).
The production shell still has no ITCM loader, so its map reports zero ITCM
usage as expected by the product boundary in section 8.

Archived evidence:

- O2: [Markdown](../../bench/2026-07-30-1aa74ee-system-itcm-hot-o2.md),
  [CSV](../../bench/2026-07-30-1aa74ee-system-itcm-hot-o2.csv), and
  [exact-ELF QSPI receipt](../../bench/2026-07-30-1aa74ee-system-itcm-hot-o2-qspi-verified.json).
- O3: [Markdown](../../bench/2026-07-30-1aa74ee-system-itcm-hot-o3.md),
  [CSV](../../bench/2026-07-30-1aa74ee-system-itcm-hot-o3.csv), and
  [exact-ELF QSPI receipt](../../bench/2026-07-30-1aa74ee-system-itcm-hot-o3-qspi-verified.json).
- O3+LTO: [tracked static-rejection receipt](../../bench/2026-07-30-1aa74ee-system-itcm-hot-o3-lto-static-rejection.md);
  no hardware capture or QSPI receipt exists.

O3 satisfies the stop gate. The CPU ladder ends here; half-rate reverb is not
the next implementation step.
