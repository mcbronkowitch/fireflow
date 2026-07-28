#include "workload.h"
#include "mem.h"
#include "fx/bbd.h"
#include "fx/flux.h"
#include <cmath>

namespace bench {
namespace {

using namespace spky;

// --- BbdEcho: one line at the ceiling ---------------------------------------
// taps_2_opt (bench/workloads_taps.cpp, now deleted) priced two mono reads
// and two one-poles. This row prices what replaced them, and it is priced at
// the WORST case rather than a typical one, because the whole CPU argument of
// the redesign is "the taps pay for the BBD and nothing more" and a typical
// figure cannot settle that.
//
// The worst case is the CLOCK, not the delay time: you pay CPU per Hz of BBD
// bandwidth and delay time does not enter (spec "One correction recorded on
// purpose"). So this row runs the clock at its 32 kHz ceiling -- 2*32000/48000
// = 1.33 ticks per audio sample, 0.67 write events and 0.67 read events --
// and STAGES at its maximum, which is where the cell array is largest and
// least cache-friendly. One line, because BbdEcho is per channel and the
// two-part instrument cost is this row four times.

BbdEcho g_echo;

void setup_bbd_ceiling()
{
    g_echo.Init(kSampleRate, sdram_arena(), Flux::kMaxSamples);
    g_echo.SetStages(bbd_tuning::kMaxStages);
    g_echo.SetDrive(0.85f);             // deep into the saturator every pass
    g_echo.SetFeedback(1.1f);           // just under the bloom, loop always hot

    // Settle: the line must be FULL before measuring, or the loss pole, the
    // compander envelopes and the feedback path all run on zeros and the row
    // measures an empty machine. At the ceiling clock a full line is
    // 16384/(2*32000) = 256 ms = 12288 samples; run four times that.
    for (int i = 0; i < 49152; ++i)
        g_echo.Process(0.3f * sinf(static_cast<float>(i) * 0.01f),
                       bbd_tuning::kClockMaxHz);
}

float proc_bbd_ceiling()
{
    const float* in = test_input();
    float acc = 0.f;
    for (size_t s = 0; s < kBlock; ++s)
        acc += g_echo.Process(in[s], bbd_tuning::kClockMaxHz);
    return acc;
}

// --- BbdLine alone: the model without the compander or the drive path -------
// Splits the bill. If the ceiling row comes in over budget, this says whether
// the cost is the filter branches and the event work (which the spec's two
// pre-authorised levers address: drop the clock ceiling to 24 kHz, drop
// kMaxStages to 8192) or the compander's two sqrtf per sample (which they do
// not).

BbdLine g_line;

void setup_bbd_line_only()
{
    g_line.Init(sdram_arena(), Flux::kMaxSamples, kSampleRate);
    g_line.SetStages(bbd_tuning::kMaxStages);
    g_line.SetClock(bbd_tuning::kClockMaxHz);
    for (int i = 0; i < 49152; ++i)
        g_line.Process(0.3f * sinf(static_cast<float>(i) * 0.01f));
}

float proc_bbd_line_only()
{
    const float* in = test_input();
    float acc = 0.f;
    for (size_t s = 0; s < kBlock; ++s)
        acc += g_line.Process(in[s]);
    return acc;
}

// --- a tap line, at the ceiling and at half the clock ------------------------
// The rhythm-taps design (docs/superpowers/specs/2026-07-28-flux-rhythm-taps-
// design.md) puts eight bare BbdLines beside the four BbdEchos, fixed at
// kTapStages = 4096, and its own section 8 says the COMMON case is the
// expensive one: tap offsets come from a rhythm, rhythms are mostly fast, and
// a short offset means a fast clock.
//
// Two rows, because one number cannot answer the question that decides the
// design. bbd_line_only above prices 16384 stages at the ceiling; these price
// the tap configuration itself, and then the same line at half the clock:
//
//   - the pair against each other splits the bill between EVENT work (scales
//     with the clock: 2*f/fs ticks per sample) and the FIXED per-sample filter
//     work (does not). Only that split says whether raising the tap floor from
//     64 ms to 128 ms -- which halves the maximum clock -- buys anything.
//   - bbd_line_tap against bbd_line_only says what the stage count alone is
//     worth, at identical clock. bbd_walk_sdram's 0.09 % predicts "almost
//     nothing"; this measures it rather than assuming it.
//
// A separate BbdLine object rather than reusing g_line: the runner calls
// setup() immediately before each workload's warmup (bench/runner.cpp:23-28),
// so sharing the arena between rows is safe, but sharing the OBJECT would
// leave the second row's SetStages fighting the first row's settled state.

constexpr int kTapStages = 4096;

BbdLine g_tap;

// Full line at the ceiling is 4096/(2*32000) = 64 ms = 3072 samples; at half
// clock, 128 ms = 6144. 32768 covers both several times over -- the line must
// be FULL before measuring, for the same reason setup_bbd_ceiling says.
void settle_tap()
{
    for (int i = 0; i < 32768; ++i)
        g_tap.Process(0.3f * sinf(static_cast<float>(i) * 0.01f));
}

void setup_bbd_line_tap()
{
    g_tap.Init(sdram_arena(), kTapStages / 2, kSampleRate);
    g_tap.SetStages(kTapStages);
    g_tap.SetClock(bbd_tuning::kClockMaxHz);
    settle_tap();
}

void setup_bbd_line_tap_half()
{
    g_tap.Init(sdram_arena(), kTapStages / 2, kSampleRate);
    g_tap.SetStages(kTapStages);
    g_tap.SetClock(0.5f * bbd_tuning::kClockMaxHz);
    settle_tap();
}

float proc_bbd_line_tap()
{
    const float* in = test_input();
    float acc = 0.f;
    for (size_t s = 0; s < kBlock; ++s)
        acc += g_tap.Process(in[s]);
    return acc;
}

// --- the SDRAM shape --------------------------------------------------------
// The active window at 8192 stages is 4096 cells = 16 KB per line, walked
// SEQUENTIALLY (imem advances by exactly one cell per write tick and the read
// tick reads the cell about to be overwritten). The 3.29x SDRAM penalty
// measured for streaming walks is EXPECTED to largely disappear here. That is
// an expectation, not a measurement -- this row is what turns it into one.

constexpr int kWalkCells = 4096;
int g_walk = 0;

void setup_bbd_walk_sdram()
{
    float* a = sdram_arena();
    for (int i = 0; i < kWalkCells; ++i) a[i] = sinf(static_cast<float>(i) * 0.0007f);
    g_walk = 0;
}

float proc_bbd_walk_sdram()
{
    const float* in = test_input();
    float* a = sdram_arena();
    float acc = 0.f;
    for (size_t s = 0; s < kBlock; ++s) {
        acc += a[g_walk];
        a[g_walk] = in[s];
        g_walk = (g_walk + 1 < kWalkCells) ? g_walk + 1 : 0;
    }
    return acc;
}

} // namespace

const Workload kBbdWorkloads[] = {
    { "bbd", "bbd_ceiling",     setup_bbd_ceiling,     proc_bbd_ceiling     },
    { "bbd", "bbd_line_only",   setup_bbd_line_only,   proc_bbd_line_only   },
    { "bbd", "bbd_line_tap",      setup_bbd_line_tap,      proc_bbd_line_tap },
    { "bbd", "bbd_line_tap_half", setup_bbd_line_tap_half, proc_bbd_line_tap },
    { "bbd", "bbd_walk_sdram",  setup_bbd_walk_sdram,  proc_bbd_walk_sdram  },
};
const int kBbdCount = sizeof(kBbdWorkloads) / sizeof(kBbdWorkloads[0]);

} // namespace bench
