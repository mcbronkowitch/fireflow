# Rev A P6b-1 — one control law for VCV and the firmware

**Date:** 2026-10-09
**Phase:** Rev A P6 (master plan `2026-09-28-rev-a-master-plan-design.md`, row P6).
P6a (`2026-10-02-rev-a-p6a-panel-scan-design.md`) announced one P6b spec; it is
split in two (Bastian, 2026-10-09). This is the first:

- **P6b-1 (this spec):** the control law leaves `Fireflow::pushParams()` for a
  Rack-free `control/` that VCV and the firmware both call; every pot reaches
  its target; mod depths boot at the init patch's values.
- **P6b-2 (next spec):** MOD on the hardware (latch, pot pickup, depth
  editing), SHIFT, REC, the LED law over the 595 chain.

**Inputs:** P6a spec §2 (why 35 of 70 pots had no target), the pushParams map
below (§2, read at `450eed36`), the shell build of 2026-10-09 (§5).

> **Amended 2026-10-09 while planning** (plan
> `docs/superpowers/plans/2026-10-09-rev-a-p6b1-shared-control-law.md`).
> Where this note and the sections below disagree, this note wins.
>
> 1. **`control/params.hpp` is emitted beside, not instead of,**
>    `generated_panel.hpp` and `init_patch.hpp`, in its own namespace `ffctl`.
>    `host/vcv/res/test_panel.py` reads those two files and `Fireflow.cpp` at
>    94 places; moving their content would rewrite that guard for no gain. One
>    generator writes both from one `PARAMS` list, and
>    `tests/test_control_params.cpp` holds the copies together (§3.1).
> 2. **REC stays in the law.** REC_A/B are knob-vector params, so the latch
>    moves with the rest and returns `Events::rec_started[p]`; VCV clears
>    `smp[p].path`/`factoryLoaded` on it. The factory autoload cannot follow
>    as an event: it sits between `set_excitation_sources` and the sampler
>    options inside the deck loop, so the law takes a second template
>    parameter `Hooks` with `after_engine(p, eng, inst)` called at exactly
>    that spot. The firmware passes `NoHooks` (§3.2, §4.1).
> 3. **QSPI code moves the bank.** `.qspiflash_text` is linked at
>    `0x90100000`, in front of the wavetable bank, so code there shifts the
>    bank: `shell-qspi.bin` then carries both and is flashed again whenever
>    its md5 changes. Whether the MPU allows execution there is the first
>    probe. The placement guard runs at link time inside `make images` (a
>    violating image cannot be built); its checker script is host-tested in
>    ctest (§5).
> 4. **The tick-rate probe runs after the extraction,** not before it: it
>    ticks the extracted law itself at 16 and 96 samples (§4.3).

## 1. Goal, done, and what stays out

**Goal.** At equal knob positions the Rev A board and `FireflowHW` in Rack run
the same control law, because they run the same code. P6a's accepted
divergence — every mod depth zero on the hardware — closes: the firmware boots
with the init patch's depths.

**Done means:**

1. `control/` exists; VCV's `pushParams()` is a thin adapter over it, and a
   Rack session shows no behaviour change (§7.3).
2. Every Rev A pot row carries a `control` parameter id except the three
   reserved ones; the coupon's three rows do too.
3. All three shell images build (default, Rev A, coupon), with the margins of
   §5 met and the QSPI placement guard green.
4. One coupon session: the QSPI-resident image boots and plays, the three pots
   act through the law, and `tick()`'s cycle cost is measured and the rule of
   §6 applied.

**Out of P6b-1:** editing depths, the MOD latch, SHIFT, REC, LEDs (all P6b-2);
a factory sample on the hardware — a Sampler deck stays silent until P6b-2's
REC records something (Bastian, 2026-10-09); CV, CLOCK and RESET on the
hardware; any parameter for `ROOT_A`, `ROOT_B`, `REV_MOD`; the render host,
which keeps calling `apply_param()`.

## 2. What pushParams is today (measured by reading, `450eed36`)

