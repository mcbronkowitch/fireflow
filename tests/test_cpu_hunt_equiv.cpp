// The CPU hunt's class-1 candidates that carry a runtime flag (toggle bits 4 and
// 5, engine/util/probe_toggles.h): each is rendered on and off, bit for bit.
// Toggles compile away on the desktop, so the tests flip the flags directly.
#include <doctest/doctest.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include "instrument.h"
#include "parts/part.h"
#include "parts/bbd_engine.h"
#include "fx/flux.h"

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

// ---- bit 4: FLUX_STEREO_LINE -------------------------------------------

struct TapeMem {
    std::vector<float> l, r;
    TapeMem() : l(Flux::kMaxSamples), r(Flux::kMaxSamples) {}
};

// mode 0: stereo line off, 1: on, 2: flipped every 977 samples.
std::vector<float> render_flux(int mode) {
    TapeMem mem;
    Flux f;
    f.init(48000.f, mem.l.data(), mem.r.data());
    f.set_bpm(120.f);
    f.set_rate(3);
    f.set_mix(1.f);
    f.set_feedback(0.9f);           // high feedback: the loop carries the state
    f.set_time_mod(0.5f);
    f.set_on(true, true);
    Noise n;
    std::vector<float> out;
    const int total = 60000;
    for (int i = 0; i < total; ++i) {
        if (mode == 1) f.set_stereo_line(true);
        if (mode == 2) f.set_stereo_line(((i / 977) & 1) != 0);
        if (i == 6000)  f.set_time_mod(0.15f);   // the smoothed delay time moves
        if (i == 14000) f.set_time_mod(0.9f);
        if (i == 24000) f.set_rate(5);
        if (i == 33000) f.set_on(false);         // fall, then the idle flush
        if (i == 41000) f.set_on(true);          // a fresh take on flushed lines
        if (i == 50000) f.set_time_mod(0.4f);
        float l = n.next() * 0.5f, r = n.next() * 0.5f;
        f.process(l, r);
        out.push_back(l);
        out.push_back(r);
    }
    return out;
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
// swapped away from BBD and back.
// `shift` samples of pre-roll move the host's block boundaries against the
// engine's 96-sample control ticks, so a boundary can fall between an engine
// swap's set_cycle() and the next tick's set_targets() -- the one spot where a
// pending recompute is real.
std::vector<float> render_instrument(bool guards, bool nonbbd, int* recomputes,
                                     int shift = 0) {
    InstMem mem;
    Instrument inst;
    inst.init(48000.f, mem.fx());
    inst.set_law_guards(guards);
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

TEST_CASE("FLUX stereo line: on, off and flipped mid-stream render the same bits") {
    const auto off = render_flux(0), on = render_flux(1), flip = render_flux(2);
    CHECK(count_diff(off, on) == 0);
    CHECK(count_diff(off, flip) == 0);
    // Not silence, and the wet tail really exists after the flush and re-entry.
    CHECK(energy(off) > 1.0);
}

TEST_CASE("law guards: a BBD deck's set_flow and FLUX's pace guard change no bit") {
    for (int shift : {0, 7, 23, 41, 58, 77, 95}) {
        INFO("block shift " << shift);
        int rc_off = 0, rc_on = 0;
        const auto off = render_instrument(false, false, &rc_off, shift);
        const auto on  = render_instrument(true,  false, &rc_on,  shift);
        CHECK(count_diff(off, on) == 0);
        CHECK(energy(off) > 1.0);
        // The guard must actually have skipped work, or the test proves nothing.
        CAPTURE(rc_off);
        CAPTURE(rc_on);
        CHECK(rc_on < rc_off);
    }
}

TEST_CASE("law guards: the same for a deck pair without BBD (tempo and pace turns)") {
    for (int shift : {0, 41, 77}) {
        INFO("block shift " << shift);
        const auto off = render_instrument(false, true, nullptr, shift);
        const auto on  = render_instrument(true,  true, nullptr, shift);
        CHECK(count_diff(off, on) == 0);
        CHECK(energy(off) > 1.0);
    }
}

TEST_CASE("law guards: BbdEngine::set_flow_redundant is exact after every setter that feeds _recompute") {
    // Two engines get the same calls. X pushes set_flow(true) every time (the
    // host does that every block); Y skips it when set_flow_redundant() says
    // it would change nothing. After each event both render a stretch; every
    // output bit must agree, and the guard must have skipped at least once.
    static float xl[BbdEngine::kCells], xr[BbdEngine::kCells];
    static float yl[BbdEngine::kCells], yr[BbdEngine::kCells];
    BbdEngine x, y;
    for (BbdEngine* e : {&x, &y}) e->init(48000.f);
    x.init_buffers(xl, xr, BbdEngine::kCells);
    y.init_buffers(yl, yr, BbdEngine::kCells);
    for (int i = 0; i < BbdEngine::kCells; ++i) xl[i] = xr[i] = yl[i] = yr[i] = 0.f;

    int skipped = 0, mismatches = 0;
    Noise n;
    auto push_flow = [&]() {
        x.set_flow(true);
        if (y.set_flow_redundant(true)) ++skipped;
        else y.set_flow(true);
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
    };
    float t[LANE_COUNT] = {0.2f, 0.6f, 0.4f, 0.7f, 0.9f};
    auto both = [&](auto&& fn) { fn(x); fn(y); };
    both([](BbdEngine& e) { e.set_cycle(0.5f); e.set_flow(true); });
    both([&](BbdEngine& e) { e.set_targets(t, 0.5f); });
    render(500);
    push_flow(); render(200);                                   // nothing changed: skipped
    both([](BbdEngine& e) { e.set_cycle(0.8f); });              // cycle moves, no recompute
    push_flow(); render(300);
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
    push_flow(); render(300);                                   // as _engine_swap leaves it
    push_flow(); render(300);
    CHECK(mismatches == 0);
    CHECK(skipped > 0);
}

TEST_CASE("law guards: Flux::set_rhythm_pace skips only an unchanged pace") {
    TapeMem ma, mb;
    Flux a, b;                       // a: guard off, b: guard on
    a.init(48000.f, ma.l.data(), ma.r.data());
    b.init(48000.f, mb.l.data(), mb.r.data());
    b.set_skip_same_pace(true);
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
    int mismatches = 0, changed = 0;
    int last0 = -1;
    Noise n;
    const float paces[] = {1.f, 1.f, 0.5f, 0.5f, 0.5f, 2.f, 2.f, 0.25f, 1.f, 1.f};
    for (float p : paces) {
        a.set_rhythm_pace(p);
        b.set_rhythm_pace(p);
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
    CHECK(changed >= 3);             // the pace really moves the pattern
}
