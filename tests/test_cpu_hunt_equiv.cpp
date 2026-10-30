// The CPU hunt's adopted class-1 candidates 4 and 5 (engine/util/probe_toggles.h):
// each is compared bit for bit with the path it replaced.
#include <doctest/doctest.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include "instrument.h"
#include "parts/part.h"
#include "parts/bbd_engine.h"
#include "fx/flux.h"
#include "fx/tape_echo.h"
#include "util/fast_tanh.h"

using namespace spky;

namespace {

// xorshift32 noise, -1..1 (the seed is already well mixed).
struct Noise {
    uint32_t s = 2463534242u;
    float next() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float)(int32_t)s * (1.f / 2147483648.f);
    }
};
uint32_t bits_of(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

// ---- candidate 4: the tape echoes share their line bookkeeping -----------

struct TapeMem {
    std::vector<float> l, r;
    TapeMem() : l(Flux::kMaxSamples), r(Flux::kMaxSamples) {}
};

// The tape echo as it was before the two channels shared their bookkeeping:
// one InjectedDelayLine and one band-pass per channel, SetDelay / Read / bpf /
// tanh / Write per sample. A test-local copy of that arithmetic (the way
// test_grit.cpp keeps the daisysp Decimator), so the pair is compared with
// something that cannot drift along with it.
template <size_t N>
struct MonoEchoRef {
    InjectedDelayLine<float, N> line;
    TapeBpf bpf;
    float feedback = 0.f;
    float process(float in, float delay_samples) {
        line.SetDelay(delay_samples);
        float out = bpf.Process(line.Read());
        out = fast_tanh(out);
        line.Write(out * feedback + in);
        return out;
    }
};

// Pair vs two mono references over `total` samples. The line is small enough
// (kN cells) that the run saturates _filled (past kMask) several times over,
// the delay sweeps through both of SetDelay's clamps (below 1 sample and above
// kN - 2, the upper one for a stretch), feedback 1.2 (the loop's maximum),
// and both lines are Reset mid-run (what flush_lines does). Honest limit: a
// slip in the saturating count itself (nf < kMask) is not observable here --
// the delay never exceeds kMask - 1, and at that clamp frac is 0 -- so the
// run covers the saturated state, not a test for that one comparison.
int pair_vs_mono(float feedback, int total) {
    constexpr size_t kN = 4096;
    static float ma[kN], mb[kN], ra[kN], rb[kN];
    TapeEcho<kN> ea, eb;
    ea.Init(48000.f, ma);
    eb.Init(48000.f, mb);
    ea.SetFeedback(feedback);
    eb.SetFeedback(feedback);
    MonoEchoRef<kN> xa, xb;
    xa.line.Init(ra);
    xb.line.Init(rb);
    xa.bpf.Init(48000.f);
    xb.bpf.Init(48000.f);
    xa.feedback = xb.feedback = feedback;
    Noise n;
    int mismatches = 0;
    double acc = 0.0;
    for (int i = 0; i < total; ++i) {
        // 0.3 .. 5200 samples: crosses the lower clamp, the whole range, and
        // sits above kN - 2 = 4094 for a stretch of every sweep.
        const float ph = 0.5f + 0.5f * std::sin(i * (2.f * 3.14159265f / 16411.f));
        float d = 0.3f + 5200.f * ph * ph;
        if (i > total / 2 && i < total / 2 + 3000) d = 9000.f;   // pinned at the clamp
        if (i == total / 3 || i == (total * 3) / 4) {            // flush_lines
            ea.Reset(); eb.Reset(); xa.line.Reset(); xb.line.Reset();
            xa.bpf.Reset(); xb.bpf.Reset();
        }
        const float ia = n.next() * 0.5f, ib = n.next() * 0.5f;
        float oa, ob;
        TapeEcho<kN>::ProcessPair(ea, eb, ia, ib, d, oa, ob);
        const float wa = xa.process(ia, d), wb = xb.process(ib, d);
        if (bits_of(oa) != bits_of(wa)) ++mismatches;
        if (bits_of(ob) != bits_of(wb)) ++mismatches;
        acc += (double)oa * oa + (double)ob * ob;
    }
    REQUIRE(acc > 1.0);
    return mismatches;
}

// ---- bit 5: LAW_GUARDS ---------------------------------------------------

struct InstMem {
    std::vector<float> echo[PART_COUNT][2];
    std::vector<float> bbd[PART_COUNT][2];
    InstMem() {
        for (int p = 0; p < PART_COUNT; ++p)
            for (int c = 0; c < 2; ++c) {
                echo[p][c].assign(Flux::kMaxSamples, 0.f);
                bbd[p][c].assign(BbdEngine::kCells, 0.f);
            }
    }
    FxMem fx() {
        FxMem m;
        for (int p = 0; p < PART_COUNT; ++p)
            for (int c = 0; c < 2; ++c) {
                m.echo[p][c] = echo[p][c].data();
                m.bbd[p][c] = bbd[p][c].data();
            }
        return m;
    }
};

// The host's pattern: every block, push the STEP state, then process. Events
// along the way: tempo changes, a PACE turn, STEP in and out of FLOW, deck A
// swapped away from BBD and back. `guards` false pushes everything the way the
// code did before the unchanged-value guards (set_unguarded_for_test).
//
// `shift` samples of pre-roll move the host's block boundaries against the
// engine's 96-sample control tick raster, so the set_step pushes land at
// different tick phases. (An engine swap sets _ctrl_ctr = 0 and the tick runs
// on the same sample, so a swap's set_cycle() is always followed by a
// set_targets() before the next push; the shifts vary the phase of every
// other event against the tick, not that one. That case is held by the
// engine-level test below, where the pushes are placed by hand.)
std::vector<float> render_instrument(bool guards, bool nonbbd, int* recomputes,
                                     int* thin_updates, int shift = 0) {
    InstMem mem;
    Instrument inst;
    inst.init(48000.f, mem.fx());
    inst.set_unguarded_for_test(!guards);
    inst.set_tempo_bpm(120.f);
    if (!nonbbd) inst.set_engine(PART_A, ENGINE_BBD);
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_fx_on(p, FxBlock::Flux, true);
        inst.set_fx_on(p, FxBlock::Grit, false);
    }
    Noise n;
    std::vector<float> out;
    const int kBlock = 96;
    const int kBlocks = 700;
    float inl[kBlock], inr[kBlock], ol[kBlock], orr[kBlock];
    for (int i = 0; i < shift; ++i) {
        const float x = n.next() * 0.3f, y = n.next() * 0.3f;
        float l, r;
        inst.process(&x, &y, &l, &r, 1);
        out.push_back(l);
        out.push_back(r);
    }
    for (int b = 0; b < kBlocks; ++b) {
        if (b == 60)  inst.set_tempo_bpm(97.f);
        if (b == 150) inst.set_pace(0.8f);
        if (b == 260) inst.set_tempo_bpm(133.f);
        if (b == 330 && !nonbbd) inst.set_engine(PART_A, ENGINE_SYNTH);   // away from BBD
        if (b == 420 && !nonbbd) inst.set_engine(PART_A, ENGINE_BBD);     // and back
        if (b == 380) inst.set_tempo_bpm(171.f);   // BBD away: its cycle goes stale
        if (b == 500) inst.set_pace(0.5f);
        const bool step_a = (b >= 200 && b < 300) || (b >= 560 && b < 620);
        const bool step_b = (b >= 100 && b < 140);
        inst.set_step(PART_A, step_a, 8);       // pushed every block
        inst.set_step(PART_B, step_b, 8);
        for (int i = 0; i < kBlock; ++i) {
            inl[i] = n.next() * 0.3f;
            inr[i] = n.next() * 0.3f;
        }
        inst.process(inl, inr, ol, orr, kBlock);
        for (int i = 0; i < kBlock; ++i) { out.push_back(ol[i]); out.push_back(orr[i]); }
    }
    if (recomputes) *recomputes = inst.part(PART_A).bbd().recompute_count_for_test();
    if (thin_updates) *thin_updates = inst.part(PART_A).fx().flux().thin_updates_for_test();
    return out;
}