- `Fireflow` and `FireflowHW` are **one Module class**
  (`createModel<Fireflow, FireflowHWWidget>`, `Fireflow.cpp:2470`); only the
  widget differs. There is exactly one control law to extract.
- `pushParams()` (`Fireflow.cpp:765-1263`) runs from `process()` every 16
  samples (`ctrlDiv`, `:429`, `:1282`). Its blocks: A, the MOD frame snapshot
  (lane outputs); B, SHUFFLE; C, the per-deck loop (motion, voice, FX gates,
  LVL/COMP split, PAN, engine remap, sampler options, REC latch, engine-
  dependent re-pointing, BBD edge, GRIT, STEPS, SONG ladder); D, the
  engine-backed depth loop over `kModLayer`; E, centre (MORPH, COUPLE zone,
  DRIFT settle, TIDE, CHOKE, PULL); F, reverb and fixed constants, SCALE;
  G, tempo with the CLOCK override, PACE.
- Movable logic is about 180–210 code lines; the already Rack-free headers it
  uses (`mod_layer.hpp`, `bbd_edge_state.hpp`, `drift_settle_state.hpp`,
  `song_rung_state.hpp`) add about 90, and are unit-tested today.
- Rack-bound: the readers `pp()`/`mv()` (`params[]`, and
  `paramQuantities[]->getMin/MaxValue()` for the clamp), the factory autoload
  (`:941-949`), the sampler menu state `smp[p]` (`std::string`), the REC latch
  (`:962-974`), the BBD edge's write-back `params[FLUX].setValue(0)`
  (`:1026-1043`), the CLOCK input, the `dsp::` triggers and dividers.
- **No CV is merged with any knob.** MODBTN is never read by pushParams: depths
  always apply; the latch only swaps widgets.
- Parameter ranges live only in the constructor's `configParam` calls
  (`:441-532`), not in any generated table.

## 3. `control/` — the shared layer

A new top-level folder beside `engine/` (Bastian, 2026-10-09). No Rack type,
no hardware type, no file I/O.

### 3.1 `control/params.hpp` (generated)

Written by `host/vcv/res/gen_panel.py`, the generator that already owns the
ids. It carries:

- the 125 parameter ids (today's `ParamId` in `generated_panel.hpp`, which then
  includes this file instead of defining them);
- `kModLayer` (moved from `generated_panel.hpp`);
- `kInitParamDefaults` (moved from `init_patch.hpp`);
- **new: a range table** — `lo`, `hi` and a `snap` flag per id, the values the
  constructor passes to `configParam` today. The constructor then reads them
  from the table, so the range has one source.

### 3.2 `control/control_law.h` (+ `.cpp` for non-template helpers)

```cpp
namespace control {

struct DeckOptions {          // what menus feed today; the firmware passes {}
    // Defaults = a fresh SamplerPartState (host/vcv/src/sampler_ui.hpp:13-30)
    int   tape_idx   = 1;     // 1 = Tape
    bool  reverse    = false;
    float feedback   = 0.95f;
    bool  test_tone  = false;
    bool  excite_tape = true, excite_other_deck = false, excite_audio_in = false;
};

struct Options {
    DeckOptions deck[2];
    float measured_bpm = 0.f;  // the host's measured CLOCK rate; the law accepts it inside 20..400, else uses TEMPO
};

struct Events {               // what the law used to write back into Rack
    bool bbd_edge[2] = {};    // VCV: params[FLUX].setValue(0), exciteOtherDeck
};

template <class Inst>
class ControlLawT {
public:
    void   on_reset();        // re-arms song rung + drift settle (Fireflow.cpp:1328, :1335)
    void   on_restore();      // re-arms all three, bbd edge too (Fireflow.cpp:1517-1520)
    Events tick(const float* knobs, const Options& opt, Inst& inst);
};
using ControlLaw = ControlLawT<spky::Instrument>;

}
```

