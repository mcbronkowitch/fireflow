# bench

## What this is

A measurement app that has never shipped and never will. It boots the Daisy
Seed straight into a cycle-counting harness instead of the FireFlow firmware,
runs a fixed table of workloads, and reports cycle counts and checksums back
over the debug probe. The shipping firmware is the repo-root `Makefile`,
`main.cpp`, `app.cpp`/`app.h`, `src/**`, `third_party/**` and `engine/**` —
none of that is touched by anything under `bench/`. This directory has its
own `Makefile`, its own `main.cpp`, and links against `alt_sram.lds` only to
reuse the same BOOT_SRAM placement the real firmware uses, so the timing
context (SRAM vs SDRAM latency) matches production.

## Profiles, and one command per profile

The bench image cannot hold every workload family at once — at `d294556` it
linked with 40 bytes of `SRAM_EXEC` and 24 bytes of `SRAM` to spare, and the
next engine took it over. A profile selects which families an image
contains.

`bench/profiles.py` is the source of truth for what ships. On this branch:

| profile | families | purpose |
|---|---|---|
| `system` | system | carries the WAVE acceptance gate; fits comfortably |
| `body` | system, body | the M5j gate: prices the BODY mode bank and KS pair |
| `bbd` | bbd | the BBD gate, on its own; no system anchor (reports undetermined) |
| `sweep` | system, sweep | the cost-curve round; system anchor prevents undetermined verdict |
| `regress` | system, bbd | gate rows and BBD kernel rows in one image, for any gate-versus-BBD A/B (introduced by the 2026-08-04 signal-path round); **`axi` only — `--itcm-hot` cannot pass placement at any optimization level, see below** |
| `ablate` | system, instr | the instrument-level ablation; measures the worst-case instrument build |
| `full` (default) | system, voice, mem, mod, abl, bbd, body, sampler | the complete run, as before profiles existed |

**`regress --itcm-hot` cannot produce a usable image, at any optimization
level, and the failure is not yours.** `spky::BbdLine::Process` is a weak
symbol; under `regress` the linker keeps the copy in the bench-harness TU
`build/workloads_bbd.o`, which `itcm_hot.lds` does not list, so the surviving
symbol lands outside ITCM and `itcm_placement.py` fail-closes. At `-O3` a
second, independent failure sits on top: the hot section overflows the 64 KiB
region and does not link. Do **not** add `workloads_bbd.o` to the hotset —
that places bench-harness code in ITCM and distorts every measurement ever
taken there. Run `regress` with `axi` until the hotset definition is reworked
(M6 work). Measured in
`../docs/bench/2026-08-04-2101349-signal-path-regression.md`.

**`full` currently fails to link, and that is deliberate, not broken.** The
engine has outgrown the image; no profile change fixes that on its own (see
the history above), and the point of profiles is to make forward progress
possible without hiding the debt. Running `python run.py` with no
`--profile` resolves to `full` and fails at the link step with an
SRAM/SRAM_EXEC region overflow. For a build that actually links and
measures today, name a profile:

```
python run.py --profile system
```

From `bench/`. This builds the bench firmware for the named profile,
verifies that the separately programmed WAVE bank matches the current
linked QSPI payload, loads only the SRAM side of the image through the
debug probe, and captures its semihosting output **twice** (`--repeat 2` is
the default — see "Anchor mode's audio" below for what that means out
loud), compares the two runs' unique row sets and checksums, enforces every
 gate the profile declares on every run, and writes both complete captures
 to `../docs/bench/YYYY-MM-DD-<githash>-<profile>-<layout>-<optimization>.md`
 and `.csv`. The layout is `axi` by default or `itcm-hot` with `--itcm-hot`.
 The optimization identity is `o2` (the default, `-O2`), `o3` (`-O3`), or
 `o3-lto` (`-O3 -flto`), selected with `--optimization`. The CSV carries a
 run index, the profile name, the execution layout, optimization identity, the live QSPI
 payload digest, and a SHA-256 device fingerprint on every row; the Markdown records those