int count_diff(const std::vector<float>& a, const std::vector<float>& b) {
    int d = 0;
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
        if (bits_of(a[i]) != bits_of(b[i])) ++d;
    return d;
}
double energy(const std::vector<float>& v) {
    double acc = 0.0;
    for (float x : v) acc += (double)x * x;
    return acc;
}

} // namespace

TEST_CASE("tape echo pair: equals two mono echoes bit for bit (saturated, clamped, flushed)") {
    for (float fb : {0.f, 0.45f, 0.9f, 1.2f}) {
        INFO("feedback " << fb);
        CHECK(pair_vs_mono(fb, 80000) == 0);
    }
}

TEST_CASE("law guards: a BBD deck's set_flow and FLUX's pace guard change no bit") {
    for (int shift : {0, 7, 23, 41, 58, 77, 95}) {
        INFO("block shift " << shift);
        int rc_off = 0, rc_on = 0, tu_off = 0, tu_on = 0;
        const auto off = render_instrument(false, false, &rc_off, &tu_off, shift);
        const auto on  = render_instrument(true,  false, &rc_on,  &tu_on,  shift);
        CHECK(count_diff(off, on) == 0);
        CHECK(energy(off) > 1.0);
        // The guard must actually have skipped work, or the test proves nothing.
        CAPTURE(rc_off);
        CAPTURE(rc_on);
        CHECK(rc_on < rc_off);
        CAPTURE(tu_off);
        CAPTURE(tu_on);
        CHECK(tu_on < tu_off);
    }
}

