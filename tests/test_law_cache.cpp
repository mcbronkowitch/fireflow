// The control law's sent-value cache (control/control_law.h) against the law
// without it: two real Instruments, one driven by each, must render the same
// bits. Both hosts' tick divisions -- 96 (the firmware: Center::update
// rebuilds every lane at the top of each block) and 16 (VCV: the law's push
// lands mid-block) -- in every scenario.
#include <doctest/doctest.h>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "control/control_law.h"
#include "instrument.h"

using namespace spky;

namespace {

constexpr float kSr = 48000.f;

// The Instrument with two of its cached setters counted, so the gate can
// show the cache really skipped. The law is a template on the instrument and
// these hide the base's for it.
struct Counted : Instrument {
    long decays = 0, mixes = 0;
    void set_voice_decay(int p, float n) { ++decays; Instrument::set_voice_decay(p, n); }
    void set_reverb_mix(int p, float n)  { ++mixes;  Instrument::set_reverb_mix(p, n); }
};

// One side of the comparison: its own instrument, its own FX and sampler
// memory (two instruments must not share an echo or a room), its own law.
struct Side {
    std::vector<float> echo[PART_COUNT][2];
    std::vector<float> bbd[PART_COUNT][2];
    std::vector<SampleBuffer::Frame> smp[PART_COUNT];
    std::unique_ptr<AmbientReverb> reverb = std::make_unique<AmbientReverb>();
    std::unique_ptr<Counted> inst = std::make_unique<Counted>();
    control::ControlLawT<Counted> law;

