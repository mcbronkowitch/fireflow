#include <doctest/doctest.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <limits>
#include "instrument.h"
#include "fx/reverb.h"
#include "mod/super_modulator.h"
#include "mod/divisions.h"
using namespace spky;

TEST_CASE("instrument: init and render a block without NaNs") {
    Instrument inst;
    inst.init(48000.f);
    inst.set_tempo_bpm(120.f);
    inst.set_target_active(PART_A, LANE_PITCH, true);
    inst.set_target_active(PART_A, LANE_LEVEL, true);
    inst.set_rate(PART_A, 0.5f);
    inst.set_range(PART_A, 1.f);

    std::vector<float> l(96), r(96);
    inst.process(nullptr, nullptr, l.data(), r.data(), 96);
    for (int i = 0; i < 96; ++i) {
        CHECK(l[i] == l[i]);            // not NaN
        CHECK(l[i] >= -1.5f);
        CHECK(l[i] <=  1.5f);
    }
}

TEST_CASE("instrument: the two parts are decorrelated") {
    Instrument inst;
    inst.init(48000.f);
    inst.set_rate(PART_A, 0.5f);
    inst.set_rate(PART_B, 0.5f);
    inst.set_shape(PART_A, 1.f);
    inst.set_shape(PART_B, 1.f);
    inst.set_range(PART_A, 1.f);
    inst.set_range(PART_B, 1.f);

    std::vector<float> l(1), r(1);
    bool differ = false;
    for (int i = 0; i < 48000; ++i) {
        inst.process(nullptr, nullptr, l.data(), r.data(), 1);
        if (std::fabs(inst.lane_output(PART_A, LANE_PITCH)
                    - inst.lane_output(PART_B, LANE_PITCH)) > 0.05f) differ = true;
    }
    CHECK(differ);
}

TEST_CASE("instrument: set_scale is global and reaches both parts") {
    Instrument inst;
    inst.init(48000.f);
    inst.set_target_depth(PART_A, LANE_PITCH, 0.f);
    inst.set_target_depth(PART_B, LANE_PITCH, 0.f);
    inst.set_target_base(PART_A, LANE_PITCH, 0.5f);
    inst.set_target_base(PART_B, LANE_PITCH, 0.5f);
    inst.set_scale(SCALE_WHOLE);   // 18 semis is a whole-tone degree
    std::vector<float> l(1), r(1);
    for (int i = 0; i < 4000; ++i)   // ride out the 40 ms change slew
        inst.process(nullptr, nullptr, l.data(), r.data(), 1);
    CHECK(inst.pitch_cv(PART_A) == doctest::Approx(18.f / 36.f));
    CHECK(inst.pitch_cv(PART_B) == doctest::Approx(18.f / 36.f));
}

struct TapeMem {
    std::vector<float> echo[PART_COUNT][2];
    TapeMem() {
        for (int p = 0; p < PART_COUNT; ++p)
            for (int ch = 0; ch < 2; ++ch)
                echo[p][ch].resize(Flux::kMaxSamples);
    }
    void bind(FxMem& m) {
        for (int p = 0; p < PART_COUNT; ++p)
            for (int ch = 0; ch < 2; ++ch)
                m.echo[p][ch] = echo[p][ch].data();
    }
};
static TapeMem s_ti_tape;
static spky::AmbientReverb s_ti_reverb;

static spky::FxMem test_fx_mem() {
    spky::FxMem m;
    s_ti_tape.bind(m);
    m.reverb = &s_ti_reverb;
    return m;
}

TEST_CASE("instrument: all FX off + send 0 is bit-identical to the no-FX build") {
    Instrument plain;
    plain.init(48000.f);
    Instrument fx;
    fx.init(48000.f, test_fx_mem());
    for (int p = 0; p < PART_COUNT; ++p)
        fx.set_fx_target_base(p, FXT_REV_SEND, 0.f);   // before any process()
    fx.set_reverb_mix(0.f);                        // MIX 0: dry passes untouched
    float pl, pr, fl, fr;
    for (int i = 0; i < 48000; ++i) {
        plain.process(nullptr, nullptr, &pl, &pr, 1);
        fx.process(nullptr, nullptr, &fl, &fr, 1);
        CHECK(fl == pl);
        CHECK(fr == pr);
    }
}

TEST_CASE("instrument: boot reverb send is audible") {
    Instrument plain;
    plain.init(48000.f);
    Instrument fx;
    fx.init(48000.f, test_fx_mem());   // boot REV_SEND base = 0.25
    float pl, pr, fl, fr;
    int diff = 0;
    for (int i = 0; i < 48000; ++i) {
        plain.process(nullptr, nullptr, &pl, &pr, 1);
        fx.process(nullptr, nullptr, &fl, &fr, 1);
        if (std::fabs(fl - pl) > 1e-5f) ++diff;
    }
    CHECK(diff > 1000);
}

TEST_CASE("instrument: fx setters reach the parts and reverb setters are null-safe") {
    Instrument inst;
    inst.init(48000.f);                 // NO reverb, NO buffers
    inst.set_fx_on(PART_A, FxBlock::Grit, true);
    inst.set_grit_mode(PART_A, GritMode::Reduce);
    inst.set_fx_target_active(PART_A, FXT_GRIT_INT, true);
    inst.set_fx_target_base(PART_A, FXT_GRIT_INT, 0.6f);
    inst.set_fx_target_depth(PART_A, FXT_GRIT_INT, 0.5f);
    inst.set_flux_mix(PART_A, 0.4f);
    inst.set_grit_mix(PART_A, 0.7f);
    inst.set_reverb_size(0.9f);         // must not crash without a reverb
    inst.set_reverb_tone(0.2f);
    inst.set_reverb_decay(0.7f);
    inst.set_reverb_diffusion(0.5f);
    inst.set_reverb_mix(0.7f);
    float l, r;
    inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(inst.fx_target_value(PART_A, FXT_GRIT_INT) >= 0.f);
    CHECK(l == l);   // not NaN
}

TEST_CASE("instrument: boots both parts on the synth engine with an audible drone") {
    Instrument inst;
    inst.init(48000.f);
    CHECK(inst.engine_id(PART_A) == ENGINE_SYNTH);
    CHECK(inst.engine_id(PART_B) == ENGINE_SYNTH);
    float l, r, energy = 0.f;
    for (int i = 0; i < 48000; ++i) {
        inst.process(nullptr, nullptr, &l, &r, 1);
        energy += l * l;
    }
    CHECK(inst.active_voices(PART_A) >= 1);
    CHECK(inst.active_voices(PART_B) >= 1);
    CHECK(energy > 1e-3f);
}