TEST_CASE("law guards: the same for a deck pair without BBD (tempo and pace turns)") {
    for (int shift : {0, 41, 77}) {
        INFO("block shift " << shift);
        const auto off = render_instrument(false, true, nullptr, nullptr, shift);
        const auto on  = render_instrument(true,  true, nullptr, nullptr, shift);
        CHECK(count_diff(off, on) == 0);
        CHECK(energy(off) > 1.0);
    }
}

TEST_CASE("law guards: BbdEngine::set_flow_redundant leaves exactly what a recompute would") {
    // Two engines get the same calls. X pushes set_flow(true) every time (the
    // host does that every block); Y skips it when set_flow_redundant() says
    // it would change nothing. Right after every push, and again after a
    // rendered stretch, everything _recompute() writes and everything it feeds
    // must agree bit for bit: the clocks and stage counts (clock_hz, stages,
    // clock_l/r, stages_l/r) and the output. Events land between pushes the
    // way the host's would, including a set_cycle() with no set_targets()
    // behind it -- the case the cycle clear in BbdEngine exists for.
    static float xl[BbdEngine::kCells], xr[BbdEngine::kCells];
    static float yl[BbdEngine::kCells], yr[BbdEngine::kCells];
    BbdEngine x, y;
    x.init(48000.f);
    y.init(48000.f);
    x.init_buffers(xl, xr, BbdEngine::kCells);
    y.init_buffers(yl, yr, BbdEngine::kCells);
    for (int i = 0; i < BbdEngine::kCells; ++i) xl[i] = xr[i] = yl[i] = yr[i] = 0.f;

    int skipped = 0, mismatches = 0;
    Noise n;
    auto observers_equal = [&]() {
        return bits_of(x.clock_hz()) == bits_of(y.clock_hz()) &&
               bits_of(x.clock_l())  == bits_of(y.clock_l())  &&
               bits_of(x.clock_r())  == bits_of(y.clock_r())  &&
               x.stages() == y.stages() && x.stages_l() == y.stages_l() &&
               x.stages_r() == y.stages_r();
    };
    auto push_flow = [&]() {
        x.set_flow(true);
        if (y.set_flow_redundant(true)) ++skipped;
        else y.set_flow(true);
        if (!observers_equal()) ++mismatches;
    };
    auto render = [&](int count) {
        for (int i = 0; i < count; ++i) {
            const float a = n.next() * 0.3f, b = n.next() * 0.3f;
            x.process_in(a, b);
            y.process_in(a, b);
            float lx, rx, ly, ry;
            x.process(lx, rx);
            y.process(ly, ry);
            if (bits_of(lx) != bits_of(ly) || bits_of(rx) != bits_of(ry)) ++mismatches;
        }
        if (!observers_equal()) ++mismatches;
    };
    float t[LANE_COUNT] = {0.2f, 0.6f, 0.4f, 0.7f, 0.9f};
    auto both = [&](auto&& fn) { fn(x); fn(y); };
    both([](BbdEngine& e) { e.set_cycle(0.5f); e.set_flow(true); });
    both([&](BbdEngine& e) { e.set_targets(t, 0.5f); });
    render(500);
    push_flow(); render(200);                                   // nothing changed: skipped
    const int skipped_quiet = skipped;
    both([](BbdEngine& e) { e.set_cycle(0.8f); });              // cycle moves, no recompute
    push_flow(); render(3000);
    both([](BbdEngine& e) { e.set_cycle(0.3f); });              // and again, then a second push
    push_flow(); push_flow(); render(3000);
    both([](BbdEngine& e) { e.set_width(0.7f); });
    push_flow(); render(300);
    both([](BbdEngine& e) { e.set_decay(0.3f); });
    push_flow(); render(300);
    both([](BbdEngine& e) { e.set_resonance(0.9f); });
    push_flow(); render(300);
    t[LANE_PITCH] = 0.8f; t[LANE_SIZE] = 0.2f;
    both([&](BbdEngine& e) { e.set_targets(t, 0.5f); });
    push_flow(); render(300);
    both([](BbdEngine& e) { e.set_gate(true); });               // ignored in FLOW
    push_flow(); render(300);
    both([](BbdEngine& e) { e.set_flow(false); });              // STEP, then back
    render(100);
    push_flow(); render(300);
    both([](BbdEngine& e) { e.reset(); e.set_flow(true); e.set_cycle(0.4f); });
    push_flow(); render(3000);                                  // as _engine_swap leaves it
    push_flow(); render(300);
    CHECK(mismatches == 0);
    CHECK(skipped_quiet > 0);        // the guard really fires on an idle push
    CHECK(skipped > skipped_quiet);
}