    explicit Side(bool cache) {
        law.set_cache_enabled_for_test(cache);
        for (int p = 0; p < PART_COUNT; ++p) {
            for (int ch = 0; ch < 2; ++ch) {
                echo[p][ch].assign(Flux::kMaxSamples, 0.f);
                bbd[p][ch].assign(BbdEngine::kCells, 0.f);
            }
            smp[p].assign(static_cast<size_t>(2 * kSr), SampleBuffer::Frame{0.f, 0.f});
        }
        init();
        law.on_restore();
    }
    // Instrument::init plus the same material in both sampler buffers, so a
    // sampler deck has something to play.
    void init() {
        FxMem m;
        for (int p = 0; p < PART_COUNT; ++p) {
            for (int ch = 0; ch < 2; ++ch) {
                m.echo[p][ch] = echo[p][ch].data();
                m.bbd[p][ch]  = bbd[p][ch].data();
            }
            m.sampler_buf[p] = smp[p].data();
        }
        m.sampler_frames = smp[0].size();
        m.reverb = reverb.get();
        inst->init(kSr, m);
        std::vector<float> l(static_cast<size_t>(kSr)), r(l.size());
        uint32_t s = 0x1234567u;
        for (size_t i = 0; i < l.size(); ++i) {
            s = s * 1664525u + 1013904223u;
            const float noise = static_cast<float>(s >> 8) / 8388608.f - 1.f;
            const float env = std::exp(-3.f * static_cast<float>(i % 12000) / 12000.f);
            l[i] = 0.4f * env * std::sin(0.031f * static_cast<float>(i)) + 0.1f * env * noise;
            r[i] = 0.4f * env * std::sin(0.029f * static_cast<float>(i)) - 0.1f * env * noise;
        }
        for (int p = 0; p < PART_COUNT; ++p) inst->load_sample(p, l.data(), r.data(), l.size());
    }
};

enum Scen { S_INIT, S_ONE, S_ALL, S_ENGINE, S_SONG, S_DRIFT, S_REINIT, S_COUNT };
const char* const kScenName[S_COUNT] = {
    "init patch, steady", "one knob ramp (DECAY_A)", "all knobs moving",
    "engine switches on both decks (BBD, Sampler with REC, test tone)",
    "SONG rung changes", "DRIFT settles under GROW", "restore, reset and re-init mid-run"};

bool is_depth(int id) { return id >= ffctl::MODD_SOURCE_A && id <= ffctl::MODD_TIDE; }

// The knobs and options at time t (seconds) of a scenario.
void knobs_at(int scen, float t, float* k, control::Options& opt) {
    using namespace ffctl;
    for (int i = 0; i < NUM_PARAMS; ++i) k[i] = kInitParamDefaults[i];
    opt = control::Options{};
    const float w = 6.2831853f;
    switch (scen) {
    case S_INIT: break;
    case S_ONE:
        k[DECAY_A] = kInitParamDefaults[DECAY_A] + 0.2f * std::sin(w * t / 10.f);
        break;
    case S_ALL:
        // Every continuous knob, depths included, each on its own slow sine.
        for (int i = 0; i < NUM_PARAMS; ++i) {
            const ParamRange& r = kParamRange[i];
            if (r.snap || i == MODBTN || i == DRIFT) continue;
            const float span = (r.hi - r.lo) * (is_depth(i) ? 0.3f : 0.05f);
            const float v = kInitParamDefaults[i]
                            + span * std::sin(w * t / (7.f + 0.13f * float(i)) + float(i));
            k[i] = std::fmin(r.hi, std::fmax(r.lo, v));
        }
        break;
    case S_ENGINE: {
        // Deck A: FEED -> SYNTH -> SAMPLER (test tone for a second) -> BBD ->
        // WAVE -> BODY. Deck B: WAVE -> BBD -> SAMPLER (REC for a second) ->
        // FEED -> SYNTH -> SAMPLER. The engine-conditional faces move too.
        const float a[] = {5, 0, 1, 4, 2, 3};
        const float b[] = {2, 4, 1, 5, 0, 1};
        k[ENGINE_A] = a[std::min(5, static_cast<int>(t / 3.3f))];
        k[ENGINE_B] = b[std::min(5, static_cast<int>((t + 1.6f) / 3.3f))];
        opt.deck[0].test_tone = t >= 7.5f && t < 8.5f;
        k[REC_B] = (t >= 8.f && t < 9.f) ? 1.f : 0.f;
        opt.deck[1].excite_other_deck = t >= 5.f;
        k[STAGES_A] = 0.5f + 0.4f * std::sin(w * t / 5.f);
        k[SUB_A]    = 0.3f + 0.2f * std::sin(w * t / 6.f);
        k[DETUNE_B] = 0.4f + 0.3f * std::sin(w * t / 4.f);
        k[MELODY_A] = 0.5f * std::sin(w * t / 8.f);
        k[STEPS_B]  = t >= 12.f ? 8.f : 0.f;
        break;
    }
    case S_SONG:
        k[SONG_A] = static_cast<float>(static_cast<int>(t / 1.5f) % kSongLadderCount);
        k[SONG_B] = static_cast<float>(static_cast<int>(t / 2.3f) % kSongLadderCount);
        k[ENGINE_B] = t >= 10.f ? 1.f : 2.f;       // rung changes on a sampler deck punch
        k[STEPS_A] = 8.f;
        break;
    case S_DRIFT:
        // GROW and fast free lanes, so _ev_rate walks and the slews matter;
        // RANGE up, so the PITCH lane's slew is audible.
        k[RATE_A] = 0.85f; k[RATE_B] = 0.8f; k[COUPLE] = 0.3f; k[PACE] = 0.9f;
        k[MELODY_A] = 0.9f; k[MELODY_B] = 0.9f; k[RANGE_A] = 1.f; k[RANGE_B] = 1.f;
        k[SMOOTH_A] = 0.4f;
        // From 14 s deck A runs a fast STEP clock under GROW and SMOOTH: its
        // PITCH lane wraps mid-block and walks _ev_rate there, which only the
        // retiming setters' refresh_slew() picks up at a 16-sample tick (the
        // setup tests/test_skip_unchanged.cpp measured). The cache must leave
        // those setters alone for this to stay bit-identical.
        if (t >= 14.f) {
            k[STEPS_A] = 8.f; k[STEPS_B] = 12.f; k[RATE_A] = 0.9f;
            k[MELODY_A] = 1.f; k[MELODY_B] = 0.95f; k[SMOOTH_A] = 0.6f;
            k[COUPLE] = 1.f; k[TEMPO] = 0.5f; k[PACE] = 0.4f; k[TIDE] = 0.8f;
            k[DRIFT] = 0.f;
        }
        k[DRIFT] = (t >= 5.f && t < 7.f) || (t >= 12.f && t < 12.5f) ? 0.f : 0.6f;
        break;
    case S_REINIT:
        k[SHAPE_A] = t >= 5.f ? 0.7f : 0.2f;      // a jump across the restore
        k[ENGINE_A] = t >= 5.f ? 4.f : 5.f;
        k[DRIFT] = t >= 15.f ? 0.01f : 0.5f;      // a settle across the reset
        k[SONG_B] = t >= 15.f ? 6.f : 0.f;
        break;
    }
}

struct Outcome { long diffs = 0, lane_diffs = 0, blocks = 0; float peak = 0.f; };

Outcome run(int scen, int div, Side& ref, Side& dut, float seconds) {
    Outcome o;
    float k[ffctl::NUM_PARAMS];
    control::Options opt;
    std::vector<float> il(div, 0.f), ir(div, 0.f), aL(div), aR(div), bL(div), bR(div);
    const long blocks = static_cast<long>(seconds * kSr) / div;
    for (long b = 0; b < blocks; ++b) {
        const float t = static_cast<float>(b * div) / kSr;
        if (scen == S_REINIT && b * div == static_cast<long>(5 * kSr)) {
            ref.law.on_restore(); dut.law.on_restore();
        }
        if (scen == S_REINIT && b * div == static_cast<long>(10 * kSr)) {
            // VCV's sample-rate change: Instrument::init() without a restore.
            ref.init(); dut.init();
            ref.law.on_instrument_init(); dut.law.on_instrument_init();
        }
        if (scen == S_REINIT && b * div == static_cast<long>(15 * kSr)) {
            ref.law.on_reset(); dut.law.on_reset();
        }
        knobs_at(scen, t, k, opt);
        ref.law.tick(k, opt, *ref.inst);
        dut.law.tick(k, opt, *dut.inst);
        ref.inst->process(il.data(), ir.data(), aL.data(), aR.data(), static_cast<size_t>(div));
        dut.inst->process(il.data(), ir.data(), bL.data(), bR.data(), static_cast<size_t>(div));
        for (int i = 0; i < div; ++i) {
            o.peak = std::fmax(o.peak, std::fabs(aL[i]));
            o.diffs += std::memcmp(&aL[i], &bL[i], sizeof(float)) != 0;
            o.diffs += std::memcmp(&aR[i], &bR[i], sizeof(float)) != 0;
        }
        for (int p = 0; p < PART_COUNT; ++p)
            for (int l = 0; l < LANE_COUNT; ++l) {
                const float va = ref.inst->lane_value_for_test(p, l);
                const float vb = dut.inst->lane_value_for_test(p, l);
                o.lane_diffs += std::memcmp(&va, &vb, sizeof(float)) != 0;
            }
        ++o.blocks;
    }
    return o;
}

} // namespace