// DENSE 0 leaves only the downbeat/anchor slot able to fire, so after the
// guaranteed first-sample fire (STEP entry: step -1 -> 0) the next natural
// note is a full cycle away. Settle past that single note's decay before
// checking silence, so the manual trigger is the only voice left.
TEST_CASE("instrument: voice setters and manual trigger reach the part") {
    Instrument inst;
    inst.init(48000.f);
    inst.set_voice_decay(PART_A, 0.f);      // shortest decay ratio (0.1x cycle)
    inst.set_step(PART_A, true, 8);
    inst.set_density(PART_A, 0.f);          // anchor-only: next natural fire is a cycle away
    float l, r;
    for (int i = 0; i < 10000; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(inst.active_voices(PART_A) == 0);
    inst.trigger_manual(PART_A);
    inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(inst.active_voices(PART_A) == 1);
    float peak = 0.f;                        // some voice's envelope is running
    for (int v = 0; v < 4; ++v) peak = std::max(peak, inst.voice_env(PART_A, v));
    CHECK(peak > 0.f);
}

TEST_CASE("instrument: set_step tolerates a zero step count") {
    // The panel merged the retired STEP pad's boolean into the STEPS knob's
    // own count (spec 2026-08-09 hw-control-reduction task 2/3; see task
    // 3's review, Finding 8): turning STEPS to its left stop now reaches
    // inst.set_step(p, false, 0) as a live, player-reachable runtime state,
    // not just a snapshot value nobody could dial in -- before the merge
    // the count passed alongside on=false was always >= 2.
    //
    // Walking the path from set_step: SuperModulator::set_step clamps
    // `_deck_steps = n < 1 ? 1 : n` and ModLane::set_step independently
    // clamps `steps < 1 ? 1 : steps` before storing `_steps` -- both guard
    // clock_scale() (8.f / _steps) and step_samples()'s reciprocal
    // (1.f / (... * _steps)), lane.h. clock_scale() only divides while
    // _step_mode is true, so a genuinely reachable FLOW-mode zero (on=false)
    // never reaches it regardless of the clamp. step_samples() has NO such
    // gate, though -- it divides by _steps whenever _phase_inc > 0, on or
    // off STEP -- so it is the one real hazard, and it is read only on a
    // Sampler deck (Part::_control_tick, `if (_engine_id == ENGINE_SAMPLER)
    // _sampler.set_step_clock(_mod.pitch_step_samples());`).
    //
    // This test exercises that combination -- Sampler engine, steps=0 --
    // but does NOT pin the clamp itself: with no sample loaded, SamplerEngine
    // ::set_step_clock() early-returns on an empty buffer before the value
    // this passes it is ever read, so this only proves an Instrument-level
    // Sampler with steps=0 stays NaN-free and in range end to end (still a
    // real regression net -- it just isn't exercising the divide). The
    // clamp's actual guarantee -- step_samples() itself, with _steps forced
    // to 0, never dividing by zero -- is pinned directly by
    // tests/test_lane.cpp's "lane: step_samples() tolerates a zero step
    // count", which is the load-bearing case.
    Instrument inst;
    inst.init(48000.f);
    inst.set_engine(PART_A, ENGINE_SAMPLER);
    inst.set_engine(PART_B, ENGINE_SAMPLER);
    inst.set_step(PART_A, false, 0);
    inst.set_step(PART_B, false, 0);

    std::vector<float> l(96), r(96);
    // Run long enough for the click-free engine swap (part.cpp) to land on
    // Sampler before checking output.
    for (int block = 0; block < 20; ++block)
        inst.process(nullptr, nullptr, l.data(), r.data(), 96);
    CHECK(inst.engine_id(PART_A) == ENGINE_SAMPLER);
    CHECK(inst.engine_id(PART_B) == ENGINE_SAMPLER);

    inst.process(nullptr, nullptr, l.data(), r.data(), 96);
    for (int i = 0; i < 96; ++i) {
        CHECK(l[i] == l[i]);            // not NaN
        CHECK(r[i] == r[i]);            // not NaN
        CHECK(l[i] >= -1.5f);
        CHECK(l[i] <=  1.5f);
        CHECK(r[i] >= -1.5f);
        CHECK(r[i] <=  1.5f);
    }
}

TEST_CASE("instrument: set_engine switches to the test tone and back") {
    Instrument inst;
    inst.init(48000.f);
    inst.set_engine(PART_A, ENGINE_TEST_TONE);
    float l, r;
    for (int i = 0; i < 1000; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(inst.engine_id(PART_A) == ENGINE_TEST_TONE);
    CHECK(inst.active_voices(PART_A) == 0);
    inst.set_engine(PART_A, ENGINE_SYNTH);
    for (int i = 0; i < 48000; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(inst.engine_id(PART_A) == ENGINE_SYNTH);
    CHECK(inst.active_voices(PART_A) >= 1);   // the drone resumes
}

TEST_CASE("instrument: WAVE is a melodic engine through the public API") {
    Instrument inst;
    inst.init(48000.f);
    inst.set_engine(PART_A, ENGINE_WAVE);
    inst.set_density(PART_A, 0.f);
    float l, r;
    for (int i = 0; i < 1000; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    REQUIRE(inst.engine_id(PART_A) == ENGINE_WAVE);
    CHECK(inst.active_voices(PART_A) >= 1);   // FLOW drone is observable at the Instrument API

    // A FLOW->STEP change releases the live drone, so it legitimately leaves
    // a tail beside the next strike. Start a fresh STEP part to test the
    // single-trigger contract itself rather than its release behavior.
    Instrument step;
    step.init(48000.f);
    step.set_step(PART_A, true, 8);
    step.set_density(PART_A, 0.f);
    step.set_engine(PART_A, ENGINE_WAVE);
    for (int i = 0; i < 500; ++i) step.process(nullptr, nullptr, &l, &r, 1);
    REQUIRE(step.engine_id(PART_A) == ENGINE_WAVE);
    step.trigger_manual(PART_A);
    step.process(nullptr, nullptr, &l, &r, 1);
    CHECK(step.active_voices(PART_A) == 1);   // STEP remains a single struck voice
}

TEST_CASE("instrument M4: couple 0 + drift 0 -> PITCH lane matches a bare SuperModulator") {
    Instrument inst; inst.init(48000.f);
    inst.set_couple(0.f); inst.set_drift(0.f);
    inst.set_rate(PART_A, 0.5f);
    SuperModulator ref; ref.init(48000.f, 0x1234abcdu);   // PART_A seed (see instrument.cpp)
    ref.set_rate(0.5f);
    bool same = true;
    std::vector<float> l(1), r(1);
    for (int i = 0; i < 20000; ++i) {
        inst.process(nullptr, nullptr, l.data(), r.data(), 1);
        ref.process();
        if (inst.lane_output(PART_A, LANE_PITCH) != ref.lane_output(LANE_PITCH)) same = false;
    }
    CHECK(same);   // Center writes rate_scale=1 / shape_offset=0 -> zero perturbation
}

// Two tests: DRY isolation is exact (no reverb), SEND isolation is a decaying
// tail (with reverb). One combined test was wrong — at morph 1 the DRY path is
// gone immediately, but the shared reverb keeps ringing out the send injected
// during the 0.5->1 morph ramp, so an absolute "difference < 1e-5 within 1 s"
// contradicts the design ("only its already-committed tail rings out").
TEST_CASE("instrument M4: morph=1 isolates part A's dry path") {
    Instrument x; x.init(48000.f);                 // no reverb: a pure dry-isolation check
    Instrument y; y.init(48000.f);
    x.set_morph(1.f); y.set_morph(1.f);            // full B; part A must stop contributing
    x.set_rate(PART_A, 0.3f); x.set_target_base(PART_A, LANE_PITCH, 0.2f);
    y.set_rate(PART_A, 0.9f); y.set_target_base(PART_A, LANE_PITCH, 0.9f);   // A differs a lot
    float xl, xr, yl, yr, maxd = 0.f;
    for (int i = 0; i < 48000; ++i) {
        x.process(nullptr, nullptr, &xl, &xr, 1);
        y.process(nullptr, nullptr, &yl, &yr, 1);
        if (i > 16000) { float d = std::fabs(xl - yl); if (d > maxd) maxd = d; }  // after morph snaps to 1
    }
    CHECK(maxd < 1e-5f);   // A's dry contribution is gone (gain_a = cos(pi/2) ~ 0)
}

TEST_CASE("instrument M4: morph=1 injects no new reverb from part A (send isolated)") {
    TapeMem echoX, echoY;
    static AmbientReverb rvX, rvY;
    FxMem mx, my;
    echoX.bind(mx); echoY.bind(my);
    mx.reverb = &rvX; my.reverb = &rvY;
    Instrument x; x.init(48000.f, mx);
    Instrument y; y.init(48000.f, my);
    x.set_morph(1.f); y.set_morph(1.f);
    x.set_reverb_decay(0.15f); y.set_reverb_decay(0.15f);   // short tail so 3 s covers full decay
    x.set_reverb_size(0.2f);   y.set_reverb_size(0.2f);     // small room too
    x.set_rate(PART_A, 0.3f); x.set_target_base(PART_A, LANE_PITCH, 0.2f);
    y.set_rate(PART_A, 0.9f); y.set_target_base(PART_A, LANE_PITCH, 0.9f);
    float xl, xr, yl, yr, early = 0.f, late = 0.f;
    const int N = 48000 * 3;
    for (int i = 0; i < N; ++i) {
        x.process(nullptr, nullptr, &xl, &xr, 1);
        y.process(nullptr, nullptr, &yl, &yr, 1);
        float d = std::fabs(xl - yl);
        if (i < 24000)      { if (d > early) early = d; }   // first 0.5 s: morph ramp injects A
        if (i >= N - 24000) { if (d > late)  late  = d; }   // final 0.5 s: only a decayed tail
    }
    // No new A energy enters the shared reverb at morph 1 -> the divergence is a
    // decaying tail: the final window is far below the early transient, near zero.
    CHECK(early > 1e-4f);
    CHECK(late < early * 0.05f);
    CHECK(late < 1e-4f);
}

TEST_CASE("instrument: set_comp forwards to the part chain") {
    // Two identically-seeded instruments, one with comp up: the comp'd one
    // must be louder on the same deterministic synth content.
    auto render_rms = [](float comp) {
        Instrument inst;
        inst.init(48000.f);                       // engine-only init: no FxMem needed
        inst.set_comp(0, comp);
        inst.trigger_manual(0);
        double acc = 0.0;
        float l[96], r[96];
        const float inL[96] = {0}, inR[96] = {0};
        int n = 0;
        for (int b = 0; b < 500; ++b) {
            inst.process(inL, inR, l, r, 96);
            if (b == 250) inst.trigger_manual(0);
            for (int i = 0; i < 96; ++i) { acc += l[i] * l[i]; ++n; }
        }
        return std::sqrt((float)(acc / n));
    };
    CHECK(render_rms(1.f) > render_rms(0.f));
}

TEST_CASE("instrument: master output never exceeds 1.0 even driven hard") {
    Instrument inst;
    inst.init(48000.f);
    inst.set_master_drive(1.f);                    // 4x into the ceiling
    inst.set_comp(0, 1.f);
    inst.set_comp(1, 1.f);
    inst.trigger_manual(0);
    inst.trigger_manual(1);
    float l[96], r[96];
    const float inL[96] = {0}, inR[96] = {0};
    for (int b = 0; b < 1000; ++b) {
        inst.process(inL, inR, l, r, 96);
        if (b % 100 == 0) { inst.trigger_manual(0); inst.trigger_manual(1); }
        for (int i = 0; i < 96; ++i) {
            CHECK(std::fabs(l[i]) <= 1.f);
            CHECK(std::fabs(r[i]) <= 1.f);
            CHECK(std::isfinite(l[i]));
        }
    }
}

TEST_CASE("instrument: dynamics chain is deterministic end to end") {
    auto run = [] {
        Instrument inst;
        inst.init(48000.f);
        inst.set_master_drive(0.7f);
        inst.set_comp(0, 0.9f);
        inst.trigger_manual(0);
        std::vector<float> out;
        float l[96], r[96];
        const float inL[96] = {0}, inR[96] = {0};
        for (int b = 0; b < 500; ++b) {
            inst.process(inL, inR, l, r, 96);
            for (int i = 0; i < 96; ++i) out.push_back(l[i]);
        }
        return out;
    };
    auto a = run(), b = run();
    for (size_t i = 0; i < a.size(); ++i) CHECK(a[i] == b[i]);
}

TEST_CASE("instrument M4.8: mix 0 is bit-identical to the engine-only build") {
    Instrument plain;
    plain.init(48000.f);
    Instrument fx;
    fx.init(48000.f, test_fx_mem());
    fx.set_reverb_mix(0.f);            // before the first process(): snaps
    // NOTE: the default sends stay live — the wet return is simply discarded
    float pl, pr, fl, fr;
    for (int i = 0; i < 48000; ++i) {
        plain.process(nullptr, nullptr, &pl, &pr, 1);
        fx.process(nullptr, nullptr, &fl, &fr, 1);
        CHECK(fl == pl);
        CHECK(fr == pr);
    }
    CHECK(fx.reverb_asleep());
}

TEST_CASE("instrument M4.8: mix 1 with muted sends is exact silence (dry fully gone)") {
    Instrument fx;
    fx.init(48000.f, test_fx_mem());
    fx.set_reverb_mix(1.f);
    for (int p = 0; p < PART_COUNT; ++p)
        fx.set_fx_target_base(p, FXT_REV_SEND, 0.f);   // empty room: wet is silence
    float l, r;
    for (int i = 0; i < 48000; ++i) {
        fx.process(nullptr, nullptr, &l, &r, 1);
        CHECK(l == 0.f);               // dry gain is EXACTLY 0 at the endpoint
        CHECK(r == 0.f);
    }
}

TEST_CASE("instrument M4.8: mix 0.5 sits at equal power (both gains cos(pi/4))") {
    // Three identically-seeded fx instruments at MIX 0 / 0.5 / 1, default
    // sends live. Their dry and wet streams are bit-identical, so:
    //   out0   = dry            out1   = wet
    //   out05  = 0.7071*dry + 0.7071*wet
    // => rms(out05 - 0.7071*out0) / rms(out1) == 0.7071  (wet gain)
    //    rms(out05 - 0.7071*out1) / rms(out0) == 0.7071  (dry gain)
    TapeMem echoEP[3];
    static AmbientReverb rvEP[3];
    Instrument inst[3];
    const float mixes[3] = { 0.f, 0.5f, 1.f };
    for (int k = 0; k < 3; ++k) {
        FxMem m;
        echoEP[k].bind(m);
        m.reverb = &rvEP[k];
        inst[k].init(48000.f, m);
        inst[k].set_reverb_mix(mixes[k]);
    }
    float l[3], r[3];
    for (int i = 0; i < 48000; ++i)                     // settle: gains + room fill
        for (int k = 0; k < 3; ++k) inst[k].process(nullptr, nullptr, &l[k], &r[k], 1);
    const float g = 0.70710678f;
    double accW = 0.0, acc1 = 0.0, accD = 0.0, acc0 = 0.0;
    for (int i = 0; i < 96000; ++i) {
        for (int k = 0; k < 3; ++k) inst[k].process(nullptr, nullptr, &l[k], &r[k], 1);
        float wet_half = l[1] - g * l[0];
        float dry_half = l[1] - g * l[2];
        accW += wet_half * wet_half;  acc1 += l[2] * l[2];
        accD += dry_half * dry_half;  acc0 += l[0] * l[0];
    }
    CHECK(std::sqrt(accW / acc1) == doctest::Approx(g).epsilon(0.02));
    CHECK(std::sqrt(accD / acc0) == doctest::Approx(g).epsilon(0.02));
}

TEST_CASE("instrument: reverb mix is per-deck (A wet-kills-dry while B stays dry)") {
    // Sends muted -> the shared room is silent, so we observe the DRY path only.
    // With A mix 1 (dry gain 0) and B mix 0 (dry gain 1) held SIMULTANEOUSLY:
    //   morph 0 (only A audible)  -> A's dry is killed  -> exact silence
    //   morph 1 (only B audible)  -> B's dry survives   -> non-zero energy
    // No single shared mix value could satisfy both at once (1 kills both, 0
    // keeps both), so this fails on the old shared-mix engine and passes on the
    // per-deck one.
    auto dry_energy = [](float morph) {
        Instrument fx;
        fx.init(48000.f, test_fx_mem());
        for (int p = 0; p < PART_COUNT; ++p)
            fx.set_fx_target_base(p, FXT_REV_SEND, 0.f);   // room stays silent
        fx.set_morph(morph);
        fx.set_reverb_mix(PART_A, 1.f);   // A fully wet -> A dry gone
        fx.set_reverb_mix(PART_B, 0.f);   // B fully dry
        float l = 0.f, r = 0.f;
        // Warm-up: Center's own MORPH smoother (30 ms, control-rate, untouched
        // by this task) boots at 0.5 and glides to the target over ~200 ms;
        // settle it before measuring so only the per-deck reverb-mix gains
        // (which snap instantly, see _rev_primed) are under test.
        for (int i = 0; i < 20000; ++i) fx.process(nullptr, nullptr, &l, &r, 1);
        double acc = 0.0;
        for (int i = 0; i < 48000; ++i) {
            fx.process(nullptr, nullptr, &l, &r, 1);
            acc += (double)l * l;
        }
        return acc;
    };
    CHECK(dry_energy(0.f) == 0.0);   // morph 0: A's dry killed by an exact-0 gain
    CHECK(dry_energy(1.f) > 0.0);    // morph 1: B's dry survives untouched
}

TEST_CASE("instrument M4.8: hard MIX jumps are smoothed (no zipper)") {
    TapeMem echoZ;
    static AmbientReverb rvZ;
    auto run_maxd = [&](bool stepped) {
        FxMem m;
        echoZ.bind(m);
        m.reverb = &rvZ;
        Instrument inst;
        inst.init(48000.f, m);                 // init() re-clears the shared statics
        float l = 0.f, r = 0.f, prev = 0.f, maxd = 0.f;
        for (int i = 0; i < 96000; ++i) {
            if (stepped && i == 48000) inst.set_reverb_mix(1.f);
            if (stepped && i == 72000) inst.set_reverb_mix(0.f);
            inst.process(nullptr, nullptr, &l, &r, 1);
            if (i > 0) { float d = std::fabs(l - prev); if (d > maxd) maxd = d; }
            prev = l;
        }
        return maxd;
    };
    float steady = run_maxd(false);
    float stepped = run_maxd(true);
    // an unsmoothed 0->1 gain jump would spike the per-sample delta far above
    // the drone's own; the 10 ms glide keeps it in the same ballpark
    CHECK(stepped < 2.f * steady + 0.01f);
}

TEST_CASE("instrument M4.8: MIX 0 sleeps the room, any MIX > 0 wakes it") {
    Instrument fx;
    fx.init(48000.f, test_fx_mem());
    float l, r;
    fx.process(nullptr, nullptr, &l, &r, 1);
    CHECK(!fx.reverb_asleep());            // boot mix 0.25: awake
    fx.set_reverb_mix(0.f);                // runtime fade-out -> sleep
    for (int i = 0; i < 14400; ++i) fx.process(nullptr, nullptr, &l, &r, 1);
    CHECK(fx.reverb_asleep());             // 0.3 s >> the 10 ms glide + 150 ms fade
    fx.set_reverb_mix(0.4f);
    CHECK(!fx.reverb_asleep());            // waking is immediate
}

TEST_CASE("instrument M4.8: waking from sleep starts with an empty room (no ghost tail)") {
    TapeMem echoGX, echoGY;
    static AmbientReverb rvGX, rvGY;
    FxMem mx, my;
    echoGX.bind(mx); echoGY.bind(my);
    mx.reverb = &rvGX; my.reverb = &rvGY;
    Instrument x; x.init(48000.f, mx);
    Instrument y; y.init(48000.f, my);
    x.set_reverb_decay(0.85f); y.set_reverb_decay(0.85f);  // a surviving ghost would ring loud
    // Y is the reference: sends muted from boot, MIX 0.5 from boot
    y.set_reverb_mix(0.5f);
    for (int p = 0; p < PART_COUNT; ++p) y.set_fx_target_base(p, FXT_REV_SEND, 0.f);
    float xl, xr, yl, yr;
    // phase 1 (1 s): X rings up a loud tail on the default sends
    for (int i = 0; i < 48000; ++i) { x.process(nullptr, nullptr, &xl, &xr, 1);
                                      y.process(nullptr, nullptr, &yl, &yr, 1); }
    // phase 2 (0.5 s): X mutes its sends and goes to sleep at MIX 0
    for (int p = 0; p < PART_COUNT; ++p) x.set_fx_target_base(p, FXT_REV_SEND, 0.f);
    x.set_reverb_mix(0.f);
    for (int i = 0; i < 24000; ++i) { x.process(nullptr, nullptr, &xl, &xr, 1);
                                      y.process(nullptr, nullptr, &yl, &yr, 1); }
    CHECK(x.reverb_asleep());
    // phase 3 (0.5 s settle): X wakes at MIX 0.5; gains glide and snap
    x.set_reverb_mix(0.5f);
    for (int i = 0; i < 24000; ++i) { x.process(nullptr, nullptr, &xl, &xr, 1);
                                      y.process(nullptr, nullptr, &yl, &yr, 1); }
    // both rooms are now empty and unfed, the part streams are identical:
    // any difference left would be X's pre-sleep tail — it must be GONE
    float maxd = 0.f;
    for (int i = 0; i < 24000; ++i) {
        x.process(nullptr, nullptr, &xl, &xr, 1);
        y.process(nullptr, nullptr, &yl, &yr, 1);
        float d = std::fabs(xl - yl); if (d > maxd) maxd = d;
    }
    CHECK(maxd == 0.f);
}

TEST_CASE("instrument M4.8: mix automation incl. sleep is deterministic end to end") {
    auto run = [] {
        TapeMem echoD;
        static AmbientReverb rvD;
        FxMem m;
        echoD.bind(m);
        m.reverb = &rvD;
        Instrument inst;
        inst.init(48000.f, m);            // init() re-clears the shared statics
        std::vector<float> out;
        float l[96], r[96];
        for (int b = 0; b < 500; ++b) {
            if (b == 100) inst.set_reverb_mix(0.8f);
            if (b == 250) inst.set_reverb_mix(0.f);   // sleeps mid-run
            if (b == 400) inst.set_reverb_mix(0.5f);  // wakes again
            inst.process(nullptr, nullptr, l, r, 96);
            for (int i = 0; i < 96; ++i) out.push_back(l[i]);
        }
        return out;
    };
    auto a = run(), b = run();
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) CHECK(a[i] == b[i]);
}

TEST_CASE("instrument: RST restarts both loops at the bar start") {
    // RST (reset_transport) is the bar-resync gesture: zero the downbeat AND
    // snap the lane phases to 0, so the loops restart on the new bar instead
    // of being dragged onto it by the grid servo.
    Instrument inst;
    inst.init(48000.f);
    inst.set_tempo_bpm(120.f);
    inst.set_sync(true);
    inst.set_step(PART_A, true, 12);
    inst.set_step(PART_B, true, 8);
    std::vector<float> l(1), r(1);
    for (int i = 0; i < 48000; ++i)                  // run ~1 s into the pattern
        inst.process(nullptr, nullptr, l.data(), r.data(), 1);
    inst.reset_transport();
    inst.process(nullptr, nullptr, l.data(), r.data(), 1);
    // the very next sample fires step 0 on both parts — a fresh downbeat
    CHECK(inst.lane_fired(PART_A, LANE_PITCH));
    CHECK(inst.lane_fired(PART_B, LANE_PITCH));
}

TEST_CASE("instrument: shared shuffle reaches both STEP parts") {
    Instrument inst;
    inst.init(48000.f);
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_rate(p, 1.f);       // 30 Hz: nominal 200-sample STEP spacing
        inst.set_density(p, 1.f);    // every composed boundary fires
    }
    inst.set_shuffle(1.f);           // the only shuffle call: shared surface
    inst.set_step(PART_A, true, 8);
    inst.set_step(PART_B, true, 8);

    std::vector<int> fires_a;
    std::vector<int> fires_b;
    float l = 0.f, r = 0.f;
    for (int sample = 0; sample < 4000 && fires_a.size() < 10; ++sample) {
        inst.process(nullptr, nullptr, &l, &r, 1);
        if (inst.lane_fired(PART_A, LANE_PITCH)) fires_a.push_back(sample);
        if (inst.lane_fired(PART_B, LANE_PITCH)) fires_b.push_back(sample);
    }

    REQUIRE(fires_a.size() >= 5);
    REQUIRE(fires_b.size() == fires_a.size());
    CHECK(fires_a == fires_b);

    int short_gap = fires_a[1] - fires_a[0];
    int long_gap = short_gap;
    for (size_t i = 2; i < fires_a.size(); ++i) {
        const int gap = fires_a[i] - fires_a[i - 1];
        if (gap < short_gap) short_gap = gap;
        if (gap > long_gap) long_gap = gap;
    }
    CHECK(long_gap == doctest::Approx(2.f * short_gap).epsilon(0.03));
}

TEST_CASE("instrument: shared shuffle leaves FLOW exact beside an active STEP sibling") {
    Instrument straight;
    Instrument shuffled;
    straight.init(48000.f);
    shuffled.init(48000.f);
    for (int p = 0; p < PART_COUNT; ++p) {
        straight.set_rate(p, 1.f);
        shuffled.set_rate(p, 1.f);
        straight.set_density(p, 1.f);
        shuffled.set_density(p, 1.f);
    }
    straight.set_shuffle(0.f);
    shuffled.set_shuffle(1.f);
    straight.set_step(PART_B, true, 8);
    shuffled.set_step(PART_B, true, 8);

    std::vector<float> output_straight;
    std::vector<float> output_shuffled;
    std::vector<int> step_fires_straight;
    std::vector<int> step_fires_shuffled;
    output_straight.reserve(4000);
    output_shuffled.reserve(4000);
    float al = 0.f, ar = 0.f, bl = 0.f, br = 0.f;
    for (int sample = 0; sample < 4000; ++sample) {
        straight.process(nullptr, nullptr, &al, &ar, 1);
        shuffled.process(nullptr, nullptr, &bl, &br, 1);
        output_straight.push_back(straight.lane_output(PART_A, LANE_PITCH));
        output_shuffled.push_back(shuffled.lane_output(PART_A, LANE_PITCH));
        if (straight.lane_fired(PART_B, LANE_PITCH))
            step_fires_straight.push_back(sample);
        if (shuffled.lane_fired(PART_B, LANE_PITCH))
            step_fires_shuffled.push_back(sample);
    }

    // PART_A remains FLOW and therefore bit-exact even while the same shared
    // control actively warps PART_B's STEP boundaries.
    CHECK(output_straight == output_shuffled);
    REQUIRE(step_fires_straight.size() >= 5);
    REQUIRE(step_fires_shuffled.size() == step_fires_straight.size());
    CHECK(step_fires_shuffled != step_fires_straight);

    int straight_short = step_fires_straight[1] - step_fires_straight[0];
    int straight_long = straight_short;
    for (size_t i = 2; i < step_fires_straight.size(); ++i) {
        const int gap = step_fires_straight[i] - step_fires_straight[i - 1];
        if (gap < straight_short) straight_short = gap;
        if (gap > straight_long) straight_long = gap;
    }
    // The nominal step is exactly 200 samples (8 steps of a 1600-sample cycle)
    // and process() adds the increment BEFORE testing the step index, so
    // boundary k is due at sample 200k-1. Measured fire samples:
    //   float   0, 200, 400, 599, 799, 999, 1199, 1399, 1599   (gaps 199..200)
    //   double  0, 199, 399, 599, 799, 999, 1199, 1399, 1600   (gaps 199..201)
    // The two accumulators miss in OPPOSITE places, which is why the window
    // widened on 2026-08-12 (spec modulation-pace) while timing got no worse.
    //
    // Interior boundaries: shuffle_step_index() is fed
    // static_cast<float>(_phase) (lane.cpp), and at SHUFFLE 0 the boundaries
    // are exactly k/8, so a phase within half a float-ulp of k/8 snaps onto it.
    // Double lands all seven on time that way -- it sits ~1e-14 short and the
    // cast absorbs that. Float sat 9.7e-8 short at k=1 and 2.1e-7 at k=2, some
    // 26x and 28x the snap window, so both fired a sample LATE; from k=3 on its
    // accumulation noise had drifted positive and it landed on time.
    //
    // The wrap goes the other way: `while (_phase >= 1.0)` reads the raw
    // accumulator, with no cast and therefore no snap, and 1600 rounded double
    // adds finish ~83 ulps below 1.0 -- so the cycle now closes at 1600, where
    // float's coarser drift had already carried it past 1.0 by 1599.
    //
    // Every boundary is still within one sample of nominal, which is all this
    // check is for: full shuffle below moves one by ~66 samples. The bound is
    // stated against nominal rather than as a spread, so a window that drifted
    // away from 200 as a block cannot satisfy it.
    CHECK(straight_short >= 199);
    CHECK(straight_long <= 201);
    CHECK(straight_long == doctest::Approx(200.f).epsilon(0.01));

    int shuffled_short = step_fires_shuffled[1] - step_fires_shuffled[0];
    int shuffled_long = shuffled_short;
    for (size_t i = 2; i < step_fires_shuffled.size(); ++i) {
        const int gap = step_fires_shuffled[i] - step_fires_shuffled[i - 1];
        if (gap < shuffled_short) shuffled_short = gap;
        if (gap > shuffled_long) shuffled_long = gap;
    }
    CHECK(shuffled_long == doctest::Approx(2.f * shuffled_short).epsilon(0.03));
}

TEST_CASE("instrument: set_color blooms the FLOW pad live, click-free") {
    Instrument inst;
    inst.init(48000.f);
    inst.set_density(0, 0.f);                      // no lane fires: pure drone
    float outL[64], outR[64];
    for (int i = 0; i < 750; ++i)                  // 1 s warmup: drone settles
        inst.process(nullptr, nullptr, outL, outR, 64);
    CHECK(inst.active_voices(0) >= 1);
    inst.set_color(0, 0.7f);                       // knob turned up, NO trigger
    float max_step = 0.f, prev = 0.f;
    bool first = true;
    for (int i = 0; i < 1500; ++i) {               // 2 s
        inst.process(nullptr, nullptr, outL, outR, 64);
        for (int k = 0; k < 64; ++k) {
            if (!first && std::fabs(outL[k] - prev) > max_step)
                max_step = std::fabs(outL[k] - prev);
            prev = outL[k];
            first = false;
        }
    }
    CHECK(inst.active_voices(0) >= 3);             // the pad bloomed
    CHECK(max_step < 0.5f);                        // no hard discontinuity
}

TEST_CASE("instrument: COLOR 0 stays bit-deterministic") {
    Instrument a, b;
    a.init(48000.f);
    b.init(48000.f);
    b.set_color(0, 0.f);                           // explicit 0 == untouched
    b.set_color(1, 0.f);
    float al[64], ar[64], bl[64], br[64];
    for (int i = 0; i < 1500; ++i) {
        a.process(nullptr, nullptr, al, ar, 64);
        b.process(nullptr, nullptr, bl, br, 64);
        for (int k = 0; k < 64; ++k) {
            CHECK(al[k] == bl[k]);                 // exact
            CHECK(ar[k] == br[k]);
        }
    }
}

TEST_CASE("instrument: control raster survives a block-size-agnostic call pattern") {
    // The raster lives in Part and advances per sample, so rendering the same
    // audio in 96-sample blocks and in 7-sample blocks must give the same
    // samples. If anything ever ties the tick to the host block boundary,
    // this is what catches it.
    auto render = [](size_t chunk, std::vector<float>& out) {
        Instrument inst;
        inst.init(48000.f);
        inst.set_tempo_bpm(120.f);
        for (int p = 0; p < PART_COUNT; ++p) {
            inst.set_depth(p, 1.f);
            inst.set_rate(p, 0.8f);
        }
        out.assign(4800, 0.f);
        std::vector<float> r(4800, 0.f);
        for (size_t i = 0; i < 4800; i += chunk) {
            const size_t n = std::min(chunk, size_t(4800) - i);
            inst.process(nullptr, nullptr, out.data() + i, r.data() + i, n);
        }
    };
    std::vector<float> a, b;
    render(96, a);
    render(7, b);
    for (size_t i = 0; i < a.size(); ++i) REQUIRE(a[i] == b[i]);
}

TEST_CASE("instrument: set_tempo_bpm guards a non-positive or non-finite bpm at the single door") {
    // Instrument::set_tempo_bpm is the one call both SuperModulator::
    // set_tempo_bpm and Flux::set_bpm go through -- both keep their own _bpm
    // and bypass Transport entirely, so guarding only Transport::set_bpm (as
    // it already was) left this door open: host/render/scenario.cpp forwards
    // an unvalidated scenario-file `bpm` straight into this method (task 12
    // finding 2). A guarded call must be a complete no-op, so a reference
    // instrument that only ever receives the one valid bpm is compared,
    // sample for sample, against a "dut" that also receives interleaved bad
    // calls -- the same bit-exact idiom already used for FLUX's other
    // unchanged-value guards (I3, see flux.h). SYNC is on so SuperModulator
    // actually divides by bpm (division_hz), which is the path that reacts.
    auto setup = [](Instrument& inst) {
        inst.init(48000.f);
        inst.set_sync(true);
        inst.set_tempo_bpm(120.f);
        inst.set_target_active(PART_A, LANE_PITCH, true);
        inst.set_rate(PART_A, 0.5f);
        inst.set_range(PART_A, 1.f);
    };
    Instrument ref, dut;
    setup(ref);
    setup(dut);

    std::vector<float> rl(96), rr(96), dl(96), dr(96);
    for (int block = 0; block < 50; ++block) {
        if (block == 10) {
            dut.set_tempo_bpm(0.f);
            dut.set_tempo_bpm(-30.f);
            dut.set_tempo_bpm(std::numeric_limits<float>::quiet_NaN());
        }
        ref.process(nullptr, nullptr, rl.data(), rr.data(), 96);
        dut.process(nullptr, nullptr, dl.data(), dr.data(), 96);
        for (int i = 0; i < 96; ++i) {
            REQUIRE(std::isfinite(dl[i]));
            REQUIRE(dl[i] == rl[i]);
            REQUIRE(dr[i] == rr[i]);
        }
    }
}

TEST_CASE("instrument FORM SONG API clamps values and advances both Parts independently") {
    Instrument inst;
    inst.init(48000.f);
    for (int part = 0; part < PART_COUNT; ++part) {
        inst.set_form(part, static_cast<int>(Principle::Hierarchical));
        inst.set_song(part, static_cast<int>(SongMode::AAAB));
        inst.set_rate(part, 1.f);
        inst.set_shape(part, 1.f);
        inst.set_density(part, 1.f);
        inst.set_step(part, true, part == PART_A ? 8 : 12);
    }

    std::vector<uint8_t> symbols[PART_COUNT];
    uint32_t previous_position[PART_COUNT] = {
        inst.song_position_for_test(PART_A),
        inst.song_position_for_test(PART_B)
    };
    for (int part = 0; part < PART_COUNT; ++part)
        symbols[part].push_back(inst.active_pattern_for_test(part));

    float left = 0.f;
    float right = 0.f;
    for (int sample = 0; sample < 200000; ++sample) {
        inst.process(nullptr, nullptr, &left, &right, 1);
        for (int part = 0; part < PART_COUNT; ++part) {
            const uint32_t position = inst.song_position_for_test(part);
            if (position != previous_position[part]) {
                previous_position[part] = position;
                symbols[part].push_back(
                    inst.active_pattern_for_test(part));
            }
        }
        if (symbols[PART_A].size() >= 8 &&
            symbols[PART_B].size() >= 8)
            break;
    }

    const std::vector<uint8_t> expected = {0, 0, 0, 1, 0, 0, 0, 1};
    REQUIRE(symbols[PART_A].size() >= expected.size());
    REQUIRE(symbols[PART_B].size() >= expected.size());
    CHECK(std::equal(expected.begin(), expected.end(),
                     symbols[PART_A].begin()));
    CHECK(std::equal(expected.begin(), expected.end(),
                     symbols[PART_B].begin()));
    CHECK(inst.form(PART_A) == static_cast<int>(Principle::Hierarchical));
    CHECK(inst.form(PART_B) == static_cast<int>(Principle::Hierarchical));
    CHECK(inst.song(PART_A) == static_cast<int>(SongMode::AAAB));
    CHECK(inst.song(PART_B) == static_cast<int>(SongMode::AAAB));

    Instrument clamped;
    clamped.init(48000.f);
    clamped.set_form(PART_A, -7);
    clamped.set_form(PART_B, 99);
    clamped.set_song(PART_A, -7);
    clamped.set_song(PART_B, 99);
    clamped.set_step(PART_A, true, 8);
    clamped.set_step(PART_B, true, 8);
    clamped.process(nullptr, nullptr, &left, &right, 1);
    CHECK(clamped.form(PART_A) ==
          static_cast<int>(Principle::TwoMotif));
    CHECK(clamped.form(PART_B) ==
          static_cast<int>(Principle::Ostinato));
    CHECK(clamped.song(PART_A) == static_cast<int>(SongMode::AAAB));
    CHECK(clamped.song(PART_B) == static_cast<int>(SongMode::Off));
}

// --- Task 10: cross-deck tap, audio input, and source selection -----------
//
// The brief's own test code for this task is a sketch that does not compile
// against this tree (task-10-brief-addendum.md, top) -- `Instrument::part(int)`
// does not exist, `trigger(int, float)` is really `trigger_manual(int)` with
// no pitch argument, and `process(...)` is the block-based 5-argument form
// used everywhere else in this file. Both cases below are written against
// the real API and against what the two claims (symmetric + off by default;
// bounded with everything hot) actually require to be verified, not against
// the brief's literal sketch.

TEST_CASE("cross-deck excitation is symmetric and off by default") {
    // Deck A on the synth, deck B on BODY, with no manual trigger of B's
    // own. Both engines boot in FLOW ("lanes boot in FLOW -> drone",
    // part.cpp), so deck A sustains a drone tone with no trigger_manual
    // needed, and deck B fires exactly one auto-drone pluck the instant its
    // engine-switch fade completes -- identical in both instruments below,
    // since neither the cross-deck flag nor SUB gates a note's OWN exciter
    // pluck (only the EXTERNAL bus term is SUB-gated -- body_voice.cpp,
    // BodyVoice::process: `_exciter.process() + (_sub > 0.f ? ... : 0.f)`).
    // set_voice_sub is pushed identically on both instruments so the ONLY
    // difference between them is the cross-deck flag (same isolation idiom
    // test_dust/test_rot above use).
    //
    // The settle loop waits for that one-off boot pluck to decay out of the
    // QUIET instrument specifically, rather than assuming a fixed sample
    // count -- DECAY's default mapping is tuning material this test has no
    // business depending on (same reasoning as test_part_fx.cpp's
    // warm_up_tape_tap). COUPLED is stepped in lockstep so the two stay
    // time-aligned for the comparison window that follows, and never gets
    // asked to decay itself -- the cross-deck tap keeps it fed on purpose.
    Instrument quiet, coupled;
    for (Instrument* inst : { &quiet, &coupled }) {
        inst->init(48000.f);
        inst->set_engine(PART_A, ENGINE_SYNTH);
        inst->set_engine(PART_B, ENGINE_BODY);
        inst->set_voice_sub(PART_B, 1.f);
        // Explicit, not incidental: this test is about cross-deck coupling,
        // not about detune, but BODY's own resonant character (how far
        // body_voice.cpp's kDetuneScale bends its mode bank) still shapes
        // how visibly a coupling energy shows up over background
        // self-oscillation -- so its detune must be a known, fixed point,
        // not whatever SynthEngineT::_detune_spread_ct's un-pushed boot
        // default (18 ct, a SYNTH-oriented number that was never chosen to
        // serve BODY -- see synth_engine.h) happens to produce on BODY today.
        // 72 ct is exactly what that 18 ct default DID produce on BODY
        // before the ceiling grew (kDetuneCeilCt 35 -> 105, kDetuneScale
        // 4 -> 4/3, spec 2026-08-09 hw-control-reduction task 10:
        // 18 * 4 = 72 old, 18 * 4/3 = 24 new) -- i.e. the BODY character
        // this test's margins were always, if incidentally, measured
        // against. Pinning it here (identically on both instruments, so it
        // stays a controlled variable) makes the dependency honest instead
        // of quietly re-deriving new thresholds around a moved default.
        inst->set_voice_detune(PART_B, 72.f / 140.f);
    }
    coupled.set_excitation_sources(PART_B, false, true, false);

    float l, r;
    int settle = 0;
    for (; settle < 480000 && quiet.voice_env(PART_B, 0) > 1e-4f; ++settle) {
        quiet.process(nullptr, nullptr, &l, &r, 1);
        coupled.process(nullptr, nullptr, &l, &r, 1);
    }
    // The premise this loop exists to establish -- that B's own boot pluck
    // really does decay away with the cross-deck source off -- must itself
    // be true, or a settle loop that "times out" every run would still let
    // the CHECK below pass for the wrong reason.
    REQUIRE(settle < 480000);

    double e_quiet = 0.0, e_coupled = 0.0;
    for (int i = 0; i < 24000; ++i) {
        quiet.process(nullptr, nullptr, &l, &r, 1);
        const float qe = quiet.voice_env(PART_B, 0);
        e_quiet += (double)qe * qe;

        coupled.process(nullptr, nullptr, &l, &r, 1);
        const float ce = coupled.voice_env(PART_B, 0);
        e_coupled += (double)ce * ce;
    }
    CHECK(e_coupled > e_quiet);

    // Stronger claim: B's elevated energy must depend on A specifically, not
    // merely on "some feedback loop exists". set_other_deck_tap wired to the
    // WRONG index (B fed its OWN delayed output instead of A's) is itself a
    // legitimate feedback loop through B's SUB-gated bus, and measurement
    // showed it is NOT distinguishable from the CHECK above alone
    // (task-10-report.md's mutation table) -- a single BODY deck feeding its
    // own dry output back into its own excitation bus is exactly the kind of
    // self-oscillation spec §6 says is intended and bounded. Silencing A and
    // requiring B's energy to fall back closes that gap: under the
    // self-feedback bug, muting A has no effect on B at all, because B was
    // never listening to A in the first place.
    coupled.set_target_active(PART_A, LANE_LEVEL, false);
    coupled.set_target_base(PART_A, LANE_LEVEL, 0.f);
    for (int i = 0; i < 24000; ++i)      // let A's ~10 ms level smoother settle
        coupled.process(nullptr, nullptr, &l, &r, 1);
    double e_after_mute = 0.0;
    for (int i = 0; i < 24000; ++i) {
        coupled.process(nullptr, nullptr, &l, &r, 1);
        const float ce = coupled.voice_env(PART_B, 0);
        e_after_mute += (double)ce * ce;
    }
    CHECK(e_after_mute < e_coupled);
}

// The trap this test guards against (task-10-brief-addendum.md section B):
// Instrument::init(sample_rate) alone builds NO FX chain, so FLUX can never
// engage and enabling the tape source would silently exercise nothing --
// this test would then prove a silent path is bounded and call it done. So:
// a real FxMem (test_fx_mem(), already used above in this file), FLUX
// actually switched on with a nonzero mix on both decks, and an assertion
// that the tape tap really did go live (Instrument::tape_tap(), added for
// this purpose) rather than merely that FLUX::set_on(true) was called.
//
// What it checks for boundedness is deliberately NOT the final master
// output: fx/limiter.h's shape() runs every sample through fast_tanh, which
// is itself hard-clamped to |y| <= 1 (util/fast_tanh.h) -- so |l| < N (for
// any N) on the POST-LIMITER signal would hold even if the excitation loop
// between the two decks were genuinely diverging underneath it. What can
// actually run away is the RAW resonator energy feeding that limiter --
// BodyVoice's own unclamped follower, read here via voice_env -- so that is
// what this test watches, both for non-finiteness and for gross magnitude.
//
// Mutation testing this claim (task-10-report.md has the full table) found
// the system's headroom against THIS specific two-deck loop is large: with
// the post-sum clip removed outright, peak_env plateaus around ~22-24
// (same order as the correct build) for a full 60 s rather than diverging
// -- BodyVoice's own resonance cap and the SUB^2 <= 0.5 gate already give
// this particular loop enough margin that the outer clip alone isn't what
// is keeping it stable here. A gain bug IS caught: scaling the summed bus
// by 5x (simulating e.g. a doubled/mis-added source) reaches peak_env in
// the thousands within this test's 10 s window and eventually goes
// non-finite past it. So: isfinite() is the claim this test can actually
// prove teeth for, and peak_env's bound below is a coarse canary sized well
// above the correct build's ~24 with real margin (not fitted to it) but
// nowhere near the gain-bug failure's ~10^3 -- it will not catch a subtler
// gain error, and the report says so plainly rather than implying it does.
TEST_CASE("two BODY decks with the bus hot stay bounded") {
    Instrument inst;
    inst.init(48000.f, test_fx_mem());
    inst.set_engine(PART_A, ENGINE_BODY);
    inst.set_engine(PART_B, ENGINE_BODY);
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_fx_on(p, FxBlock::Flux, true);
        inst.set_flux_mix(p, 1.f);
        inst.set_excitation_sources(p, true, true, true);
        inst.set_voice_sub(p, 1.f);
    }
    inst.trigger_manual(PART_A);
    inst.trigger_manual(PART_B);

    bool tape_a = false, tape_b = false;
    float peak_env = 0.f;
    float l = 0.f, r = 0.f;
    float inL = 0.3f, inR = 0.3f;
    for (int i = 0; i < 48000 * 10; ++i) {
        inst.process(&inL, &inR, &l, &r, 1);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
        const float ea = inst.voice_env(PART_A, 0);
        const float eb = inst.voice_env(PART_B, 0);
        REQUIRE(std::isfinite(ea));
        REQUIRE(std::isfinite(eb));
        if (ea > peak_env) peak_env = ea;
        if (eb > peak_env) peak_env = eb;
        if (inst.tape_tap(PART_A) != 0.f) tape_a = true;
        if (inst.tape_tap(PART_B) != 0.f) tape_b = true;
    }
    CHECK(tape_a);   // the tape source really was live, not just switched on
    CHECK(tape_b);
    // Coarse canary against a gross gain bug, not a proof of the post-sum
    // clip specifically -- see the mutation note above this test.
    CHECK(peak_env < 100.f);
}

// Neither test above independently pins the third source: the bounded test
// enables all three at once (so a broken audio-in term could hide behind the
// other two), and the cross-deck test's assertion is specific to the
// other_deck flag. Same isolation idiom as the cross-deck test, mirrored
// onto a single deck fed through Instrument::process's inL/inR instead of a
// sibling part.
TEST_CASE("audio input reaches the excitation bus and is off by default") {
    Instrument quiet, fed;
    for (Instrument* inst : { &quiet, &fed }) {
        inst->init(48000.f);
        inst->set_engine(PART_A, ENGINE_BODY);
        inst->set_voice_sub(PART_A, 1.f);
    }
    fed.set_excitation_sources(PART_A, false, false, true);

    float l, r;
    float inL = 0.4f, inR = 0.4f;
    int settle = 0;
    for (; settle < 480000 && quiet.voice_env(PART_A, 0) > 1e-4f; ++settle) {
        quiet.process(&inL, &inR, &l, &r, 1);
        fed.process(&inL, &inR, &l, &r, 1);
    }
    REQUIRE(settle < 480000);

    double e_quiet = 0.0, e_fed = 0.0;
    for (int i = 0; i < 24000; ++i) {
        quiet.process(&inL, &inR, &l, &r, 1);
        const float qe = quiet.voice_env(PART_A, 0);
        e_quiet += (double)qe * qe;

        fed.process(&inL, &inR, &l, &r, 1);
        const float fe = fed.voice_env(PART_A, 0);
        e_fed += (double)fe * fe;
    }
    CHECK(e_fed > e_quiet);
}

// Task 10 review (task-10-review.md), finding 3: the report's mutation 10
// only flipped the DEFAULT of _src_tape (true -> false), which
// tests/test_part.cpp's pre-existing Task 9 test catches independently --
// nothing ever set _src_tape = false on a deck whose tape is genuinely LIVE
// and required the tape to stop reaching the bus. Reproduced the reviewer's
// mutation myself first (part.cpp:381, `if (_src_tape) bus += ...` ->
// unconditional `bus += _fx.tape_tap();`): full suite stayed 785/785 green.
//
// Same real-FX-chain idiom as "two BODY decks... stay bounded" above
// (test_fx_mem(), FLUX on with a nonzero mix) so the tape source is provably
// live in BOTH renders -- REQUIRE(tape_live) below closes the addendum's §B
// trap the same way that test does, just landing on this source instead.
TEST_CASE("tape source is honoured: switching it off changes a live FLUX render") {
    auto render = [](bool tape) {
        Instrument inst;
        inst.init(48000.f, test_fx_mem());
        inst.set_engine(PART_A, ENGINE_BODY);
        inst.set_fx_on(PART_A, FxBlock::Flux, true);
        inst.set_flux_mix(PART_A, 1.f);
        inst.set_excitation_sources(PART_A, tape, false, false);
        inst.set_voice_sub(PART_A, 1.f);
        inst.trigger_manual(PART_A);
        bool tape_live = false;
        std::vector<float> out;
        out.reserve(48000);
        float l, r;
        for (int i = 0; i < 48000; ++i) {
            inst.process(nullptr, nullptr, &l, &r, 1);
            if (inst.tape_tap(PART_A) != 0.f) tape_live = true;
            out.push_back(l);
        }
        // The premise: FLUX really did engage in THIS render, tape flag or
        // not (tape_tap() reflects PartFx's own FLUX state, independent of
        // whether Part's bus sum is honouring _src_tape -- see instrument.h's
        // tape_tap() comment). Without this, a render pair that both happen
        // to have a dead FLUX chain would trivially compare equal for the
        // wrong reason.
        REQUIRE(tape_live);
        return out;
    };
    CHECK(render(true) != render(false));
}

// Task 10 review, finding 2: M1/M2 showed the whole post-sum DC-block +
// fast_tanh stage (part.cpp:_control_tick, the line after the three `if
// (_src_*)` adds) can be deleted and the two-BODY-deck "bus hot stays
// bounded" test never notices, because that scenario's own sources are
// self-limiting (the resonator's own damping, SUB^2 <= 0.5, and Task 9's
// per-source clip on the tape tap already keep it stable without any help
// from the post-sum stage). The reviewer's fix: stop trying to provoke
// instability and test the stage's actual CONTRACT directly -- soft
// clipping means the response to a 10x-louder drive is compressed, not
// proportional. Audio-in is the right source to drive this through because
// it is the one source with NO clip anywhere upstream of the post-sum stage
// (tape_tap() has Task 9's own clip; the cross-deck tap is a deck's dry
// output, plausibly loud but not adversarially so in this repo's other
// tests) -- so this is also the closest thing to a regression test for the
// exact failure scenario finding 1 names (an unclipped, unblocked source
// riding the bus at speaker-destroying levels).
//
// Driven with a 220 Hz tone (not a constant/DC level): _audio_in_tap
// captures one instantaneous sample per 96-sample control block, and with
// Important 1 now fixed the post-sum DcBlock's corner is a real ~1.6 Hz --
// a held DC input would just get removed, telling this test nothing. 220 Hz
// comfortably survives that highpass and is well below the 500 Hz rate the
// captured sequence is effectively sampled at.
TEST_CASE("audio-in excitation bus is soft-clipped, not proportional to drive") {
    auto measure_energy = [](float drive) {
        Instrument inst;
        inst.init(48000.f);
        inst.set_engine(PART_A, ENGINE_BODY);
        inst.set_voice_sub(PART_A, 1.f);
        inst.set_excitation_sources(PART_A, false, false, true);

        constexpr double kTwoPi = 6.283185307179586;
        constexpr double kFreqHz = 220.0;
        double phase = 0.0;
        const double dphase = kTwoPi * kFreqHz / 48000.0;
        float l, r;
        auto step = [&] {
            const float s = static_cast<float>(drive * std::sin(phase));
            phase += dphase;
            float inL = s, inR = s;
            inst.process(&inL, &inR, &l, &r, 1);
        };
        for (int i = 0; i < 48000; ++i) step();   // past the boot pluck, into steady state

        double e = 0.0;
        for (int i = 0; i < 24000; ++i) {
            step();
            const float ve = inst.voice_env(PART_A, 0);
            e += (double)ve * ve;
        }
        return e;
    };

    const double e_lo = measure_energy(0.4f);
    const double e_hi = measure_energy(4.0f);
    // Premise: the low-drive render genuinely excited the resonator (SUB is
    // open and the tone is above the DC block's corner) -- otherwise a ratio
    // computed against ~0 would pass or divide-by-zero for the wrong reason.
    REQUIRE(e_lo > 0.0);

    const double ratio = e_hi / e_lo;
    // A 10x amplitude increase with NO clip anywhere in the chain would
    // reach the resonator roughly proportionally, i.e. an ENERGY (squared)
    // ratio near 10^2 = 100 modulo the resonator's own dynamics. With the
    // post-sum fast_tanh in place, 4.0 sits past its |x| >= 3.646739 hard
    // clamp (returns exactly +-1) while 0.4 is barely compressed
    // (fast_tanh(0.4) ~= 0.380), an amplitude ratio of ~2.6 rather than 10,
    // energy ratio ~<7. 20 sits with real margin above the clipped case and
    // real margin below the unclipped one -- derived from the clip's own
    // arithmetic, not fitted to either measurement.
    CHECK(ratio < 20.0);
}

// Task 10 review, round 2: the 0.4-vs-4.0 test above binds fast_tanh (its
// own mutation table shows so) but is blind to the DC block -- a 220 Hz
// probe cannot tell a 1.6 Hz corner from a 0.017 Hz corner apart over that
// test's timescale, so neither "delete the DcBlock" nor "revert Important
// 1's calibration fix" moved its result. Same shape as test_part_fx.cpp's
// "tape_tap's DC block removes a sustained offset, fast_tanh alone cannot"
// (Task 9's equivalent claim, same 0.3 threshold, reused deliberately --
// see the derivation below): a genuinely sustained DC input, early_mean
// versus late_mean, read through excitation_eff() (part.h) so this watches
// the RAW post-clip bus directly rather than inferring the DC block's
// behaviour through resonator dynamics.
//
// Window/threshold, derived from the corner frequencies, not fitted to a
// run:
//   _bus_dc.Process() runs once per control TICK (kCtrlInterval = 96
//   samples @ 48 kHz = 2 ms), not once per sample. DcBlock's difference
//   equation (dcblock.cpp: out = in - input_ + gain*output_) fed a CONSTANT
//   input C settles to y[n] = gain^n * C -- pure geometric decay per TICK.
//     Correct calibration (Important 1): gain = 1 - 10/(48000/96)
//                                              = 1 - 10/500 = 0.98
//       tick-rate time constant tau = 1/(1-gain) = 50 ticks = 100 ms.
//     Reverted/miscalibrated: gain = 1 - 10/48000 ~= 0.999792
//       tau = 1/(1-gain) ~= 4800 ticks = 9.6 s -- the exact 96x the review
//       named (kCtrlInterval itself).
//   EARLY window: ticks ~2-7 (samples 200-700, 4-15 ms) -- close enough to
//   the offset first reaching the bus that decay is negligible under
//   EITHER calibration (correct: 0.98^5 ~= 0.90; miscalibrated: ~1.00), so
//   early_mean is a clean "the DC genuinely got here" reference for both.
//   LATE window: 300-500 ms (samples 14400-24000), chosen as 3x the
//   CORRECT tau -- at the correct rate that is 150-250 ticks in, decayed to
//   roughly 0.98^200 ~= e^-4 ~= 1.8% of early; at the miscalibrated rate
//   the SAME 300-500 ms is only ~0.03-0.05 tau, decayed to roughly
//   0.999792^200 ~= e^-0.042 ~= 96% of early -- barely moved. With the
//   DcBlock deleted outright, fast_tanh alone maps a constant to a constant
//   forever: 100% of early, forever. 0.3 -- test_part_fx.cpp's own
//   threshold for the identical shape of claim -- sits with wide margin
//   above ~1.8% and wide margin below both ~96% and 100%.
TEST_CASE("audio-in excitation bus: post-sum DC block decays a sustained offset, fast_tanh alone cannot") {
    Instrument inst;
    inst.init(48000.f);
    inst.set_engine(PART_A, ENGINE_BODY);
    inst.set_voice_sub(PART_A, 1.f);
    inst.set_excitation_sources(PART_A, false, false, true);

    double early_sum = 0.0, late_sum = 0.0;
    int early_n = 0, late_n = 0;
    float l, r;
    float inL = 0.5f, inR = 0.5f;   // constant, one-sided drive: a real DC offset
    for (int i = 0; i < 24000; ++i) {          // 500 ms
        inst.process(&inL, &inR, &l, &r, 1);
        if (i >= 200   && i < 700)   { early_sum += inst.excitation_bus(PART_A); ++early_n; }
        if (i >= 14400 && i < 24000) { late_sum  += inst.excitation_bus(PART_A); ++late_n;  }
    }
    const double early_mean = early_sum / early_n;
    const double late_mean  = late_sum  / late_n;
    MESSAGE("early_mean=", early_mean, " late_mean=", late_mean);
    CHECK(std::fabs(early_mean) > 0.05);                        // the DC genuinely reached the bus
    CHECK(std::fabs(late_mean) < std::fabs(early_mean) * 0.3);  // and the block pulls it toward 0
}

// ---- bloom duck (spec 2026-08-03-reverb-bloom-duck-design.md) ----

// A patch that reliably blooms: hot per-part sends, hot MIX, DECAY at the
// stop (110% loop gain). The parts' own generative output seeds the room.
static void duck_bloom_rig(Instrument& inst) {
    inst.init(48000.f, test_fx_mem());
    for (int p = 0; p < PART_COUNT; ++p)
        inst.set_fx_target_base(p, FXT_REV_SEND, 1.f);
    inst.set_reverb_mix(0.9f);
    inst.set_reverb_decay(1.f);
}

static void duck_render_blocks(Instrument& inst, int blocks) {
    std::vector<float> l(96), r(96);
    for (int i = 0; i < blocks; ++i)
        inst.process(nullptr, nullptr, l.data(), r.data(), 96);
}

TEST_CASE("instrument duck: a normal patch never ducks -- exactly 1.0") {
    Instrument inst;
    inst.init(48000.f, test_fx_mem());   // boot defaults: MIX 0.25, DECAY 0.55
    // Hot send + hot MIX, DECAY still at the boot default (sub-unity loop
    // gain -- no bloom): a loud but ordinary room, not a quiet one, so this
    // guard cannot pass merely because there was nothing for the duck to see.
    for (int p = 0; p < PART_COUNT; ++p) inst.set_fx_target_base(p, FXT_REV_SEND, 1.f);
    inst.set_reverb_mix(0.9f);
    std::vector<float> l(96), r(96);
    for (int i = 0; i < 2500; ++i) {     // 5 s
        inst.process(nullptr, nullptr, l.data(), r.data(), 96);
        CHECK(inst.duck_gain() == 1.f);  // ==, not Approx: bit-transparent
    }
}

TEST_CASE("instrument duck: a loud sub-unity room never ducks -- exactly 1.0") {
    Instrument inst;
    duck_bloom_rig(inst);
    inst.set_reverb_decay(0.75f);            // loop gain ~0.94: player's regime
    std::vector<float> l(96), r(96);
    float env_max = 0.f;
    for (int i = 0; i < 7500; ++i) {         // 15 s
        inst.process(nullptr, nullptr, l.data(), r.data(), 96);
        env_max = std::max(env_max, s_ti_reverb.return_level());
        CHECK(inst.duck_gain() == 1.f);      // ==: the regime bit, not the level, gates
    }
    // Honesty: the guard only means something if the room really crossed the
    // threshold. If this fails, make the rig louder -- do not delete the line.
    REQUIRE(env_max > 0.30f);
}

TEST_CASE("instrument duck: a bloom pulls the dry bus below 0.5") {
    Instrument inst;
    duck_bloom_rig(inst);
    std::vector<float> l(96), r(96);
    float min_gain = 1.f;
    for (int i = 0; i < 7500; ++i) {     // 15 s: swell + duck both engage
        inst.process(nullptr, nullptr, l.data(), r.data(), 96);
        min_gain = std::min(min_gain, inst.duck_gain());
    }
    // The rig's return envelope BREATHES (measured on this rig: ~0.39-0.58,
    // ~10 s period -- generative material; a wash fluctuates by nature), so
    // the gain at any single checkpoint straddles the line. The spec's
    // claim is "the gain falls below 0.5"; the render minimum pins exactly
    // that, and the fixed seeds make it deterministic.
    CHECK(min_gain < 0.5f);
}

TEST_CASE("instrument duck: sub-unity DECAY releases the duck even while the room is loud") {
    Instrument inst;
    duck_bloom_rig(inst);
    duck_render_blocks(inst, 7500);                  // 15 s: visibly ducked
    // Precondition only (the env breathes, ~0.39-0.58): any visible duck at
    // the disarm instant serves; the proof is the monotone rise below.
    REQUIRE(inst.duck_gain() < 0.9f);
    REQUIRE(s_ti_reverb.return_level() > 0.30f);     // env above the threshold
    inst.set_reverb_decay(0.75f);                    // loop gain ~0.94: player takes over
    float g = inst.duck_gain();
    const float g0 = g;
    bool monotone = true;
    float return_level_1s = 0.f;
    std::vector<float> l(96), r(96);
    for (int i = 0; i < 2000; ++i) {                 // 4 s
        inst.process(nullptr, nullptr, l.data(), r.data(), 96);
        if (inst.duck_gain() + 1e-9f < g) monotone = false;
        g = inst.duck_gain();
        if (i == 499) return_level_1s = s_ti_reverb.return_level();   // 1 s in, Task 3's own checkpoint
    }
    // The room is STILL over the threshold 1 s into the disarm (the peak
    // follower's release hasn't caught up there) -- this test's job is only
    // to guard the RELEASE RIDE (the gain climbs back, monotonically, once
    // DECAY drops the loop below unity), not to prove the regime bit itself:
    // over the full 4 s window the env also drains through kDuckThresh on
    // its own (measured on this rig: return_level 0.301 at i=1500, 0.261 at
    // i=1999), so a *plain* level duck would eventually rise here too --
    // that proof belongs to the steady-state test above ("a loud sub-unity
    // room never ducks"), which holds DECAY at 0.75 for the room's whole life
    // and never lets the env's own drain do the work. Pinning only the 1 s
    // checkpoint keeps this precondition honest without leaning on that
    // borrowed authority.
    CHECK(return_level_1s > 0.30f);
    // This guards the release ride, not the endpoint: from any breathing
    // g0 <= ~0.8, four seconds of the 4 s up-slew rise at least
    // (1-g0)*(1-1/e) > 0.1.
    CHECK(monotone);
    CHECK(g > g0 + 0.1f);

    // Eventual exact recovery (residual-form slew, _duck_residual persisted
    // as its own state and decayed by a pure multiply -- see instrument.h/
    // .cpp): kill the sends and drop into a sub-unity room, then render long
    // past the up time constant. Unlike the additive form g += c*(t-g) --
    // and unlike a residual reconstructed from an already-rounded gain each
    // sample, which is algebraically the same update and stalls the same
    // way -- the persisted residual has no addition in its per-sample decay,
    // so it is not bounded by the target's rounding grid: it keeps shrinking
    // until it underflows below half the target's ulp, at which point the
    // read-out g = target + residual rounds to exactly the target.
    for (int p = 0; p < PART_COUNT; ++p) inst.set_fx_target_base(p, FXT_REV_SEND, 0.f);
    inst.set_reverb_decay(0.5f);
    for (int i = 0; i < 45000; ++i)                  // 90 s post-disarm
        inst.process(nullptr, nullptr, l.data(), r.data(), 96);
    CHECK(inst.duck_gain() == 1.f);
}

TEST_CASE("instrument duck: the gain never steps -- per-sample delta is slew-bounded") {
    Instrument inst;
    duck_bloom_rig(inst);
    inst.set_reverb_decay(0.5f);            // start sub-unity: duck idle at 1.0
    duck_render_blocks(inst, 2500);         // 5 s of material into the room
    inst.set_reverb_decay(1.f);             // jump into the bloom regime
    std::vector<float> l(1), r(1);
    float prev = inst.duck_gain(), max_delta = 0.f;
    for (int i = 0; i < 48000 * 10; ++i) {  // 10 s, per-sample
        inst.process(nullptr, nullptr, l.data(), r.data(), 1);
        max_delta = std::max(max_delta, std::fabs(inst.duck_gain() - prev));
        prev = inst.duck_gain();
    }
    REQUIRE(inst.duck_gain() < 0.9f);       // the duck did engage during the window
    // 1-exp(-1/(1.5 s * 48k)) * (1-0.316) = 9.5e-6; bound leaves 2x slack.
    CHECK(max_delta < 2e-5f);
}

TEST_CASE("instrument duck: wet-solo bloom settles at the plateau, not the floor") {
    Instrument inst;
    duck_bloom_rig(inst);
    inst.set_reverb_mix(1.f);   // exact endpoint: dry gain 0, output is the return
    std::vector<float> l(96), r(96);
    float peak = 0.f;
    for (int i = 0; i < 7500; ++i) {        // 15 s; measure the last 2 s
        inst.process(nullptr, nullptr, l.data(), r.data(), 96);
        if (i >= 6500)
            for (int k = 0; k < 96; ++k)
                peak = std::max(peak, std::fabs(l[k]));
    }
    // The settled return sits ~0.5-0.6 (post ceiling, master transparent at
    // drive 0). If the duck ever multiplied the send or the return, this
    // lands near plateau * floor ~= 0.17.
    CHECK(peak > 0.35f);
}

static std::vector<float> duck_purity_trace(float decay_norm, float* duck_at_end) {
    Instrument inst;
    duck_bloom_rig(inst);
    inst.set_reverb_decay(decay_norm);
    std::vector<float> trace;
    std::vector<float> l(96), r(96);
    for (int i = 0; i < 7500; ++i) {
        inst.process(nullptr, nullptr, l.data(), r.data(), 96);
        trace.push_back(inst.excitation_bus(PART_A));
        trace.push_back(inst.excitation_bus(PART_B));
        trace.push_back(inst.deck_tap(PART_A, 0));
        trace.push_back(inst.deck_tap(PART_B, 1));
    }
    if (duck_at_end) *duck_at_end = inst.duck_gain();
    return trace;
}

TEST_CASE("instrument duck: excitation and deck taps are bit-identical with and without a duck") {
    float duck_quiet = 0.f, duck_bloom = 0.f;
    auto quiet = duck_purity_trace(0.55f, &duck_quiet);
    auto bloom = duck_purity_trace(1.f, &duck_bloom);
    REQUIRE(duck_quiet == 1.f);            // control render never ducked
    REQUIRE(duck_bloom < 0.9f);            // precondition: visibly ducked at the end
    REQUIRE(quiet.size() == bloom.size());
    for (size_t i = 0; i < quiet.size(); ++i)
        CHECK(quiet[i] == bloom[i]);       // ==, the decks never saw the duck
}

TEST_CASE("instrument duck: re-init forgets the duck") {
    Instrument inst;
    duck_bloom_rig(inst);
    duck_render_blocks(inst, 7500);
    REQUIRE(inst.duck_gain() < 0.9f);       // precondition: visibly ducked
    duck_bloom_rig(inst);                   // re-init: a new patch starts clean
    CHECK(inst.duck_gain() == 1.f);         // exactly, before any process()
}

TEST_CASE("instrument: closing REVERB MIX fades the tail instead of cutting it") {
    Instrument plain;                  // dry twin: same seeds, no FX memory
    plain.init(48000.f);
    Instrument fx;
    fx.init(48000.f, test_fx_mem());
    fx.set_reverb_mix(0.6f);
    float pl, pr, fl, fr;
    for (int i = 0; i < 96000; ++i) {  // 2 s: fill the room
        plain.process(nullptr, nullptr, &pl, &pr, 1);
        fx.process(nullptr, nullptr, &fl, &fr, 1);
    }
    REQUIRE(!fx.reverb_asleep());
    fx.set_reverb_mix(0.f);
    // |fx - plain| is the wet residual: past the 10 ms dry-gain glide the
    // dry paths are identical (duck disarmed — set_reverb_decay never ran).
    float win[32] = {};
    int slept_at = -1;
    for (int i = 0; i < 96000; ++i) {
        plain.process(nullptr, nullptr, &pl, &pr, 1);
        fx.process(nullptr, nullptr, &fl, &fr, 1);
        win[i % 32] = std::max(std::fabs(fl - pl), std::fabs(fr - pr));
        if (fx.reverb_asleep()) { slept_at = i; break; }
    }
    REQUIRE(slept_at > 0);
    // 1) the room may only sleep after the ~150 ms return fade has run
    //    (today the clear lands ~66-76 ms after the knob: FAIL)
    CHECK(slept_at > static_cast<int>(0.15f * 48000.f));
    // 2) whatever tail was left at the clear had been faded to nothing
    float worst = 0.f;
    for (float v : win) worst = std::max(worst, v);
    CHECK(worst < 0.005f);
}

TEST_CASE("pace: centre is exactly neutral, and both worlds scale") {
    spky::Instrument in;
    in.init(48000.f);
    in.set_tempo_bpm(120.f);
    for (int p = 0; p < 2; ++p) in.set_rate(p, 0.5f);

    float before[2][spky::LANE_COUNT];
    for (int p = 0; p < 2; ++p)
        for (int l = 0; l < spky::LANE_COUNT; ++l)
            before[p][l] = in.lane_rate_hz_for_test(p, l);

    // _pace starts at 1.f, and pace_mult(0.5f) is ALSO exactly 1.f -- so a
    // bare set_pace(0.5f) here would be a no-op on a fresh instrument (the
    // m == _pace early-out fires) and this case would pass trivially even
    // with a broken set_pace. Move pace away from centre first, so bringing
    // it back actually exercises pace_mult, the multiply in _update_rate,
    // and the fan-out to every lane -- then the return-to-centre check below
    // is a real bit-identity claim, not a vacuous one.
    in.set_pace(1.f);
    bool moved = false;
    for (int p = 0; p < 2; ++p)
        for (int l = 0; l < spky::LANE_COUNT; ++l)
            if (in.lane_rate_hz_for_test(p, l) != before[p][l]) moved = true;
    CHECK(moved);

    in.set_pace(0.5f);
    for (int p = 0; p < 2; ++p)
        for (int l = 0; l < spky::LANE_COUNT; ++l)
            CHECK(in.lane_rate_hz_for_test(p, l) == before[p][l]);  // EXACT

    // Free world.
    in.set_sync(false);
    in.set_pace(0.f);
    for (int p = 0; p < 2; ++p)
        for (int l = 0; l < spky::LANE_COUNT; ++l)
            CHECK(in.lane_rate_hz_for_test(p, l)
                  == doctest::Approx(before[p][l] / 32.f));

    // Grid world: the LANES and the TRANSPORT scale together, or the servo
    // pulls the lanes back onto the unstretched grid and wins. Checking only
    // transport_bpm_for_test() here cannot catch a synced SuperModulator that
    // stopped tracking PACE -- Center::set_tempo_bpm(_bpm * _pace) is a
    // separate call from SuperModulator::_update_rate's own synced branch,
    // and stays correct even if the lanes stop scaling (verified: reverting
    // just the synced branch's `* _pace`, per step 6, left every assertion
    // in this test passing). So this also pins a lane-rate baseline at the
    // grid's own neutral pace, and checks the lanes scale with it.
    in.set_sync(true);
    in.set_pace(0.5f);   // neutral: establishes the grid baseline at x1
    float grid_before[2][spky::LANE_COUNT];
    for (int p = 0; p < 2; ++p)
        for (int l = 0; l < spky::LANE_COUNT; ++l)
            grid_before[p][l] = in.lane_rate_hz_for_test(p, l);

    in.set_pace(1.f);
    CHECK(in.transport_bpm_for_test() == doctest::Approx(120.f * 4.f));
    for (int p = 0; p < 2; ++p)
        for (int l = 0; l < spky::LANE_COUNT; ++l)
            CHECK(in.lane_rate_hz_for_test(p, l)
                  == doctest::Approx(grid_before[p][l] * 4.f));

    in.set_pace(0.5f);
    CHECK(in.transport_bpm_for_test() == doctest::Approx(120.f));
    for (int p = 0; p < 2; ++p)
        for (int l = 0; l < spky::LANE_COUNT; ++l)
            CHECK(in.lane_rate_hz_for_test(p, l)
                  == doctest::Approx(grid_before[p][l]));
}

TEST_CASE("pace: a fresh instrument runs at x1") {
    spky::Instrument in;
    in.init(48000.f);
    CHECK(in.pace_for_test() == 1.0f);
}

TEST_CASE("pace: a non-finite request is dropped, not propagated") {
    spky::Instrument in;
    in.init(48000.f);
    in.set_tempo_bpm(120.f);
    in.set_rate(0, 0.5f);
    const float ok = in.lane_rate_hz_for_test(0, spky::LANE_PITCH);
    in.set_pace(std::numeric_limits<float>::quiet_NaN());
    // Without the guard NaN reaches _base_hz, set_rate_hz maps it to 0, and
    // both decks go silent with no error at all.
    CHECK(in.lane_rate_hz_for_test(0, spky::LANE_PITCH) == ok);
}

TEST_CASE("pace: set_tempo_bpm does not compound the pace factor") {
    // _bpm must stay RAW. An implementation that folds pace into _bpm inside
    // set_tempo_bpm (e.g. `_bpm = bpm * _pace;`) can still land on the right
    // transport BPM on a single call by coincidence -- the invariant this
    // pins is that a SECOND call at the SAME raw bpm must not drift, which a
    // compounding implementation cannot satisfy no matter what the first call
    // happened to produce.
    spky::Instrument in;
    in.init(48000.f);
    in.set_pace(1.f);   // x4
    in.set_tempo_bpm(100.f);
    CHECK(in.transport_bpm_for_test() == doctest::Approx(400.f));
    in.set_tempo_bpm(100.f);   // same raw bpm again: must still be 400, not 1600
    CHECK(in.transport_bpm_for_test() == doctest::Approx(400.f));
}

TEST_CASE("pace: turns per unit time follow the commanded rate, through the whole stack") {
    // Every rate observer in this engine (lane_rate_hz_for_test included)
    // reports the COMMANDED rate, which stays perfectly correct while the
    // phase accumulator is frozen solid (lane.h's _phase comment on the
    // measured float stalls) -- so a gate built on Hz alone would pass a
    // stalled lane green. This one counts real motion, and drives it
    // through Instrument::process() rather than a bare ModLane (Task 3's
    // gate, tests/test_lane.cpp), so a grid-servo fight would show up too,
    // not just a lane exercised in isolation. It does NOT, by itself, catch
    // a missing set_pace fan-out (instrument.cpp): _rate_hz and _phase_inc
    // both derive from the SAME _base_hz (super_modulator.cpp), so a broken
    // fan-out moves the observed hz and the real accumulation together --
    // the independent hz-vs-pace_mult cross-check below is what closes
    // that hole.
    //
    // It counts TURNS (wraps + the still-fractional remainder of phase at
    // the end of the window), not raw wraps. Raw wraps are integers, and at
    // the low end of PACE that loses too much resolution to mean anything:
    // knob 0.f puts the pitch lane at ~0.008 Hz, so even this window owes
    // well under one full wrap (0.008 * 60 =~ 0.48), and a stalled lane
    // would report the same "0 wraps" a slow-but-healthy lane does. A wider
    // window does not fix this on wraps alone either -- knob 0.25 already
    // owes ~2.7 wraps here, so a 5% relative tolerance on wraps needs the
    // count near 20+ before +-1 wrap (the coarsest error rounding a
    // continuous quantity to an integer can produce) fits inside it, which
    // for knob 0.f alone needs a several-hundred-second window. Folding the
    // fractional phase back in turns wraps from an integer into a continuous
    // quantity -- exactly how Task 3's lane-level gate solved the identical
    // problem -- so one uniform 60 s window resolves every knob position,
    // including the slowest.
    //
    // The turns tolerance below assumes FLOW (not STEP) and EVOLVE off:
    // rate_hz_for_test() (lane.h) reports the rate BEFORE clock_scale(),
    // while _phase_inc (lane.cpp) is built WITH it, so a STEP lane with
    // STEPS != 8 would make "commanded" wrong by a factor of 8/steps, not a
    // percent -- and a nonzero _ev_rate walk feeds directly into the same
    // per-sample add (lane.cpp's process()). Both are pinned explicitly
    // below rather than left as unstated init defaults.

    // Unity reference for the independent hz cross-check: pace_mult(0.5f)
    // is exactly 1.f (divisions.h), so this is free_hz(0.35f) with PACE
    // taken all the way out of the computation.
    float unity_hz;
    {
        spky::Instrument u;
        u.init(48000.f);
        u.set_sync(false);
        u.set_tempo_bpm(120.f);
        u.set_rate(0, 0.35f);
        u.set_variation(0, 0.f);
        u.set_pace(0.5f);
        unity_hz = u.lane_rate_hz_for_test(0, spky::LANE_PITCH);
    }

    for (float knob : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
        spky::Instrument in;
        in.init(48000.f);
        in.set_sync(false);
        in.set_tempo_bpm(120.f);
        in.set_rate(0, 0.35f);
        in.set_variation(0, 0.f);      // pin EVOLVE off -- see comment above
        in.set_pace(knob);
        REQUIRE_FALSE(in.step_mode_for_test(0));  // pin FLOW -- see comment above
        const float hz = in.lane_rate_hz_for_test(0, spky::LANE_PITCH);

        // Independent cross-check: recompute the expected rate from a hz
        // measured at a DIFFERENT knob (0.5f) times pace_mult(knob), a pure
        // function of knob alone that shares no code path with _base_hz.
        // Deleting the set_pace fan-out leaves SuperModulator's _pace at 1
        // regardless of knob, so `hz` stops tracking `expected_hz` here even
        // though it would still agree with itself in the turns check below.
        const float expected_hz = unity_hz * pace_mult(knob);
        CAPTURE(knob); CAPTURE(hz); CAPTURE(expected_hz);
        // 0.01%: both sides are a single float multiply of the same inputs
        // (free_hz(0.35f) and pace_mult(knob)), so healthy agreement is
        // within a handful of float ULPs -- nowhere near the >=2x errors
        // (knob 0.f is 32x off) a deleted fan-out produces.
        CHECK(hz == doctest::Approx(expected_hz).epsilon(1e-4));

        const int secs = 60;
        const uint32_t w0 = in.lane_wraps_for_test(0, spky::LANE_PITCH);
        const double phase_start = double(in.lane_phase_for_test(0, spky::LANE_PITCH));

        // drive the engine, not the lane, so the whole fan-out is exercised
        std::vector<float> l(256), r(256);
        for (int i = 0; i < 48000 * secs / 256; ++i)
            in.process(nullptr, nullptr, l.data(), r.data(), 256);

        const uint32_t wraps = in.lane_wraps_for_test(0, spky::LANE_PITCH) - w0;
        const double phase_end = double(in.lane_phase_for_test(0, spky::LANE_PITCH));
        const double turns     = double(wraps) + (phase_end - phase_start);
        const double commanded = double(hz) * double(secs);
        CAPTURE(commanded); CAPTURE(turns);
        // +-0.1%: the real noise floor here is float quantization in
        // phase() (lane.h's static_cast<float> of the double accumulator),
        // not the double accumulation itself (which lands within ~2e-8
        // relative of commanded, measured) -- so the floor is closer to
        // float epsilon, ~1e-7 relative. 0.1% is about four orders of
        // magnitude above that floor, and still well under every measured
        // broken-_phase deviation (0.05%-2.5%, worst at the slowest knobs).
        CHECK(turns > commanded * 0.999);
        CHECK(turns < commanded * 1.001);
    }
}