TEST_CASE("law guards: Flux::set_rhythm_pace skips only an unchanged pace") {
    TapeMem ma, mb;
    Flux a, b;                       // a: updates on every call (as before), b: guarded
    a.init(48000.f, ma.l.data(), ma.r.data());
    b.init(48000.f, mb.l.data(), mb.r.data());
    a.set_always_update_pace_for_test(true);
    for (Flux* f : {&a, &b}) {
        f->set_bpm(120.f);
        f->set_rate(3);
        f->set_link(1.f);            // thinning on: _thin_n is audible
        f->set_on(true, true);
        f->set_mix(1.f);
        f->set_feedback(0.7f);
    }
    RhythmView rv;
    rv.gap[0] = 24000; rv.gap[1] = 96000; rv.valid = true;
    a.set_rhythm(rv);
    b.set_rhythm(rv);
    int mismatches = 0, changed = 0, repeats = 0;
    int last0 = -1;
    float last_pace = 1.f;           // Flux's default pace
    Noise n;
    const float paces[] = {1.f, 1.f, 0.5f, 0.5f, 0.5f, 2.f, 2.f, 0.25f, 1.f, 1.f};
    for (float p : paces) {
        const int before_a = a.thin_updates_for_test();
        const int before_b = b.thin_updates_for_test();
        a.set_rhythm_pace(p);
        b.set_rhythm_pace(p);
        // The guarded Flux re-derives the pattern exactly when the pace moved;
        // the unguarded one every time.
        const bool moved = p != last_pace;
        if (a.thin_updates_for_test() != before_a + 1) ++mismatches;
        if (b.thin_updates_for_test() != before_b + (moved ? 1 : 0)) ++mismatches;
        if (!moved) ++repeats;
        last_pace = p;
        if (a.thin_n_for_test(0) != b.thin_n_for_test(0) ||
            a.thin_n_for_test(1) != b.thin_n_for_test(1)) ++mismatches;
        if (a.thin_n_for_test(0) != last0) ++changed;
        last0 = a.thin_n_for_test(0);
        for (int i = 0; i < 20000; ++i) {
            float la = n.next() * 0.5f, ra = n.next() * 0.5f, lb = la, rb = ra;
            a.process(la, ra);
            b.process(lb, rb);
            if (bits_of(la) != bits_of(lb) || bits_of(ra) != bits_of(rb)) ++mismatches;
        }
    }
    CHECK(mismatches == 0);
    CHECK(repeats >= 5);             // the sequence does repeat paces
    CHECK(changed >= 3);             // the pace really moves the pattern
}

