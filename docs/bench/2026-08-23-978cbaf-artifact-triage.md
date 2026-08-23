# The board's standing tone is three sources, not one

**Date** 2026-08-23, evening · **Board** `patch_sm`, serial `385138563330`
(the submodule with the 3.5 mm jacks) · **Images** `shell-audio-mux0.bin` and
`shell-cpu-mux0.bin`, both built at `978cbaf`; `shell/` unchanged since ·
**Capture** `ffmpeg -f dshow -i audio="Line (Universal Audio Twin USB)"`,
interface gain fixed across every run, **channel 0 only** — the rig is mono ·
**Analysis** band RMS by masking rFFT bins and transforming back, the method of
`tools/blockrate_fft.py`.

This started as the artifact session that
[`2026-08-23-978cbaf-shell-mux-placement.md`](2026-08-23-978cbaf-shell-mux-placement.md)
handed on: a 500 Hz harmonic series, diagnosed 8 Aug, never explained. It ends
somewhere else. What sounded like one tone that changed pitch is three separate
sources, and the loudest of them is not the one under investigation.

## The numbers

| condition | 500 Hz series | 500 Hz alone | ~6.35 kHz | 12.7 kHz |
|---|---:|---:|---:|---:|
| audio image, module **in**, morning | −60.58 | — | −86.86 | −97.76 |
| audio image, module **in**, after re-seating | −60.82 | −63.31 | **−55.79** | −71.95 |
| audio image, module **out** | −60.92 | — | **−93.57** | −101.01 |
| audio image, module **out**, 20 s | −60.73 | — | −93.22 | −100.68 |
| probe image, audio **running** | −74.39 | −78.54 | −55.73 | −65.67 |
| probe image, audio **stopped**, USB up | −77.49 | **−92.94** | −55.29 | −65.50 |

All dBFS. "Module" is the MAX11300 breakout on the desk rig. The two probe-image
rows are one 30 s capture split at the point where `main.cpp:218` calls
`StopAudio` — 2500 blocks of 96 at 48 kHz, i.e. 5 s in.

## What it says

**1. The audible whine is the MAX11300 module, and the ablation is 38 dB.**
A cluster around 6.35 kHz with sidebands ~20 Hz apart and a second harmonic at
12.7 kHz — a switcher's signature, not a tone from the engine. Module in:
−55.8 dBFS. Module out: −93.6. It is also audible acoustically at the module
itself, which is how it got noticed.

> **Corroborated by ear, and that matters here.** Bastian matched the pitch of
> the whine *in the recorded signal* against the acoustic whine audible at the
> module itself and reports them as the same. An ear-match is not a
> measurement, but it is an **independent** observation, and it rules out the
> reading the ablation alone cannot: that something else on the same path
> happens to ring at 6.35 kHz. The source is the part you can hear.

> **And it is free-running.** The question posed earlier in the session — does
> the whine change at the 5 s mark, when the audio callback stops and the
> board's current draw changes pattern in one step — is answered **no**, twice
> over: the measured level moves 0.44 dB (−55.73 running, −55.29 stopped) and
> the pitch is unchanged by ear. The converter is not modulated by the MCU's
> compute pattern. The 500 Hz series is (finding 4); this is not. Whatever
> couples the whine in can therefore be attacked without touching firmware.

**2. Its severity depends on how the module sits.** −86.9 dBFS in the morning,
−55.8 after it was pulled and re-inserted: **31 dB louder**, same module, same
board, same image. So the number above is not a property of the part; it is a
property of that contact on that afternoon. Anything built on the absolute
value is built on sand.

**3. "The tone got higher" was a different source becoming dominant.** With
audio stopped the 500 Hz line falls to −92.9 and the whine holds at −55.3,
dominating by 22 dB. Nothing shifted pitch; the lower source went away and
uncovered a higher one.

But **not** one that had been there all along at this level, which is the
tempting reading and the wrong one:

| | 500 Hz series | whine | gap |
|---|---:|---:|---:|
| morning, module in | −60.58 | −86.86 | whine **26 dB under** |
| after re-seating | −60.82 | −55.79 | whine **5 dB over** |

The series moved 0.24 dB. The whine moved **31 dB**. So it was masked all day —
but by its own weakness, not by the artifact's strength, and what uncovered it
was the re-seating and not the audio stopping. Both had to happen for it to
become the thing one notices.

**4. The 500 Hz line is tied to the audio callback.** −63.3 with audio running,
**−92.9 with it stopped** — 30 dB. Whatever couples it in needs the audio block
to be happening. That is consistent with the 8 Aug pair of measurements
(forcing silence changed nothing, filling the idle time with a `nop` loop
lowered it 7.5 dB) and it narrows them: not the signal path, but the block.

**5. USB's start-of-frame is real, measurable and irrelevant.** With audio
stopped there is a line at exactly 1000.0 Hz, which at that moment can only be
USB full speed. It sits near −86 dBFS, thirty decibels under the whine. It was
worth predicting and it is not worth fixing.

> **A limit of this rig that follows from it:** 1 kHz is the second harmonic of
> the block rate, so USB's SOF and the artifact are **not spectrally
> separable**. Only a state with audio stopped tells them apart. Any future
> claim about a 1 kHz component has to say which state it was measured in.

**6. The open lead, and it is a good one.** Two images, the same engine, the
same operating point, audio running in both: the artifact differs by
**15.5 dB** (−63.0 against −78.5 on the fundamental, matched 4 s windows).
While the probe is running, the two callback bodies differ by two cycle-counter
reads and one increment — work that cannot account for 15 dB. So the difference
lives outside the callback body, and what is left is the binary itself: layout,
placement, what the linker did with `inst.process`. That is a candidate class,
not a mechanism, and it is deliberately not named further here.

**The experiment it implies** is cheap and discriminating: build one image with
`CpuLoadMeter` linked but never called, so the two binaries differ in layout
and in nothing else. If the artifact follows the layout, this stops being a
mystery and becomes a build-time property. If it does not, the difference is
somewhere neither image has been read for yet.

## And one hardware fact

Pulling the module makes the engine **inaudible at the jack** — the program
material drops to the noise floor while the firmware keeps reporting
74.33 % avg / 76.68 % max, identical to the baseline it printed on the *other*
submodule. Re-inserting it brings the sound back, same image, same md5. The
module is electrically load-bearing for the audio output on this desk rig.
What it provides has not been traced, and the control PCB has to provide it on
purpose rather than by accident.

## Honest limits

- **One channel, one board, one afternoon.** Every figure is channel 0 of a
  mono-patched interface (see the placement capture's note on the 6 dB trap the
  first pass fell into).
- **The whine's level is not a repeatable number** — finding 2 is the reason.
  The 38 dB ablation is solid because both of its rows come from the same
  seating; the −55.8 is not.
- **The program material wanders 6.5 dB per second**, so anything quoted
  relative to it needs a window of tens of seconds. The artifact bands
  themselves are stable to ~1 dB across twelve captures.
- **The mechanism of the 500 Hz series is still unnamed.** Three measurements
  now constrain it — not in the samples, sensitive to idle-time content,
  sensitive to the binary — and none of them names it.

## The idle loop is the whole 15.5 dB

**Later the same evening** · **Board** `patch_sm` `3859386B3330`, the bare
submodule, a Thonkiconn wired to **B2** and **A7**, no other module on the rig ·
**Images** `shell-idle{0,1,2}.bin` at `1338d48`, three distinct hashes,
position 0 byte-identical to `shell-audio-mux0.bin` · one 15 s capture each,
interface gain untouched, channel 0.

Finding 6 above proposed the binary's layout as the candidate class for the
15.5 dB between two images. **It was not the layout. It was the foreground
loop**, and `SHELL_IDLE_FILL` shows it inside one image, with a byte-identical
callback in all three positions.

