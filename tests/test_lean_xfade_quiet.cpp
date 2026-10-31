// CPU round 2, candidate V7 (class 2, spec 2026-10-30-cpu-engine-kernels-
// design.md section 6): a WAVE strike from an envelope below -60 dB (1e-3,
// VoiceT::trigger) starts no mip crossfade -- WtOsc::set_freq(hz, quiet) snaps
// to the new mip. Built into spky_lean_tests only, the desktop target compiled
// with SPKY_LEAN_XFADE_QUIET=1 as a SHELL_LEAN_XFADE_QUIET=1 firmware is; the
// default build has no quiet argument, so these cases could not run there.
//
// The old path at the same strike is built from the public calls the old
// trigger() was made of: set_pitch_hz(f) starts the crossfade, and a second
// trigger(f) then only retriggers the envelope (the oscillators already sit on
// f's mip, so the quiet snap has nothing to do and the fade runs on).
// SPKY_V7_PRINT=1 in the environment prints the measured steps.
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "synth/voice.h"
#include "synth/wt_osc.h"

namespace {

constexpr float kSr    = 48000.f;
constexpr float kFrom  = 220.f;    // the voice's pitch before the strike: mip 2
constexpr float kTo    = 1000.f;   // the strike: mip 4, two octaves up
constexpr float kQuiet = 1e-3f;    // VoiceT's kXfadeQuiet

bool print_on() {
    static const bool on = std::getenv("SPKY_V7_PRINT") != nullptr;
    return on;
}

using WaveVoice = spky::VoiceT<spky::WtOsc>;

// A WAVE voice with the filter wide open and no sub, so the oscillators'
// output reaches the step measurement as it is. Idle, struck at kFrom.
struct Rig {
    WaveVoice v;
    Rig() {
        v.init(kSr, 7u);
        v.set_cutoff_hz(0.3f * kSr);
        v.set_sub_level(0.f);
        v.set_env_times(0.01f, 0.05f);
        v.set_pitch_hz(kFrom);
    }
    // The next n output samples (left channel).
    void run(int n, std::vector<float>& out) {
        for (int i = 0; i < n; ++i) {
            float l = 0.f, r = 0.f;
            v.process(l, r);
            out.push_back(l);
        }
    }
};

double max_step(const std::vector<float>& x) {
    double worst = 0.0;
    for (size_t i = 1; i < x.size(); ++i)
        worst = std::fmax(worst, std::fabs(double(x[i]) - double(x[i - 1])));
    return worst;
}

// A voice struck at kFrom and run until its envelope first reads below
// `level` while falling (the decay at 0.05 s per 60 dB moves it by 0.03 % per
// sample, so the reading lands within that of `level`).
void decay_to(Rig& r, float level) {
    r.v.trigger(kFrom);
    std::vector<float> sink;
    int guard = 0;
    while (r.v.env_value() < 0.5f && guard++ < 100000) r.run(1, sink);   // attack
    guard = 0;
    while (r.v.env_value() >= level && r.v.active() && guard++ < 100000) r.run(1, sink);
}

} // namespace

TEST_CASE("V7: a strike from silence skips the crossfade") {
    // The oscillator: a quiet change of pitch across mips snaps, a plain one fades.
    spky::WtOsc plain, quiet;
    plain.init(kSr);
    quiet.init(kSr);
    plain.set_freq(kFrom);
    quiet.set_freq(kFrom);
    REQUIRE_FALSE(plain.mip_crossfading());
    plain.set_freq(kTo);
    quiet.set_freq(kTo, true);
    CHECK(plain.mip_crossfading());
    CHECK_FALSE(quiet.mip_crossfading());
    CHECK(quiet.mip_level() == plain.mip_level());   // the same mip, reached at once
    CHECK(quiet.mip_level() != 2);                   // and not the one it left

    // The voice: idle, its envelope at 0, struck up two octaves.
    Rig r;
    REQUIRE(r.v.env_value() < kQuiet);
    r.v.trigger(kTo);
    CHECK_FALSE(r.v.osc_crossfading());
    CHECK(r.v.active());

    // The old path at the same strike starts the fade (so this case can fail).
    Rig old;
    old.v.set_pitch_hz(kTo);
    CHECK(old.v.osc_crossfading());
}