identities, a gate ledger (which gates ran and passed, and which were not
applicable and why), and both offline/anchor tables. Exit code 0 means at
least two runs passed every gate the profile declares and both were
persisted. A missing/extra/duplicate row, checksum drift, identity drift,
non-numeric WAVE result, WAVE result slower than SYNTH, or WAVE maximum at
or above the 960,000-cycle block budget exits nonzero and writes no
accepted evidence. Every profile-derived callback anchor must appear exactly
once with finite numeric values, and every decision-gate row must be numeric;
malformed/timeout gate data exits nonzero before CSV, Markdown, or a combined
program-and-run receipt is persisted. A valid over-budget DTCM+BBD result is
still archived as rejected evidence rather than being treated as malformed.

Useful flags: `--profile NAME` (default `full`; see the table above and
`bench/profiles.py` for what ships), `--repeat N` (default and minimum 2),
`--out-dir DIR` (default `../docs/bench`), `--build-only`, `--no-build`,
 `--timeout SECONDS` (default 600, per run), `--interface CFG` (see below),
 `--program-qspi`, `--itcm-hot`, and `--optimization {o2,o3,o3-lto}`.
 `--itcm-hot` links the pre-registered audio hotset into ITCM and makes
 `run.py` inspect the linked ELF before any QSPI helper or benchmark SRAM load.
 The preflight fails closed if `nm`, `objdump`, or `readelf` is unavailable,
 or if the hot section, load segment, representative symbols, or DTCM
 instrument storage are missing or misplaced. Use the flag consistently for
 build, QSPI binding, and measurement so the host can reject an AXI/ITCM
 mix-up.

`C_USR_FLAGS = -ffast-math -funroll-loops` remains dormant in the underlying
build and was deliberately not activated by the completed compiler-mode
selection. The benchmark reports its requested `o2`, `o3`, or `o3-lto`
identity; the measured winner is `o3`, the production root makefile now uses
`OPT = -O3`, and LTO remains rejected.

## Programming the WAVE bank

The 65,024-byte bank is a loadable `.qspiflash_data` section at
`0x90100000`. It sat at `0x90040000` until 2026-08-07, which is the Daisy
bootloader's own target address for a `BOOT_SRAM` app image — see
*Two transports* below for the map and why that address is off limits.
It is deliberately not part of the debug probe's SRAM load. The build creates
four distinct artifacts:

- `build/bench.elf`: authoritative linked image and map source;
- `build/bench-sram.elf`: `.qspiflash_data` removed; the ELF OpenOCD loads
  for measurement;
- `build/bench-qspi.bin`: only `.qspiflash_data`, exactly 65,024 bytes;
- `qspi_programmer/build/qspi-programmer.elf`: a temporary SRAM-only helper
  used solely to erase, write, compare, and hash that raw payload.

Do not use `make program-dfu` for this custom layout. libDaisy's generic rule
expects one flat `build/bench.bin` beginning at the BOOT_SRAM app address;
an image spanning SRAM and QSPI is not that format. `run.py` builds only the
ELF and extracts the two physical payloads explicitly.

Before the first WAVE run, or whenever `bench-qspi.bin` changes, run the
four steps below. Every command names `--profile system`: it is the only
profile that links today (see "Profiles, and one command per profile"
above — the WAVE bank itself, `engine/synth/wt_bank.cpp`, is compiled
unconditionally regardless of profile, so any linking profile would carry
the same 65,024 bytes, but `system` is also the profile that exercises the
WAVE acceptance gate this bank is for), and steps 1, 3, and 4 must agree on
it exactly: the identity check in step 4 compares that rebuild's
ELF/SRAM-ELF/QSPI/programmer-ELF hashes against the receipt step 3 wrote,
and a different profile produces a different ELF even when the QSPI bytes
are unchanged.

1. Build with `python run.py --profile system --build-only`.
2. Connect the ST-Link to the Seed's SWD header and power the Seed.
3. Run `python run.py --profile system --no-build --program-qspi --build-only`.
   Do not put the Seed into DFU mode. (`--no-build` means this step doesn't
   use `--profile` to rebuild anything — it just re-derives the QSPI payload
   from whatever `bench.elf` step 1 left behind — but passing `system` here
   too keeps the sequence consistent and avoids relying on that detail.)
4. Run `python run.py --profile system --repeat 2`.

For an ITCM-hotset experiment, append `--itcm-hot` to commands 1, 3, and 4.
Each command then performs the same mandatory linked-placement preflight
before programming or loading the target.
Do not reuse an AXI QSPI receipt for an ITCM run (or vice versa): the receipt
is bound to the exact ELF, and the firmware-reported `BENCH_BEGIN` layout is
checked before any evidence is accepted.