`knobs` holds all 125 values **in parameter units**, exactly what VCV's
`params[]` holds today. `tick()` is blocks A–G of §2 minus the Rack-bound
lines; `mv()` becomes a pure function of knob, depth, lane outputs and the
range from §3.1. The template exists for the tests (§7.1); both hosts
instantiate it with `spky::Instrument`, so the firmware pays no virtual call.

`DeckOptions` defaults must equal what a fresh VCV instance has in `smp[p]`;
a test pins them against `SamplerPartState`'s. The two re-arm entry points keep
today's split on purpose: `onReset` does not re-arm the BBD edge, a patch
restore does, and folding them into one call would change VCV.

### 3.3 Moved as-is

`mod_layer.hpp`, `bbd_edge_state.hpp`, `drift_settle_state.hpp`,
`song_rung_state.hpp` move to `control/`; their tests follow the include path.
`led_law.hpp` stays in `host/vcv/src` until P6b-2.

## 4. The hosts

### 4.1 VCV

`pushParams()` copies `params[]` into a `float[125]`, fills `Options` from
`smp[p]` and the CLOCK measurement, calls `tick()`, and applies `Events`
(the FLUX write-back and `exciteOtherDeck`, exactly as today). What stays in
VCV: factory autoload, the REC latch, CLOCK/RESET triggers, the LED call,
every widget. `onReset` calls `law.on_reset()`, the patch-restore path
`law.on_restore()`. The firmware calls `on_restore()` once at boot.

### 4.2 Firmware

- **Table.** `shell/gen_panel_map.py` emits a `control` id per row. P6a's
  "safe" rule (§2.2 there) is retired: every row with a `FireflowHW`
  counterpart gets its id. `ROOT_A`, `ROOT_B`, `REV_MOD` stay without one.
  `kCouponControls` maps RV2/RV4/RV6 to `RATE_A`/`DENSITY_A`/`FILT_A` as
  `control` ids, so the coupon runs the same law as Rev A.
- **Knob vector.** `float g_knobs[125]`, filled from `kInitParamDefaults` at
  boot — depths included, and not editable in P6b-1. A pot that emits writes
  `lo + v·(hi − lo)`; a `snap` parameter (ENGINE, STEPS, FLUXRATE, SCALE among
  them — the generator decides, from Rack's `snapEnabled`) is rounded as Rack
  rounds it.
- **Call site.** `tick()` runs once per audio block, after
  `panel_scan_tick()`, over the whole vector. `Events` are ignored: a physical
  FLUX pot cannot be turned back. Known divergence, written into
  `shell/README.md`: after a BBD edge the hardware's FLUX keeps its pot value
  where VCV's knob drops to zero.
- `apply_control()` and `control_value()` in `shell/controls.cpp` go; nothing
  else calls them.

### 4.3 Tick rate

VCV ticks every 16 samples, the firmware every 96 (one block). **Probe before
the plan's law task:** for each piece of per-tick state (drift settle, song
rung, bbd edge, the lane snapshot) and for every setter the law calls, does the
result depend on how often it is called? A desktop probe ticks the law at 16
and at 96 over the same knob sweep and prints the setter streams' end states
and any time-based constant it finds. Whatever depends on the rate is scaled by
a `dt` argument to `tick()` rather than assuming a tick count. The probe's
answer goes into `docs/engine-map.md`.

## 5. Memory — the first task

**Measured 2026-10-09** (`450eed36`, `shell/build` with the ARM toolchain):
the Rev A image (`SHELL_PANEL_SCAN=1`) uses **261 340 of 262 880 B of
SRAM_EXEC, 1540 B free**; the coupon play image 260 524 B. `.text` is
259 032 B of it. `.qspiflash_text` exists in the link and is empty;
`.qspiflash_data` holds 65 024 B. `shell::apply_control` alone is 3036 B, so
the law will not fit in 1540 B.

**Where room comes from.** A name heuristic over the Rev A ELF's text symbols
(`init|Init|MspInit|Config|Setup|setup|configure`) sums to 47 296 B, and the
USB device and host stacks to about 13.4 KB. The heuristic is a pointer, not a
list: the plan builds the list by call graph, not by name.