// ---- candidate 9: the mix's PAN gains and CHOKE duck chain are memoised ----

namespace {
void mix_setup(Instrument& inst, float pan_a, float pan_b) {
    inst.set_tempo_bpm(120.f);
    inst.set_reverb_mix(0.4f);
    inst.set_master_drive(0.4f);
    // No COMP / FLUX here: their makeup lifts the decks above the duck
    // follower's window (kChokeDuckFull), where env_n saturates at 1 and the
    // follower's coefficients cannot show in the output.
    inst.set_pan(0, pan_a);
    inst.set_pan(1, pan_b);
}

// A full Instrument: CHOKE (settled and gliding), PAN (static and moving),
// reverb, loud input so the limiter bends, master drive on and moved.
// `reference` computes the PAN gains and the duck chain from scratch every
// sample (the code before the memos). `reinit_at` > 0 re-runs init() at a
// different sample rate and with different PAN state after that many blocks,
// with CHOKE at noon so the follower coefficients matter only afterwards.
std::vector<float> render_mix(bool reference, int reinit_at, float* squash_max, float* duck_min) {
    InstMem mem;
    Instrument inst;
    inst.init(48000.f, mem.fx());
    inst.set_always_compute_mix_for_test(reference);
    mix_setup(inst, -0.4f, 0.5f);
    Noise n;
    std::vector<float> out;
    const int kBlock = 96, kBlocks = 900;
    float inl[kBlock], inr[kBlock], ol[kBlock], orr[kBlock];
    float sq = 0.f, dk = 1.f;
    for (int b = 0; b < kBlocks; ++b) {
        if (b == reinit_at && reinit_at > 0) {
            inst.init(44100.f, mem.fx());          // new coefficients, new PAN state
            mix_setup(inst, 0.7f, -0.2f);
        }
        if (b == 100) inst.set_choke(0.35f);           // duck zone, settles
        if (b == 300) inst.set_choke(-0.9f);           // hot zone, other deck priority
        if (b == 500) inst.set_choke(0.f);
        if (b == 560) inst.set_master_drive(0.9f);     // the limiter's pre-gain glides
        if (b >= 600 && b < 700) inst.set_pan(0, -0.4f + 0.008f * (b - 600));   // moving PAN
        if (b == 750) inst.set_choke(0.6f);
        // Quiet until block 640, so the duck follower sits inside its window
        // (env_n strictly between 0 and 1 -- loud input saturates it) and its
        // coefficients show in the output; loud after, so the limiter bends.
        const float amp = b < 640 ? 0.f : 0.9f;
        for (int i = 0; i < kBlock; ++i) {
            inl[i] = n.next() * amp;
            inr[i] = n.next() * amp;
        }
        inst.process(inl, inr, ol, orr, kBlock);
        sq = std::max(sq, inst.limiter_squash());
        dk = std::min(dk, inst.choke_duck_gain());
        out.push_back(inst.choke_duck_gain());     // the duck gain itself, per block
        for (int i = 0; i < kBlock; ++i) { out.push_back(ol[i]); out.push_back(orr[i]); }
    }
    if (squash_max) *squash_max = sq;
    if (duck_min) *duck_min = dk;
    return out;
}
} // namespace

TEST_CASE("mix memos: ducking, panning and limiting render the same bits") {
    float sq = 0.f, dk = 1.f;
    const auto ref = render_mix(true, 0, &sq, &dk);
    const auto memo = render_mix(false, 0, nullptr, nullptr);
    CHECK(count_diff(ref, memo) == 0);
    CHECK(energy(ref) > 1.0);
    // The path was really exercised: the limiter bent and the duck pulled down.
    CAPTURE(sq);
    CAPTURE(dk);
    CHECK(sq > 0.f);
    CHECK(dk < 0.9f);
}