The programming command uses OpenOCD to load the helper below `0x24040000`
and the raw payload at `0x24040000`; it never programs internal flash. The
helper erases one 64 KiB external-QSPI block at offset `0x100000`, writes
exactly 65,024 bytes, invalidates the mapped cache, compares every byte at
`0x90100000`, and reports the live SHA-256 plus the MCU UID over semihosting.
Only an exact success record writes `build/qspi-verified.json`. The receipt
binds that target-verified digest and UID to `bench.elf`,
`bench-sram.elf`, `bench-qspi.bin`, and the helper ELF.
Even with `--no-build`, `run.py` re-inspects `bench.elf` and regenerates both
physical artifacts before accepting the receipt.

### The receipt breaks on any engine change, and step 1 is not optional

The heading above says "whenever `bench-qspi.bin` changes", which understates
it. The receipt binds the verified digest to the **ELF hashes**, so *any*
change under `engine/` invalidates it even though the bank's 65,024 bytes are
untouched. The failure looks like this:

```
ERROR: QSPI verification receipt does not match current payload (artifacts)
```

That message names the artifacts, not the bank, and it does not mean the QSPI
is corrupt or that a different Seed is attached — those have their own checks
(the live digest comparison and the MCU UID match). It means the binding is
stale. The fix is to re-run steps 1 and 3; nothing needs erasing.

**Do not skip step 1 and go straight to step 3.** With `--no-build`, step 3
binds to whatever `bench.elf` is lying on disk. If step 4 then relinks
anything — and it will, after an engine change, because step 4 rebuilds — the
hashes it computes differ from the ones step 3 wrote and the run aborts with
the message above, having already reprogrammed the bank for nothing. Build
first, bind second, measure third. Running it in the other order costs a full
programming cycle and reads like a hardware fault.

Every measurement also SHA-256 hashes the 65,024 live bytes through the
Seed's memory-mapped QSPI interface before `BENCH_BEGIN`; the host compares
 that firmware-reported digest with the extracted payload. `BENCH_BEGIN` also
 reports the Seed's 96-bit MCU UID, the `axi` or `itcm-hot` execution layout,
 and the requested optimization identity. The UID must strictly match the
 programming receipt, and every repeat must report the requested layout and
 optimization. Together these checks catch a different
 Seed, a later QSPI overwrite, blank/wrong QSPI, or a stale/mixed execution
 layout even if an old local receipt remains. Public CSV and Markdown evidence
 persist the payload digest and a stable SHA-256 fingerprint of that UID,
 not the raw device identifier. The exact audit receipt
 (`qspi-verified.json`) intentionally retains the raw UID because it is the
 device-binding record used to reject a different Seed. The tracked O2/O3
 receipts already follow that policy; their bytes and hashes are unchanged by
 this documentation correction. Hardware evidence is refused from a dirty Git
 tree.

Programming this address overwrites the leading bank region and therefore
invalidates whatever BOOT_SRAM/BOOT_QSPI application was previously stored
in the Daisy bootloader's app slot; remnants beyond the 65,024-byte write may
remain. The bench itself starts directly from SRAM through OpenOCD.

## Hardware setup

The measurement itself uses the debug probe's SWD cable: 4 wires (SWDIO,
SWCLK, GND, and 3V3, or just GND+SWD if the probe supplies its own power)
from the ST-Link to the Seed's SWD header. The Seed's micro-USB port is usable
as a power source but is not used for DFU or reporting. If anchor mode is
going to run (it always does, as part of the family-1 sweep), have
monitors connected to the Seed's audio out at **low volume** first — see
below for what you'll actually hear.

## The probe

This desk's probe is an ST-Link V3, and openocd needs `stlink-dap.cfg` to
get a real DAP out of it — that's `run.py`'s default and the value
confirmed working in Task 1. Plain `stlink.cfg` auto-selects the older
`hla_swd` transport, under which `spotykach-sram.cfg`'s
`transport select dapdirect_swd` line is an error, not a fallback path.

If a different probe is on the desk, the other candidates to try via
`--interface` are `cmsis-dap.cfg` (CMSIS-DAP probes) and `stlink.cfg`
(older ST-Link V2/V2-1 hardware, accepting the `hla_swd` limitation). Both
are untested against this bench.

## Reading the table

- **avg cyc / max cyc** — DWT cycle counter reading for the workload's
  `process()`, averaged and maxed over the repeated calls in one bench pass.