TEST_CASE("V7: a steal from a sounding voice keeps it") {
    // A voice whose attack has carried its envelope to 0.5, struck again up
    // two octaves: it crossfades as it always did, and the largest
    // sample-to-sample step is not above the old path's.
    Rig a, b;
    for (Rig* r : { &a, &b }) {
        r->v.trigger(kFrom);
        std::vector<float> sink;
        int guard = 0;
        while (r->v.env_value() < 0.5f && guard++ < 100000) r->run(1, sink);
        REQUIRE(r->v.env_value() >= 0.5f);
        REQUIRE_FALSE(r->v.osc_crossfading());
    }
    a.v.trigger(kTo);                      // the candidate
    b.v.set_pitch_hz(kTo);                 // the old path: pitch, then envelope
    b.v.trigger(kTo);
    CHECK(a.v.osc_crossfading());
    CHECK(b.v.osc_crossfading());

    std::vector<float> xa, xb;
    a.run(480, xa);
    b.run(480, xb);
    CHECK_FALSE(a.v.osc_crossfading());    // the 96-sample fade ran down
    const double sa = max_step(xa), sb = max_step(xb);
    if (print_on()) std::printf("V7 steal at env 0.5: max step new %.6g old %.6g\n", sa, sb);
    CHECK(sa > 0.0);                       // it sounded
    CHECK(sa <= sb);
    CHECK(xa == xb);                       // the very same samples
}

TEST_CASE("V7: a strike from just below -60 dB adds no step beyond the -60 dB residue") {
    // The voice has decayed to just under 1e-3 (still active: the envelope
    // idles at 1e-4) and is struck up two octaves. The snap is the only
    // difference to the old path; it can move the output by no more than the
    // table difference times the envelope, which is below kQuiet for the
    // first sample, and the attack that follows is the same envelope on both.
    Rig a, b;
    for (Rig* r : { &a, &b }) {
        decay_to(*r, kQuiet);
        REQUIRE(r->v.active());
        REQUIRE(r->v.env_value() < kQuiet);
        REQUIRE(r->v.env_value() > 0.99f * kQuiet);
        REQUIRE_FALSE(r->v.osc_crossfading());
    }
    a.v.trigger(kTo);                      // the candidate: quiet, snaps
    b.v.set_pitch_hz(kTo);                 // the old path: fades
    b.v.trigger(kTo);                      // (quiet, but already on the mip)
    CHECK_FALSE(a.v.osc_crossfading());
    CHECK(b.v.osc_crossfading());

    std::vector<float> xa, xb;
    a.run(480, xa);
    b.run(480, xb);
    const double sa = max_step(xa), sb = max_step(xb);
    if (print_on()) std::printf("V7 strike at env just below 1e-3: max step new %.6g old %.6g\n", sa, sb);
    // The fade's own span (96 samples), where the two paths differ.
    const std::vector<float> fa(xa.begin(), xa.begin() + spky::WtOsc::kRampSamples);
    const std::vector<float> fb(xb.begin(), xb.begin() + spky::WtOsc::kRampSamples);
    const double fsa = max_step(fa), fsb = max_step(fb);
    if (print_on()) std::printf("  within the first 96 samples: new %.6g old %.6g\n", fsa, fsb);
    CHECK(sa > 0.0);
    CHECK(sa <= sb + double(kQuiet));      // no step above the old path's beyond the residue
    CHECK(fsa <= fsb + double(kQuiet));
    CHECK(std::all_of(xa.begin(), xa.end(), [](float s) { return std::isfinite(s); }));
}

TEST_CASE("V7: a SYNTH voice has no crossfade to skip") {
    spky::VoiceT<spky::MorphOsc> v;
    v.init(kSr, 7u);
    v.trigger(kTo);
    CHECK_FALSE(v.osc_crossfading());
}