| fill | idle loop | 500 Hz series | 500 Hz alone | rest | series over rest |
|---|---|---:|---:|---:|---:|
| 0 | `while(1) {}`, no memory access | −62.88 | −65.15 | −73.64 | **+10.8** |
| 1 | spin on a volatile | **−75.73** | **−79.31** | −76.07 | **+0.3** |
| 2 | that spin plus ALU work | −59.35 | −61.74 | −66.70 | +7.4 |
| — | sine only, engine bypassed | −73.96 | −86.61 | −76.16 | — |
| — | plug pulled at the board | −113.72 | −128.44 | −96.30 | — |

**1. The mystery of finding 6 is closed.** 12.9 dB on the series and 14.2 dB on
the fundamental, from changing nothing but what the core does between blocks.
The `CpuLoadMeter` calls, the probe's counters and the linker were never
involved. The 8 Aug `nop`-loop measurement was the right thread all along; it
saw 7.5 dB because it had a different filling.

**2. It is not monotonic in idle load, and that kills the obvious fix.** Real
ALU work in the idle time (fill 2) is **16.4 dB worse** than the light spin and
**3.5 dB worse than doing nothing at all**. "Keep the CPU busy" is not a
remedy; it can make the artifact louder. Whatever the lever is, it is not the
quantity of idle work.

**3. There is an idle filling where the artifact stops being a tone.** At fill 1
the series sits **0.3 dB** above the surrounding floor, against 10.8 dB at
fill 0. It did not merely get quieter — it stopped standing out as a line. That
is the first firmware-side state anyone here has measured in which the standing
tone is not a standing tone.

**4. Bypassing the engine is not the same as filling the idle time.** The
sine-only image (engine replaced by a 997 Hz generator, callback trivial) lands
at −73.96 on the series — close to fill 1 in the band, but its *fundamental* is
at −86.61, the lowest of any image that had audio running. Two different levers,
both real, and this capture does not separate them.

### Honest limits of this section

- **Nothing drives the output on this rig.** libDaisy's own header calls A1 and
  A5 ±12 V *power inputs* (`daisy_patch_sm.h:263`, `:267`) and this submodule
  runs on USB alone, so the analog output stage has no rails. Everything above
  is coupling into a correctly wired but undriven pin — which is also why a
  997 Hz sine written at −13.5 dBFS in the buffer arrives at −76.9, and why B1
  and B2 behave identically to 0.05 dB.
- **Therefore: firmware-against-firmware on one wiring is valid** — the three
  rows are — **and the transfer to a driven output is not established.** The
  next round of this belongs on a rig with ±12 V, and until then no absolute
  number here should be quoted against the other board's.
- **One capture per position.** The run-to-run spread of this rig is not
  established; the deltas are large enough that it hardly matters for the
  verdict, but 3.5 dB (fill 2 against fill 0) is the one figure that would
  benefit from a repeat.
- **The mechanism is still unnamed.** What the three fills differ in besides
  "amount of work" — continuity of current draw, memory traffic, the
  periodicity of the store in fill 2 — is not measured, and naming one would be
  the exact mistake this file has avoided twice already.

## What ruled out USB, for the record

Bastian noticed that during a DFU flash the output is **dead silent**, and the
noise appears the instant the application starts. The ST bootloader has the
board powered, the core running and **USB at maximum throughput** — 230 KB
going by — and produces nothing. So USB is not the carrier, and the 1 kHz SOF
line of finding 5 is a curiosity and not a cause. What the bootloader does not
do is initialise the codec: no SAI, no I2S, no audio DMA, no block grid.

Together with the rest of the day, the artifact now needs all of: the audio
subsystem running (`StopAudio` costs it 30 dB), the application rather than just
power (silence in the bootloader), and it scales with what happens between the
blocks (this section). It is not in the samples (8 Aug) and not the MAX11300
(finding 1).