- **avg % / max %** — the same, as a percentage of `BUDGET_CYCLES` (960 000
  cycles: 480 MHz core clock, 96-sample block, 48 kHz). Over 100% means the
  workload alone would blow the audio block budget.
- **checksum** — an 8-hex-digit accumulator folded from every sample the
  workload produced. It exists so the compiler cannot optimise the workload
  away as dead code, and it is what the `--repeat 2` determinism check
  compares between runs. A workload whose output legitimately depends on
  uninitialized memory or wall-clock timing will show up here as drift.
- **TIMEOUT** — the row's `avg_cyc` field reads the literal string `TIMEOUT`
  (and `pct_avg`/`pct_max` are empty) when the workload ran past 10× the
  block budget; the runner aborts it rather than letting one bad workload
  hang the whole sweep. A `TIMEOUT` row still carries a `max_cyc` and a
  `checksum` from the point it was cut off.

## Fallbacks

**(a) If the SRAM load ever stops working**, use `--transport usb`, which
is that bootloader image path and now exists. Still do not reach for
libDaisy's generic `make program-dfu`: it programs one flat
`build/bench.bin` covering SRAM *and* QSPI, which is not this layout.
`run.py` writes `build/bench-sram.bin` — the ELF with `.qspiflash_data`
removed, flattened — and the bank is a separate write.

**(b) If semihosting itself is not available** — a board with no debug pins
soldered on, for instance — use the USB transport. It is no longer a
fallback sketch; it is built, measured, and described in *Two transports*
below. It was written here as untested for a long time; it is not untested
any more, and what it costs is known to the cycle.

## Two transports

`BENCH_TRANSPORT` picks how a number leaves the board. `semihost` is the
default and the transport every capture in `docs/bench/` before 2026-08-07
was taken over.

| | `semihost` | `usb` |
|---|---|---|
| Wire | SWD, ARM `bkpt 0xAB` serviced by openocd | USB-CDC |
| Loader | openocd writes the SRAM image directly | `dfu-util` via the Daisy bootloader |
| Needs | an ST-Link on the SWD header | the bootloader in internal flash, and the cable that already powers the board |
| Repeats | openocd resets between them | the bench returns to the bootloader itself |
| Costs | nothing | **+0.66 % of the block budget** |

Run it with `python run.py --profile regress --transport usb`. `--port`
defaults to `auto`, which takes whichever serial port appears after the
image is loaded — the board is in DFU while being written and only
enumerates as CDC once the new image runs, so it cannot be named in
advance.

**The first run of a session needs two button presses**: tap RESET, then
BOOT during the bootloader's 2-second window. After that the bench jumps
back with `DAISY_INFINITE_TIMEOUT` after `BENCH_END` and waits there, so
every following repeat is unattended. If the Daisy bootloader is not on the
board at all, install it first: hold BOOT, tap RESET, release, then
`make program-boot`.

### Numbers from the two transports are not interchangeable

USB-CDC costs **6,370 cycles per block, 0.66 % of the budget**, measured on
one Seed with everything else held constant. The cause is not the memory
map — `g_axi_layout_guard` holds every measurement object at an identical
address in both branches — it is the peripheral: the host sends a
start-of-frame every 1 ms, a block is 2 ms, and two interrupts land inside
every measured window.

The full comparison is `docs/bench/2026-08-07-transport-semihost-vs-usb.md`.
Compare like with like: a submodule capture belongs against a Seed capture
over the *same* transport, where the surcharge cancels. USB captures carry
a `-usb` suffix in their filename for exactly this reason.

### The QSPI map

| Region | From | To | What |
|---|---|---|---|
| Bootloader | `0x90000000` | `0x90040000` | four 64 KiB sectors, reserved |
| App image | `0x90040000` | `0x90100000` | where `dfu-util` writes a `BOOT_SRAM` image |
| WAVE bank | `0x90100000` | +65,024 B | `.qspiflash_data` |
| Free | | `0x90800000` | |

**Do not put anything back at `0x90040000`.** libDaisy's
`core/Makefile` sets `FLASH_ADDRESS = QSPI_ADDRESS` for `APP_TYPE =
BOOT_SRAM`, so that is the address the next `dfu-util` invocation
overwrites. The bank lived there until 2026-08-07 and it was never
noticeable, because with a probe attached openocd loads SRAM directly and
QSPI belongs to the bank alone. Without a probe the two collide. The
shipping firmware links the same `alt_sram.lds` and had the same defect.