1. **Probe:** a single cold function placed in `.qspiflash_text`, image
   flashed to the coupon over DFU, boots and plays — code executes from QSPI
   on this boot path.
2. **Move a curated list** of boot-only functions (engine `init`s, HAL/daisy
   init, clock config) to `.qspiflash_text`.
3. **Guard** (host-side script over `build/shell.map`, wired into ctest the way
   `shell_panel_map_guard` is): no function on the audio path and none that
   runs on an engine switch — `FeedEngine::_rebuild_allocation`,
   `FeedEngine::init`, `SamplerEngine::_update_control` and whatever the call
   graph adds — sits in QSPI. Proven RED once by placing one of them there.

**Margin:** with the law linked, the Rev A image keeps **at least 8 KB** of
SRAM_EXEC free. **Fallback**, if the move yields less: `-Os` for
`control_law.o`, as `mux_plan.o` already has.

## 6. CPU

The shell's CPU reserve is about **2.9 points** of the 960 000-cycle block
(`shell/README.md`, measured earlier; this spec does not re-measure it),
roughly 28 000 cycles. The plan measures `tick()` on the coupon with the
existing cycle counter (`shell/cycles.h`).

**Decision rule, fixed before the measurement:** if `tick()` costs more than
**1 point (9600 cycles)** per call, it runs every second block; if it still
costs more than 1 point averaged, blocks A–G are spread across blocks. The
measured figure and the rule's outcome go into the roadmap's M6 entry.

## 7. Tests

### 7.1 `tests/test_control_law.cpp`

The law is instantiated on a recorder that logs every setter call (name, deck,
value). Gates, each with one RED proven:

- LVL/COMP split (`kLvlCompSplit`, `kCompTop`, `kCompShape`);
- GRIT dead zone (`kGritDead`) and FLUX's on-threshold;
- ENGINE remap, with and without `test_tone`;
- DETUNE squared, skipped on FEED;
- COUPLE zone split and DRIFT settle;
- TEMPO from the knob, and with `measured_bpm` in and out of 20..400;
- `mv()` for each kind: lane term, mirror term (PAN_B reads deck A),
  centre term; and the 50-row depth loop's split into engine-backed and
  host-computed;
- the BBD edge returns its event exactly once per edge;
- the SONG ladder's rung changes;
- `on_reset()` re-arms song rung and drift settle but not the BBD edge;
  `on_restore()` re-arms all three;
- `DeckOptions{}` equals a default `SamplerPartState`.

### 7.2 Firmware gates

- `shell/test_gen_panel_map.py`: only `ROOT_A`, `ROOT_B`, `REV_MOD` lack an id;
  every other row's id is the one `FireflowHW` places at that position.
- A range test: every init default lies in `[lo, hi]`; `snap` is set exactly
  for the ids Rack snaps.
- The QSPI placement guard of §5.
- Build: all three images link; the Rev A image's free SRAM_EXEC is printed
  and checked against §5's margin.

### 7.3 VCV unchanged

Rack does not link into `spky_tests`, so three things together:

1. The extraction is its own commit that **moves** code;
   the review reads it with `git diff --color-moved`.
2. The gates of §7.1.
3. After `host/vcv/build-local.sh install`, Bastian checks in Rack: the init
   patch sounds as before, and an engine switch, a BBD edge and a SONG
   change behave as before.

### 7.4 Coupon session

One session: (1) the QSPI image boots and plays; (2) RV2, RV4, RV6 move
RATE_A, DENSITY_A, FILT_A audibly through the law; (3) `tick()`'s cycles are
read and §6's rule applied.

## 8. Known divergences after P6b-1

- Depths are the init patch's and cannot be edited (P6b-2).
- After a BBD edge the hardware's FLUX stays at its pot value (§4.2).
- A Sampler deck is silent: no factory sample, no REC yet.
- No CLOCK, RESET or CV on the hardware.