TEST_CASE("mix memos: a re-init (other sample rate, other PAN) matches the from-scratch mix") {
    // init() must drop the CHOKE chain's memo: its atk / rel come from
    // coefficients init() recomputes, and the old depth_raw key (0 at noon)
    // would otherwise match and return the 48 kHz values.
    for (int at : {50, 150, 420}) {     // 50: CHOKE still at its boot value, the key is 0
        INFO("re-init at block " << at);
        const auto ref = render_mix(true, at, nullptr, nullptr);
        const auto memo = render_mix(false, at, nullptr, nullptr);
        CHECK(count_diff(ref, memo) == 0);
    }
}

// ---- candidate 10: the reverb tank at half rate (toggle bit 9, class 2) ----
//
// Not a bit-identity test: the output is allowed to change. What must hold is
// that the room is still the same room -- finite, about as loud, and decaying
// as long.

#include "fx/reverb.h"
#include "fx/halfband.h"

namespace {
AmbientReverb g_rev_full, g_rev_half;

// Wet-only left output: 0.5 s of noise (0.3 peak) into the room, then silence.
std::vector<float> reverb_render(AmbientReverb& rv, bool half, float decay, float size,
                                 int seconds, int switch_every = 0) {
    rv.init(48000.f);
    rv.set_decay(decay);
    rv.set_size(size);
    rv.set_tone(0.5f);
    rv.set_half_rate(half);
    Noise n;
    std::vector<float> out;
    out.reserve(48000 * seconds);
    for (int i = 0; i < 48000 * seconds; ++i) {
        if (switch_every > 0 && i > 0 && i % switch_every == 0) rv.set_half_rate(!rv.half_rate());
        const float x = i < 24000 ? 0.3f * n.next() : 0.f;
        float l, r;
        rv.process(x, x, l, r);
        out.push_back(l);
    }
    return out;
}

double rms_of(const std::vector<float>& v, size_t a, size_t b) {
    double s = 0.0;
    for (size_t i = a; i < b; ++i) s += (double)v[i] * v[i];
    return std::sqrt(s / (double)(b - a));
}

// RT60 from the wet tail: the 20 ms RMS envelope in dB, a least-squares line
// through the windows between 10 and 40 dB below the loudest one (after it),
// extrapolated to 60 dB. A line over 30 dB of the decay, not the first
// crossing of one level, so a stochastic bump does not move it.
double decay_rt60(const std::vector<float>& v) {
    const size_t W = 960;
    double peak = 0.0;
    size_t pk = 0;
    for (size_t i = 0; i + W <= v.size(); i += W) {
        const double e = rms_of(v, i, i + W);
        if (e > peak) { peak = e; pk = i; }
    }
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    for (size_t i = pk; i + W <= v.size(); i += W) {
        const double db = 20.0 * std::log10(rms_of(v, i, i + W) / peak + 1e-12);
        if (db > -10.0 || db < -40.0) continue;
        const double t = (double)(i - pk) / 48000.0;
        sx += t; sy += db; sxx += t * t; sxy += t * db; ++n;
    }
    if (n < 5) return -1.0;
    const double slope = ((double)n * sxy - sx * sy) / ((double)n * sxx - sx * sx);   // dB per s
    return slope < 0.0 ? -60.0 / slope : -1.0;
}
} // namespace

TEST_CASE("reverb half rate: same loudness and decay as the full-rate room") {
    for (float decay : {0.55f, 0.7f}) {
        for (float size : {0.6f, 0.9f}) {
            INFO("decay " << decay << " size " << size);
            const auto full = reverb_render(g_rev_full, false, decay, size, 10);
            const auto half = reverb_render(g_rev_half, true, decay, size, 10);
            bool finite = true;
            double ef = 0.0, eh = 0.0;
            for (size_t i = 0; i < full.size(); ++i) {
                if (!std::isfinite(half[i])) finite = false;
                ef += (double)full[i] * full[i];
                eh += (double)half[i] * half[i];
            }
            CHECK(finite);
            CHECK(ef > 1e-6);
            const double db = 10.0 * std::log10(eh / ef);
            const double tf = decay_rt60(full), th = decay_rt60(half);
            CAPTURE(db);
            CAPTURE(tf);
            CAPTURE(th);
            CHECK(std::fabs(db) < 3.0);
            REQUIRE(tf > 0.0);
            REQUIRE(th > 0.0);
            CHECK(std::fabs(th / tf - 1.0) < 0.10);
        }
    }
}