The bank can be written over DFU (`dfu-util -a 0 -s 0x90100000 -D
build/bench-qspi.bin -d ,0483:df11`) on a board with no probe. It cannot be
read back: the Daisy bootloader answers a QSPI *upload* with a single 4 KiB
block repeated across the whole range, identical at every address, so an
upload is not weaker evidence than a probe readback — it is none. The
probe-free proof is the firmware's own: it hashes the bank in place and
reports the digest in `BENCH_BEGIN`, which `run.py` checks as line one
arrives.

## The one hard rule, and which transport it belongs to

**On `semihost`,** the bench binary requires an attached, running openocd
session. Its `report.cpp` talks to the host by executing `bkpt 0xAB` (the
semihosting breakpoint) and blocking for a response; without a debugger
serving that breakpoint, the very first one halts the core forever. From
the outside that looks exactly like a hang — no crash, no error, just a
Seed that never gets anywhere near `BENCH_END`. If a run seems stuck, check
that openocd is actually attached before assuming the firmware is broken.

**On `usb`,** the equivalent trap is silence. `StartLog(true)` returns when
the CDC endpoint is configured, which is not when the host has opened the
port and started reading, and everything written in between is lost. This
already happened once: a capture arrived with 28 rows, no `BENCH_BEGIN`,
and the protocol starting in the middle. `transport_open()` therefore
spends three seconds sending throwaway lines before anything that counts —
`run.py` ignores every line before `BENCH_BEGIN`, so they are free.

## What anchor mode's audio actually proves, and what it does not

Anchor mode attempts five named workloads and re-runs the subset present in
the selected profile inside a real audio callback. The `system` profile emits
four anchors: Oliverb, the DTCM+BBD decision gate, its AXI comparison, and
`instrument_worst`. The callback drives `CpuLoadMeter` for the anchored
percentages. The DAC
output during that segment is **not** the workload's audio — it is one
value per block (`process()`'s return, the same checksum accumulator used
everywhere else in this bench) held flat across all 96 samples of that
block. That produces a roughly 500 Hz staircase built out of accumulator
sums, not a rendering of the reverb tail or the synth voice underneath it.

Consequently **every workload sounds like the same harsh buzz**, and the
anchored segments cannot be told apart by ear. This is a
**non-silence detector**, nothing more: it distinguishes "this callback
computed something" from "the optimiser deleted this workload as dead
code." The checksum is the actual anti-dead-code guarantee — it is
non-zero, it is reproducible across the two repeat runs, and it is
data-dependent by construction. Do not read the monitor output as a
listening test, and do not promise anyone they will hear a reverb, a
synth voice, or anything resembling FireFlow's actual sound in this mode.

**Anchor mode's final segment sounds broken on purpose.** `instrument_worst`
runs at roughly 160% of the block budget offline, so inside the real
callback it cannot finish before the next block is due; the DAC ends up
fed underrun garbage for that segment. That is not a bug in the bench — it
is the offline number, confirmed by ear. The block-count limit built into
the anchor callback (it stops itself after a fixed number of blocks) is
what ends that segment; an earlier version of this harness used a
foreground delay loop to pace itself instead, and because the over-budget
workload starves the very thread that delay loop needed to run on, that
version did not stop on its own — it ran for minutes until killed by hand.

Since `--repeat 2` is the default, anchor mode runs **twice** per `run.py`
invocation — expect two bursts of this harsh buzz, not one, with the
broken-on-purpose segment inside each.

One more thing the bench does that is easy to miss: it stamps
`boot_info.version` into the STM32H750's battery-backed backup SRAM and
does not restore the previous value afterwards. That marks the board as
having a "v6.1 bootloader present" for any firmware load that checks that
flag later. This has been judged harmless — it doesn't change what the
bootloader does — but it was undocumented until now, so future debugging
of boot-related oddities on this specific board should know the bench is a
possible source.

## `instrument_worst` measures the reverb in SRAM

