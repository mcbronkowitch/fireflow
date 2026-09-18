#include "tone_probe.h"

#include "cycles.h"
#include "shell_git_hash.h"
// SHELL_TONE_DC is read below and lives in the generated switch header
// beside SHELL_TONE_PROBE, so this translation unit needs it directly --
// main.cpp's include of it does not reach here. Same reason xtalk_probe.cpp
// includes shell_xtalk_probe.h directly. The Makefile carries the matching
// edge on tone_probe.o.
#include "shell_tone_probe.h"

namespace shell {

namespace {

// The bench discriminator: two rungs, alternating forever, on TP_AUDIO_L.
// Built to separate two readings that would otherwise look identical on a
// meter: "the node does not follow the callback at all" from "every level
// this callback can write happens to produce the same voltage at the
// node". Rung A is true silence -- the codec fed an exact zero, not a low
// level -- and rung B is 0 dBFS.
//
// Measured 2026-09-18 (spec section 9): rung A read -10.8 mV, rung B read
// -8.66 V. The node DID follow the callback -- the two rungs separated
// cleanly, which was this image's whole job. That result, together with
// the single-constant image's -4.34 V at -6 dBFS, is what answered spec
// section 9: the output is DC-coupled, the path inverts, and the chain
// does not saturate across the top 6 dB (-8.66 / -4.34 = 1.995, against
// 10^(6/20) = 1.995). This file stayed the bench instrument; it is not
// rebuilt into a level ladder or reverted to a single constant.
constexpr float kLevelSilence = 0.0f;   // rung A: exact zero, not a low level
constexpr float kLevel0dBFS   = 1.0f;   // rung B: 10^(0/20)

constexpr int   kLadderCount = 2;
constexpr float kLadderLevels[kLadderCount] = {kLevelSilence, kLevel0dBFS};
// -99 for the silent rung: there is no dBFS for an exact zero, and printing
// 0 there would collide with rung B's 0 dBFS in the output -- exactly the
// collision this image exists to rule out, so it must not exist here.
constexpr int   kLadderDbfs[kLadderCount]   = {-99, 0};

// How long each rung holds before the foreground steps to the next one --
// long enough for a handheld meter to settle and be read.
constexpr int kHoldSeconds = 15;

// Written by the foreground loop in run_tone_probe(), read by the callback.
// This is the ONLY thing that changes between rungs. The switch is driven
// from the FOREGROUND on purpose: the callback's job is the constant and the
// block count, and nothing that could make it late -- no deadline
// comparison, no rung bookkeeping, nothing that could turn a sample-accurate
// ISR into a jittery one. Reading this volatile costs the callback no more
// than reading a compile-time constant did before this two-rung switch
// existed.
volatile float g_out_level = kLadderLevels[0];

// Written by the callback, read by the foreground. volatile because the two
// are different contexts and nothing else synchronises them.
volatile uint32_t g_blocks = 0;

void AudioCallback(daisy::AudioHandle::InputBuffer  in,
                   daisy::AudioHandle::OutputBuffer out,
                   size_t                           size)
{
    (void)in;
    // NOTHING ELSE in this callback: no engine, no inst.process(). The
    // operating point of this image has to be "the codec, and only the
    // codec", or what the ADC reads is a measurement of the engine's supply
    // draw wearing the codec's name.
    const float level = g_out_level;
    for(size_t i = 0; i < size; ++i)
    {
        out[0][i] = level;
        out[1][i] = level;
    }
    ++g_blocks;
}

} // namespace

void run_tone_probe(bench::Board& hw)
{
    cycles_init();
    hw.StartLog(false);

    // Read, not assumed. main.cpp's CPU probe carries the comment about the
    // day a block size was inferred as 48 and was in fact 96.
    const int block_size = static_cast<int>(hw.AudioBlockSize());
    const int sr_hz      = static_cast<int>(hw.AudioSampleRate());

    hw.StartAudio(AudioCallback);

    int rung = 0;
    g_out_level = kLadderLevels[rung];

    while(1)
    {
        for(int held_s = 0; held_s < kHoldSeconds; ++held_s)
        {
            // A block that is a configuration line and an end marker, and
            // that is the whole of this task's firmware: the bench reading
            // is taken with a meter, not with this. blocks= is here so the
            // operator can see the callback is running at all -- a meter
            // reading of zero means two different things otherwise.
            //
            // rv4=%d is a literal 0, not a placeholder: this image runs no
            // rv4-dependent case (row 6 of kXtalkPlan is the only one, and
            // this plan's window cases point at row 3), so there is nothing
            // to read and printing it keeps the line's shape the same as
            // the crosstalk probe's.
            hw.PrintLine("SHELL_TONE_CFG adc_khz=%d repeats=%d phase_points=%d "
                         "block_size=%d sr=%d rv4=%d git=%s",
                         -1, 0, 0, block_size, sr_hz, 0, SHELL_GIT_HASH);
            // dbfs= is the live rung (-99 for silence, 0 for full scale --
            // see kLadderDbfs above), so the operator can tell from this
            // line alone which of the two rungs the meter is looking at
            // and how long it has been live (hold_s=, elapsed seconds
            // within the current rung, 0-indexed).
            hw.PrintLine("SHELL_TONE_DCCHECK dbfs=%d dc=%d blocks=%d hold_s=%d",
                         kLadderDbfs[rung], SHELL_TONE_DC,
                         static_cast<int>(g_blocks), held_s);
            hw.PrintLine("SHELL_TONE_END");
            hw.Delay(1000);
        }

        rung = (rung + 1) % kLadderCount;
        // FOREGROUND drives the switch -- see g_out_level's comment above.
        g_out_level = kLadderLevels[rung];
    }
}

} // namespace shell