TEST_CASE("reverb half rate: switching the mode mid-run stays finite and bounded") {
    const auto sw = reverb_render(g_rev_half, false, 0.7f, 0.6f, 6, 12000);
    bool finite = true;
    float peak = 0.f;
    for (float v : sw) { if (!std::isfinite(v)) finite = false; peak = std::max(peak, std::fabs(v)); }
    CHECK(finite);
    CHECK(peak < 1.5f);
    CHECK(peak > 0.001f);
}

TEST_CASE("halfband converters: unity DC gain, passband flat, stopband down >= 60 dB") {
    // Sine of f Hz through down then up: the round trip gain (after settling).
    auto gain_db = [](float f) {
        HalfbandDown dn; HalfbandUp up;
        double acc_in = 0.0, acc_out = 0.0;
        const int total = 48000;
        float pend = 0.f;
        float a = 0.f;
        for (int i = 0; i < total; ++i) {
            const float x = (float)std::sin(6.283185307179586 * (double)f * (double)i / 48000.0);
            float y;
            if ((i & 1) == 0) { a = x; y = pend; }
            else {
                float first, second;
                up.process(dn.process(a, x), first, second);
                y = first; pend = second;
            }
            if (i > 4000) { acc_in += (double)x * x; acc_out += (double)y * y; }
        }
        return 10.0 * std::log10(acc_out / acc_in + 1e-30);
    };
    for (float f : {100.f, 1000.f, 5000.f, 8000.f, 10000.f}) {
        INFO("passband " << f << " Hz");
        // down + up: the filter is applied twice, so +-0.015 dB.
        CHECK(std::fabs(gain_db(f)) < 0.05);
    }
    for (float f : {14500.f, 16000.f, 20000.f, 23000.f}) {
        INFO("stopband " << f << " Hz");
        CHECK(gain_db(f) < -60.0);
    }
}

// ---- candidate 10, fix round: modulation rate and tail tone -----------------
//
// The energy / RT60 bounds above cannot see a wrong LFO speed or a wrong tone
// filter. These two can.

namespace {
// Reversals of the sampled value's direction per second, over `seconds` of
// silence into the room (SMEAR and WOBL at their defaults).
struct LfoRates { double random_per_s; double cosine_crossings_per_s; };
LfoRates lfo_rates(AmbientReverb& rv, bool half, int seconds) {
    rv.init(48000.f);
    rv.set_half_rate(half);
    int reversals = 0, crossings = 0;
    float prev = rv.lfo_for_test(5), prev_dir = 0.f;
    float cprev = rv.cos_lfo_for_test(0) - 0.5f;
    for (int i = 0; i < 48000 * seconds; ++i) {
        float l, r;
        rv.process(0.f, 0.f, l, r);
        const float v = rv.lfo_for_test(5);
        const float d = v - prev;
        if (d != 0.f) {
            if (prev_dir != 0.f && ((d > 0.f) != (prev_dir > 0.f))) ++reversals;
            prev_dir = d;
        }
        prev = v;
        const float c = rv.cos_lfo_for_test(0) - 0.5f;
        if ((c > 0.f) != (cprev > 0.f)) ++crossings;
        cprev = c;
    }
    return { reversals / (double)seconds, crossings / (double)seconds };
}

// Power spectrum averaged over Hann frames (N = 4096) of v[from, to).
void band_powers(const std::vector<float>& v, size_t from, size_t to,
                 double& low, double& high) {
    const int N = 4096;
    std::vector<double> acc(N / 2, 0.0);
    int frames = 0;
    for (size_t s = from; s + N <= to; s += N / 2, ++frames) {
        std::vector<double> re(N), im(N, 0.0);
        for (int i = 0; i < N; ++i)
            re[i] = v[s + i] * (0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * i / N));
        for (int i = 1, j = 0; i < N; ++i) {          // bit reversal
            int bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(re[i], re[j]);
        }
        for (int len = 2; len <= N; len <<= 1) {
            const double ang = -2.0 * 3.14159265358979 / len;
            for (int i = 0; i < N; i += len)
                for (int k = 0; k < len / 2; ++k) {
                    const double wr = std::cos(ang * k), wi = std::sin(ang * k);
                    const double ur = re[i + k], ui = im[i + k];
                    const double xr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
                    const double xi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                    re[i + k] = ur + xr;           im[i + k] = ui + xi;
                    re[i + k + len / 2] = ur - xr; im[i + k + len / 2] = ui - xi;
                }
        }
        for (int k = 0; k < N / 2; ++k) acc[k] += re[k] * re[k] + im[k] * im[k];
    }
    low = high = 0.0;
    for (int k = 0; k < N / 2; ++k) {
        const double f = k * 48000.0 / N;
        if (f >= 100.0 && f < 1000.0) low += acc[k];
        if (f >= 4000.0 && f < 8000.0) high += acc[k];
    }
}

