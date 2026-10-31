#pragma once

// Candidate toggles for the firmware's remote CPU hunt (spec
// docs/superpowers/specs/2026-10-29-cpu-hunt-remote-design.md section 5.3,
// plan Task 6). OFF unless the build defines SPKY_PROBE_TOGGLES, which only
// shell/'s measurement image does (SHELL_REMOTE_MEASURE=1): there the shell's
// `TOG <bit> <0|1>` command sets a bit of one word, and a candidate saving in
// engine/ runs its new path only while its bit is set, so the same image
// measures both paths against an interleaved baseline.
//
// THE RULE (spec 5.3, docs/gotchas.md): the word is read ONCE per block, at
// the top of Instrument::process, into the Instrument's _probe_tog, and a
// candidate tests that member -- never toggles() itself, and never a global
// inside a per-sample loop: global stores in that loop once pushed every
// block over budget (probe_mark.h says the same). shell/test_remote_wiring.py
// holds that toggles() is called in instrument.cpp alone, before the loop.
//
// Everywhere else -- the desktop, VCV, every other firmware image --
// toggles() is a constexpr 0, Instrument::_probe_tog a static constexpr 0
// (instrument.h), and on() a constant false, so every candidate branch
// compiles away and those builds carry neither the member nor the read.
//
// The bits. Each candidate claims one bit here, with its name and file; a
// bit is never reused within the round.
//
//   bit  name                 file                       what it switches
//   ---  -------------------  -------------------------  -----------------
//   0    GRIT_DECIM_STEP      engine/fx/grit.cpp         RETIRED: adopted, the
//                                                        only path now (the
//                                                        discarded Decimator
//                                                        step does state only)
//   1-3  FLUX_PROBE_*         engine/fx/flux.cpp,        USED, reverted: probes
//                             tape_echo.h                splitting FLUX (no echo,
//                                                        no filter, DTCM line);
//                                                        docs/hardware/cpu-hunt-
//                                                        measured.md §5
//   4    FLUX_STEREO_LINE     engine/fx/tape_echo.h      RETIRED: adopted, the
//                                                        only path now (the two
//                                                        tape echoes share one
//                                                        line-bookkeeping pass)
//   5    LAW_GUARDS           engine/parts/part.cpp,     RETIRED: adopted, the
//                             engine/fx/flux.h           only path now
//                                                        (unchanged-value guards
//                                                        for a BBD deck's
//                                                        set_flow and FLUX's
//                                                        rhythm pace)
//   6-7  CENTER_IDLE,         engine/center/center.cpp,  USED, reverted: Center::
//        COMP_CURVE           engine/fx/comp.cpp         update's libm memo and
//                                                        COMP's pow memo saved
//                                                        nothing on the board
//                                                        (docs/hardware/cpu-hunt-
//                                                        measured.md §5)
//   8    MIX_BLOCK            engine/instrument.cpp      RETIRED: adopted, the
//                                                        only path now (the PAN
//                                                        gains read once per
//                                                        tick, the CHOKE duck
//                                                        chain reused while
//                                                        depth_raw repeats); the
//                                                        limiter-knee part cost
//                                                        3.3 k more and was
//                                                        dropped
//   9    REVERB_HALF          engine/fx/reverb.cpp,      RETIRED: adopted as the
//                             third_party/oliverb        firmware build default
//                                                        SHELL_REVERB_HALF=1
//                                                        (class 2; blind A/B:
//                                                        no difference heard).
//                                                        The bit still selects
//                                                        the tank in a
//                                                        measurement image built
//                                                        with SHELL_REVERB_HALF=0
//   10   ABL_OSC_B            engine/synth/voice.cpp     USED, reverted after
//                                                        Phase A: VoiceT runs osc
//                                                        A twice, not osc B
//   11   ABL_SUB              engine/synth/voice.cpp     USED, reverted after
//                                                        Phase A: no sub osc
//   12   ABL_SVF              engine/synth/voice.cpp     USED, reverted after
//                                                        Phase A: the voice
//                                                        passes unfiltered
//   13   ABL_ENV_PAN          engine/synth/voice.cpp     USED, reverted after
//                                                        Phase A: envelope a
//                                                        constant 0.5, unit
//                                                        gains (Env::process
//                                                        still runs for state)
//   14   ABL_GRAIN_WIN        engine/sampler/grain.h     USED, reverted after
//                                                        Phase A: window w = 1,
//                                                        counters as before
//   15   ABL_GRAIN_FOLD       engine/sampler/            USED, reverted after
//                             sample_buffer.cpp          Phase A: read_linear
//                                                        skips the fold inside
//                                                        the content, clamps
//                                                        outside it
//   16   ABL_GRAIN_SRAM       engine/sampler/            USED, reverted after
//                             sample_buffer.cpp          Phase A: read_linear
//                                                        reads a 4096-frame
//                                                        static array (DTCM on
//                                                        the board), not the
//                                                        buffer
//                                                        (bits 10-16: measured
//                                                        in docs/hardware/
//                                                        cpu-hunt-measured.md
//                                                        section 7; spec
//                                                        2026-10-30-cpu-engine-
//                                                        kernels-design.md 5.1;
//                                                        each prices one stage
//                                                        and changes the sound)
//   17   MORPH_INV            engine/synth/morph_osc.h   RETIRED: adopted, the
//                                                        only path now (round 2
//                                                        V1: MorphOsc's segment,
//                                                        weight and dt fixed in
//                                                        the setters, bit-
//                                                        identical)
//   18   SIN_WRAPPED          engine/util/fast_sin.h     RETIRED: adopted, the
//                                                        only path now (round 2
//                                                        V3: the sub oscillator
//                                                        and MorphOsc's sine
//                                                        segment call
//                                                        fast_sin_wrapped, no
//                                                        second floor wrap, bit-
//                                                        identical)
//   19   LEAN_SVF1            engine/util/svf_lp.h,      candidate V6 (round 2,
//                             engine/synth/voice.cpp     class 2): SYNTH and
//                                                        WAVE voices filter in
//                                                        one pass
//                                                        (SvfLp::ProcessSingle);
//                                                        the firmware switch is
//                                                        SHELL_LEAN_SVF1
//   20   GRAIN_FOLD_FAST      engine/sampler/            RETIRED: adopted, the
//                             sample_buffer.cpp          only path now (round 2
//                                                        G1: read_linear skips
//                                                        the divide and floor
//                                                        for a frame inside
//                                                        (0, 2 size), a select
//                                                        for the second period,
//                                                        bit-identical)
//   21   LEAN_SAMPLER_I16     engine/sampler/            USED, reverted: round 2
//                             sample_buffer.h/.cpp       G4, the record buffer
//                                                        as int16 frames (eb6cd82,
//                                                        in-place conversion on
//                                                        the flip); saved ~9
//                                                        cycles per grain-sample,
//                                                        not worth clipping the
//                                                        overdub at full scale
//   22   LEAN_GRAIN_WIN       engine/sampler/grain.h     candidate G2 (round 2,
//                                                        class 2): a grain's
//                                                        Hann window multiplies
//                                                        by 1/atk, 1/dec, 1/fade
//                                                        latched at spawn and at
//                                                        release instead of
//                                                        dividing per sample; the
//                                                        firmware switch is
//                                                        SHELL_LEAN_GRAIN_WIN
//   23   GRAIN_READ_INLINE    engine/sampler/            RETIRED: adopted, the
//                             sample_buffer.h            only path now (round 2
//                                                        G3: read_linear is
//                                                        inline in the grain
//                                                        loop, bit-identical)
//   24   LEAN_XFADE_QUIET     engine/synth/wt_osc.h,     USED, reverted: round 2
//                             engine/synth/voice.cpp     V7, a WAVE strike from
//                                                        an envelope below
//                                                        -60 dB snapped to the
//                                                        new mip instead of
//                                                        crossfading (c20ded2a);
//                                                        engine section within
//                                                        the spread on 127, 135,
//                                                        313, 314 (most
//                                                        crossfades are not
//                                                        strikes from silence)
//   25-29 (free: candidates; bits 0-24 are never reused)
//   30   SHELL_LATE_REPEAT    shell/panel_scan.cpp       repeat the last LATE
//                                                        burn on every block
//   31   SHELL_GUARD_OFF      shell/panel_scan.cpp       the overrun guard is
//                                                        not consulted
//
// Bits 30 and 31 are the shell's, in the measurement image only: together
// they saturate the callback on purpose (the BOOT-under-load test, plan
// Task 7). The engine never reads them.