TEST_CASE("law cache: audio and lanes are bit-identical to the law without it") {
    int div = 0;
    SUBCASE("law every 96 samples (firmware)") { div = 96; }
    SUBCASE("law every 16 samples (VCV)")      { div = 16; }
    for (int scen = 0; scen < S_COUNT; ++scen) {
        const std::string name = kScenName[scen];
        CAPTURE(name);
        CAPTURE(div);
        Side ref(false), dut(true);
        const Outcome o = run(scen, div, ref, dut, 20.f);
        CHECK(o.blocks == static_cast<long>(20.f * kSr) / div);
        CHECK(o.diffs == 0);
        CHECK(o.lane_diffs == 0);
        CHECK(o.peak > 0.01f);                   // compared sound, not silence
        if (scen == S_ENGINE) {                  // the last switches did land
            CHECK(dut.inst->engine_id(PART_A) == ENGINE_BODY);
            CHECK(dut.inst->engine_id(PART_B) == ENGINE_SAMPLER);
        }
        // Not vacuous: the reference pushes DECAY and the reverb mixes every
        // tick, the cache only on a change or after a forget.
        CHECK(ref.inst->decays == 2 * o.blocks);
        if (scen != S_ALL) CHECK(dut.inst->mixes < ref.inst->mixes / 4);
        if (scen != S_ALL && scen != S_ONE) CHECK(dut.inst->decays < ref.inst->decays / 4);
    }
}