`Instrument::init` wires its reverb pointer through `fx_mem()`, and
`bench/mem.cpp`'s `fx_mem()` hands back `&g_rev_sram` unconditionally --
there is no SDRAM variant reachable from the instrument rows. So
`instrument_worst` (and `instrument_init`) pay the SRAM reverb's cost, not
the SDRAM one. The bench's own SRAM-vs-SDRAM measurement for the same
Oliverb (`oliverb_solo_sram` vs `oliverb_sdram`, family 3) puts that
difference at roughly 1.1x -- small next to the grain-read proxy's 5.3x,
but not zero. If a future production build moves the reverb buffer to
SDRAM, expect `instrument_worst`'s headline percentage to rise by roughly
two points, not to hold at the figure recorded here.

## Row order is state, not just presentation

The `abl` instrument rows share one `Instrument` (`g_abl_inst`) and each
`setup_worst()` re-inits it, which clears the FLUX tape (`DeLine::Init`
memsets), the reverb buffer (`FxEngine::Init` calls `Clear`) and the reverb
loop filters (reset in `Oliverb::Init`). Even so, **inserting
`inst_worst_nogrit` before `inst_worst_choked` changed the choked row's
checksum** (`995dbd34` → `cd90d415`) while its cost moved 0.001 %
(841 869 → 841 859 cycles). Some state still carries between these rows and
the three obvious candidates above are not it; the cause is unpinned.

Consequences: a checksum shift on a row you did not touch, after inserting a
row *before* it, is expected rather than alarming — but it means **abl
checksums are only comparable between runs whose table order matches**, and
a genuine determinism regression could hide behind the same signature. The
same trap is documented in `workloads_system.cpp` for `g_inst_ctr`. Append
new `abl` rows at the end of the table when you can.

## The sampler family, and the two things it changed

`workloads_sampler.cpp` prices the M5 texture deck. Two structural
consequences worth knowing before touching anything near it:

**1. The bench's 64 KB arena left the SRAM region.** `Part` embeds a
`SamplerEngine` (1 392 B), and the bench's globals hold four bare `Part`s and
two `Instrument`s, so the sampler merge pushed `.bss` 6 168 bytes past the
256 KB `SRAM` region and the bench stopped linking. `alt_sram.lds` gained a
`.sram_exec_bss` section and `bench/mem.h` a `BENCH_SRAM_EXEC_BSS` macro;
`g_sram` now lives there.

This is **not** a change of measurement conditions. `SRAM_EXEC` and `SRAM` are
the same physical AXI SRAM — one 512 KB block at `0x24000000`, split into a
code half and a data half by the `MEMORY` block, same bus, same latency, same
MPU cache attributes. `grain_read_sram` measures exactly what it measured
before. The section is additive and empty in the shipping firmware (nothing
under `src/**` emits into it), so no shipping symbol moved either.

What it *is*: a warning. `SRAM_EXEC` now sits at **93 %** and `SRAM` at 82 %.
The next thing that overflows will be code, not data, and there is no third
half to move it to.

**2. The sampler rows own the SDRAM arena.** They run **last** in `main.cpp`
because `SamplerEngine::load_sample` takes two `float*`, so the 8 MB
`sdram_arena()` doubles as the source material both channels are copied from
(L from the front, R from an offset view — the offset is what makes both
windows land inside it exactly). Family 3 is done with the arena by then, and
every sampler `setup()` refills what it is about to read.
Anything appended after the sampler family must not assume the arena still
holds what family 3 put there.

Also note `fx_mem()` now fills `sampler_buf[]` and `sampler_frames` for every
`Instrument` the bench builds, not just the sampler rows. This costs the synth
rows nothing in the measured window — `SampleBuffer::init` → `clear()` takes
the `_size == 0` fast path on a buffer nothing has written to, and an
unselected engine's `process()` is never called — so their numbers are
unchanged. Only `setup()` touches it.

**What the rows found.** `inst_sampler_worst` is the same box as
`instrument_worst` with both parts swapped to the sampler, and it is *cheaper
on the mean block and more expensive on the worst* — the peak clears 100 % of
the budget while the mean stays under it. Anyone reading the sampler rows for
a "does it fit" answer has to read the max column, not the avg column.

The first reading of that shape was "a spawn burst," and it was wrong. The
ablation rows below exist because that guess did not survive being measured:
`inst_sampler_nomotion` moved the peak by 0.3 points, and `_slowspawn` — 75x
fewer spawns — made the peak *worse*. What it actually was is grain-count
variance. The spawn interval carries MOTION's ±75 % jitter while the grain
length does not, so short intervals stack grains and the live count wanders
5..11 where DENS asked for 8; per-block cost is linear in that count. Counted
exactly rather than timed (`Grain::process` calls per block), the worst case
spread 1.36x with the jitter and 1.01x without.