#include <cstdint>

namespace spky {
namespace probe {

// The candidates' bits (above).
inline constexpr int kBitReverbHalf = 9;
inline constexpr int kBitLeanSvf1   = 19;
inline constexpr int kBitLeanGrainWin = 22;
// The shell's two bits (above).
inline constexpr int kBitShellLateRepeat = 30;
inline constexpr int kBitShellGuardOff   = 31;

#if defined(SPKY_PROBE_TOGGLES)
inline constexpr bool kTogglesOn = true;
// One writer, the shell's audio callback (the TOG command is handled there),
// and one reader per block, Instrument::process, in the same interrupt: no
// volatile needed, and none wanted.
inline uint32_t g_toggles = 0;
inline uint32_t toggles() { return g_toggles; }
inline void     set_toggles(uint32_t word) { g_toggles = word; }
#else
inline constexpr bool kTogglesOn = false;
constexpr uint32_t toggles() { return 0; }
inline void        set_toggles(uint32_t) {}
#endif

// Whether `bit` is set in a word latched from toggles(). A constant false
// without SPKY_PROBE_TOGGLES, so `if(probe::on(_probe_tog, k))` folds away.
constexpr bool on(uint32_t word, int bit)
{
    return kTogglesOn && ((word >> bit) & 1u) != 0u;
}

} // namespace probe
} // namespace spky
