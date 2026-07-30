# DTCM instrument A/B

**Date:** 2026-07-30  
**Status:** approved by the owner  
**Predecessor:** `docs/superpowers/specs/2026-07-30-part-per-sample-design.md`

## 1. Question

The accepted hardware gate is `instrument_worst_bbd`. At `a1b1b7a` it reads
104.79 % average and 108.70 % maximum. Four voices per deck are a product
constraint and are not reduced.

The STM32H750 provides 128 KiB DTCM for CPU-local data. The current `ablate`
map uses only about 1.5 KiB of that region, while `g_system_arena` is 58,264
bytes and its largest member is the `InstrumentGroup` used by the gate.

This round asks one question:

> How much does the exact gate cost when only the `Instrument` moves from
> cached AXI SRAM to DTCM?

It does not move the bench counter, output arrays, echo buffers, sample
buffers, reverb storage, audio buffers, code, or any engine algorithm. Keeping
the output arrays in AXI is load-bearing: shipping audio buffers are DMA-owned
and cannot be placed in CPU-local DTCM.

## 2. Same-binary experiment

Add `instrument_worst_bbd_dtcm` to the existing `system` family. The original
`instrument_worst_bbd` remains the AXI control.

Both rows:

- are linked into one firmware image;
- call the same setup/configuration helpers;
- call the same `proc_inst` function through the workload table;
- use the same input, external FX memory, warm-up, retrigger cadence, voice
  guard, and checksum path;
- differ only in the address of the live `Instrument`.

The active `Instrument*` is selected during setup through one ordinary-BSS
pointer. Both rows use the same AXI-resident harness. The measured callback
therefore contains no row-specific branch, and only the `Instrument` address
differs.

This makes the subtraction an intra-run comparison. Code-layout drift between
builds cannot masquerade as a DTCM saving.

## 3. DTCM lifetime

`.dtcmram_bss` is `NOLOAD` and is not cleared by startup. A C++ object must not
be assumed to start life there after a debug reset.

The DTCM allocation is therefore raw, aligned storage. The DTCM row begins the
`Instrument` lifetime explicitly with placement `new` during every setup.
`Instrument` is not trivially destructible, so switching rows first destroys
the live object, matching `SerialArena`'s lifecycle. A second ordinary-BSS
pointer tracks the DTCM lifetime. Both pointers are cleared by startup; after a
debug reset the retained DTCM bytes are therefore treated only as raw storage
and are overwritten by placement `new`, never read as an old object.

The linker remains the hard capacity gate. The build must prove that the DTCM
symbol lies in `0x20000000..0x2001ffff` and that DTCMRAM usage remains below
131,072 bytes.

## 4. Evidence gates

The host protocol gains the new row. Hardware evidence is rejected unless:

1. every profile carrying `system` reports both A/B rows exactly once;
2. both rows return numeric results;
3. their checksums are equal within each run;
4. each row's checksum is stable across the repeated runs;
5. QSPI digest, device identity, and profile identity remain valid under the
   existing controller rules.

Both rows are also run through anchor mode so the comparison is repeated in a
real audio callback with cache and DMA contention present.

## 5. Decision rule

For every run compute:

```
average saving = AXI pct_avg - DTCM pct_avg
maximum saving = AXI pct_max - DTCM pct_max
```

- Keep DTCM if both metrics save at least 0.50 points in both runs, both
  directions agree, and the run-to-run spread of each saving is at most 0.25
  points.
- Revert DTCM if either metric regresses, either saving is below 0.50 points,
  checksums differ, or the measured effect is not stable.
- If the retained DTCM gate is still at or above 100 %, proceed to a separate
  ITCM design and measurement round.
- If it is below 100 %, stop. Compiler and reverb compromises are no longer
  justified by the CPU gate.

The 0.50-point floor is deliberately material: this round is not allowed to
keep a memory-layout change on a sub-resolution claim.

## 6. Global constraints

- Work on a branch, never directly on `main`.
- Do not change `engine/` behavior or any voice count.
- Do not rename, remove, or change the original gate row.
- Never run `bench/run.py` without an explicit `--profile`.
- Build, bind the QSPI receipt, then measure, in that order.
- Hardware evidence requires a clean Git tree and two runs.
- Commit trailer is exactly
  `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`,
  with nothing after it.