`kSpawnHeadroom` (`sampler_config.h`) caps the live count at
`ceil(overlap) + kSpawnHeadroom`. At the shipping value of 2,
`sampler_flow_worst` reads 1.18x peak-to-mean instead of 1.33x, and
`sampler_worst_nomotion` — the row that settles the mechanism, and the reason
it has to be a SOLO row — reads 1.01x on hardware, matching the offline count
exactly. The constant also sets how far tape mode may stretch a grain, so its
value is an ear decision; the table is at the constant.

Two traps this left behind, both worth knowing before adding a row here:

- **A `_nomotion` row at the instrument level does not remove MOTION.**
  `Part::_active` defaults to all-true, so zeroing a target's base leaves its
  lane still swinging it. The first version of that row measured the
  unablated peak and read as "not MOTION" — the wrong answer, arrived at
  confidently. It now calls `set_target_active(..., false)`.
- **After the cap, a high peak-to-mean ratio means a low mean, not a high
  peak.** The ceiling makes the worst block constant (the ceiling's worth of
  grains); what the SIZE lane varies is the mean, by sweeping `grain_len`
  over a factor of ~300. `inst_sampler_slowspawn`'s 1.6x is that, not a
  burst.

One thing deliberately **not** a row: `SamplerEngine::init()` on a buffer
holding content ends in `clear()`, which memsets 16 MB of SDRAM. That is real
and it is expensive — far past 10x the block budget, so as a table row it
would only ever print `TIMEOUT`. It belongs nowhere near an audio thread, and
this paragraph is the record of it.

## Adding a workload

Add one row to the relevant `kXxxWorkloads[]` table (`workloads_system.cpp`
for family 1, `workloads_daisysp.cpp` for family 2, `workloads_memory.cpp`
for family 3, `workloads_mod.cpp` for the modulation plane, `workloads_abl.cpp`
for the ablation rows, `workloads_bbd.cpp` for the bucket-brigade delay,
`workloads_sampler.cpp` for the texture deck) with a
family tag, a name, a setup function and a process function. A new *table*
additionally needs its `extern` in `workload.h`, an entry in `runner.cpp`'s
`find_workload` arrays (and its loop bound), a loop in `main.cpp`, and the
source in the `Makefile` — five places, none of them automatic, because table
order is execution order and must not depend on link order. Workload **basenames must stay unique across the whole bench**,
not just within one file's table — libDaisy's Makefile flattens every
source path with `notdir` when it builds the object list, so two files
named e.g. `voice.cpp` in different directories would collide at link time
even though their paths differ.

## The bbd family, and why its rows are worst cases on purpose

`bbd_ceiling` runs `BbdEcho` with the clock pinned to `kClockMaxHz` and
`STAGES` at `kMaxStages` — the combination that maximises tick rate and cell
array size at once. It is a ceiling, not a typical patch, because the design's
whole CPU claim ("the taps pay for the BBD and nothing more") is only settled
by the worst case: cost here is driven by clock Hz, not by delay time, so a
typical setting would still leave the ceiling unmeasured. `bbd_line_only`
strips the compander and drive path to split the ceiling's cost between the
model and the surrounding device; `bbd_walk_sdram` isolates the line's
sequential-write memory access pattern.

## WAVE direct-engine gate

`system/synth_2x4` and `system/wave_2x4` are an intentionally matched
direct-engine pair. Each row owns two engines seeded `3` and `4`, initializes
them at 48 kHz, sets decay to `1`, cycle to 2 s, and FLOW false, and triggers
the exact pitches `{0.25, 0.35, 0.45, 0.55}` on both engines. One measured
call processes both engines for exactly the 96-sample block, accumulates all
four L/R outputs, and folds both active-voice counts into the checksum.

The 2026-07-24 Task 8 firmware gate now builds. The generated ARM-only
placement puts the 65,024-byte `kBankSamples` at `0x90040000`. Serial system
rows construct their measured objects in one max-sized lifetime arena;
construction of the next row destroys the previous group before reusing the
same aligned AXI storage. The 64 KiB `g_sram` measurement arena remains in
physical AXI SRAM. The linker split was moved 736 bytes inside that same
contiguous 512 KiB AXI block; no measured object changed bus, cache, or
latency class.

The accepted build-only map is deliberately recorded because the margins are
tight:

```text
DTCMRAM     8,604 / 131,072 bytes   6.56%
SRAM_EXEC 262,864 / 262,880 bytes  99.99%  (16 bytes free)
SRAM      261,384 / 261,408 bytes  99.99%  (24 bytes free)
QSPIFLASH  65,024 / 8,126,464 bytes 0.80%
```

`build/bench.elf`, `build/bench-sram.elf`, and the exact 65,024-byte
`build/bench-qspi.bin` are produced. The receipt-bound `build/bench.elf` used
for the final runs is 3,728,648 bytes. `g_sram` maps to `0x240302d0`,
`g_system_arena` to `0x240606c0`, and `kBankSamples` to `0x90040000`.

The first full hardware traversal exposed a bench-only failure after
`modal_voice`, while setting up `string_voice`. The fault was an imprecise
bus error (`CFSR=0x00000400`) in newlib `rand`: its first call lazily
allocated a 24-byte state object, then allocator metadata/state writes crossed
the AXI boundary at `0x24080000`. The linked image had `_ebss=end=0x2407ffe8`,
leaving exactly 24 bytes and no room for allocator overhead.

The harness therefore supplies its own deterministic, heap-free `rand` and
`srand`. Its five-byte logical state occupies eight aligned bytes in DTCM;
the linked `rand` symbol comes from `build/rand_shim.o`, whose undefined-symbol
set is empty. A bench-only NOLOAD layout reservation absorbs the removed
newlib text so all measured engine objects retain the accepted AXI addresses.
Production firmware and DaisySP are unchanged. Before this shim, the corrected
accepted map used 8,596 DTCM bytes; the eight-byte state accounts for the
8,604-byte figure above.

Because `.dtcmram_bss` is NOLOAD and the startup handler clears only the AXI
`_sbss.._ebss` range, `main` explicitly calls the shim's `srand(1)` after
hardware initialization and before any workload. This both overwrites retained
DTCM state on every debug reset and keeps the target `srand` entry point in the
linked image. The final symbols are `srand=0x240010f4` and
`rand=0x24001104`; disassembly shows the call from `main` before the first
`run_workload`.

The controller completed two full hardware runs on 2026-07-25 from commit
`8c5f2e1`. Both reached `BENCH_END`; the fail-closed controller accepted
identical unique 68-row sets and checksums; and both verified that the live
QSPI SHA-256 and MCU UID matched the programming receipt. The persisted
payload digest is
`ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`;
the stable, non-raw device fingerprint is
`1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.

| run | `synth_2x4` avg/max | `wave_2x4` avg/max |
|---|---:|---:|
| 1 | 340,347 / 346,106 | 308,497 / 312,180 |
| 2 | 340,342 / 346,105 | 308,503 / 311,962 |

In both runs WAVE is lower than Synth on both average and maximum cycles, and
both WAVE maxima are below the 960,000-cycle block budget. Task 8 therefore
passes its direct-engine acceptance gate. The generated
`docs/bench/2026-07-25-8c5f2e1.md` and `.csv` persist both complete runs,
including both anchor sets, the QSPI digest, and the hashed device fingerprint.

The `66d359d` attribution values came from minimal diagnostic binaries because
the full parent bench did not link. Those diagnostics retained the exact
parent engine code and the exact Task 8 workload setup/process: 96-sample
blocks, a 100-block warmup, then 1,000 blocks measured with DWT. Parent
`lane_step_shape00` measured 27,971 / 29,042 average/maximum cycles versus
28,040 / 29,050 in the current full bench, changes of +0.247% / +0.028%.
Parent `synth_1_voice` measured 52,967 / 53,880 versus 55,576 / 56,570,
changes of +4.925% / +4.993%. The displayed parent diagnostic average/maximum
cycle values—not a checksum claim—were exact repeats across two captures.
The current full captures differ slightly in cycles as recorded above, while
their checksum comparison reported no drift. Both parent/current changes are
within this bench's documented cross-build noise. The roughly 11,614-cycle
lane figure in an older report predates intervening lane and workload-shuffle
changes; it is not the `66d359d` diagnostic comparator.

The verdict is protected by the receipt-bound live-payload check, target UID
check, full-run terminator, repeat-run checksum comparison, heap-free random
shim, explicit reset-safe random seed, fixed QSPI bank placement, and unchanged
measured AXI object addresses. Production firmware and DaisySP remain
unchanged.