// Tail (1 s .. 5 s) of the wet left output, TONE `tone`.
double tail_balance_db(AmbientReverb& rv, bool half, float tone) {
    rv.init(48000.f);
    rv.set_decay(0.7f);
    rv.set_size(0.6f);
    rv.set_tone(tone);
    rv.set_half_rate(half);
    Noise n;
    std::vector<float> out;
    for (int i = 0; i < 48000 * 6; ++i) {
        const float x = i < 24000 ? 0.3f * n.next() : 0.f;
        float l, r;
        rv.process(x, x, l, r);
        out.push_back(l);
    }
    double low, high;
    band_powers(out, 48000, 48000 * 5, low, high);
    return 10.0 * std::log10(high / low);
}
} // namespace

TEST_CASE("reverb half rate: the LFOs run at the same speed in Hz") {
    const LfoRates full = lfo_rates(g_rev_full, false, 20);
    const LfoRates half = lfo_rates(g_rev_half, true, 20);
    CAPTURE(full.random_per_s);
    CAPTURE(half.random_per_s);
    CAPTURE(full.cosine_crossings_per_s);
    CAPTURE(half.cosine_crossings_per_s);
    REQUIRE(full.random_per_s > 1.0);
    REQUIRE(full.cosine_crossings_per_s > 0.5);
    CHECK(std::fabs(half.random_per_s / full.random_per_s - 1.0) < 0.10);
    CHECK(std::fabs(half.cosine_crossings_per_s / full.cosine_crossings_per_s - 1.0) < 0.10);
}

TEST_CASE("reverb half rate: the tail keeps its tone balance") {
    // Energy of the tail in 4-8 kHz against 100-1000 Hz, half against full.
    // The half-rate room IS darker at the top, by design of the rate: every
    // fixed-fraction linear read inside the loop (dap1b / dap2b, the four
    // input diffusers) is a lowpass of |cos(pi f / fs)| per pass and fs halved
    // (measured: 4-8 kHz down 6.2 dB at TONE 0.5 and 9.3 dB at TONE 1.0, size
    // 0.6; 8-12 kHz down 21 / 28 dB), a little offset by the pole-matched TONE
    // one-pole being slightly brighter near fs / 4. So this is a PIN of the
    // measured shift (+-2.5 dB), not a "no change" bound: a tone filter derived
    // for the wrong rate (the 48 kHz coefficient at 24 kHz doubles the cutoff)
    // moves it by several dB, up or down.
    struct Case { float tone; double expect_db; };
    for (Case c : {Case{0.5f, -6.2}, Case{1.0f, -9.3}}) {
        INFO("TONE " << c.tone);
        const double full = tail_balance_db(g_rev_full, false, c.tone);
        const double half = tail_balance_db(g_rev_half, true, c.tone);
        CAPTURE(full);
        CAPTURE(half);
        CAPTURE(half - full);
        CHECK(std::fabs((half - full) - c.expect_db) < 2.5);
    }
}
