// The callback profile's stamps (engine/util/probe_mark.h), on the desktop.
// This file is its own executable (spky_probe_tests), the only desktop target
// built with SPKY_PROBE_MARKS=1, so the probe build of the engine cannot rot
// between two board sessions. It checks ORDER, not cost: every stamp of a
// sample is taken after the one before it, in the sequence Instrument::process
// and Part::process take them. What a segment costs is the board's to say
// (shell/cb_profile.cpp, docs/hardware/reva-bring-up.md 10.1).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>
#include "instrument.h"
#include "util/probe_law.h"
#include "util/probe_mark.h"

namespace {

// A clock the engine can read through probe_clock: a counter one thread keeps
// advancing while the test runs. Monotonic; the rate does not matter, the
// order does. Volatile, not atomic, because the engine reads it as a
// `volatile uint32_t*` -- the firmware's pointer is the DWT cycle counter.
volatile uint32_t g_clock = 0;
std::atomic<bool> g_run{true};

struct Spinner {
    std::thread t;
    Spinner() {
        g_run = true;    // the previous test case's destructor cleared it
        t = std::thread([] {
            while (g_run.load(std::memory_order_relaxed)) g_clock = g_clock + 1u;
        });
        // A thread starts slower than eight blocks render: wait until the
        // counter is actually running before a test reads it.
        const uint32_t start = g_clock;
        while (g_clock == start) {}
    }
    ~Spinner() {
        g_run = false;
        t.join();
    }
};

constexpr int kSlots = sizeof(spky::ProbePartSlots) / sizeof(uint32_t);
constexpr int kMix   = sizeof(spky::ProbeMixSlots) / sizeof(uint32_t);
struct Block {
    std::vector<uint32_t>                     ts, d1, d2;
    std::vector<std::array<uint32_t, kSlots>> p1, p2;
    std::vector<std::array<uint32_t, kMix>>   mx;
    int                                       n, pri;
};
std::vector<Block> g_blocks;

// "b is not before a" on a wrapping 32-bit counter: the forward distance is
// under half the range. The spinner wraps after ~4e9 increments, seconds
// away, but a test that can fail on a wrap is a flaky test.
bool after(uint32_t a, uint32_t b) { return (b - a) < 0x80000000u; }

} // namespace

namespace spky {
volatile uint32_t* probe_clock = &g_clock;
ProbePartSlots     probe_part_scratch;

void probe_block(const uint32_t* ts, const uint32_t* d1, const uint32_t* d2,
                 const ProbePartSlots* p1, const ProbePartSlots* p2,
                 const ProbeMixSlots* mx, int n, int pri)
{
    Block b;
    b.n   = n;
    b.pri = pri;
    b.ts.assign(ts, ts + n + 1);
    b.d1.assign(d1, d1 + n);
    b.d2.assign(d2, d2 + n);
    for (int i = 0; i < n; ++i) {
        std::array<uint32_t, kSlots> s1, s2;
        for (int k = 0; k < kSlots; ++k) {
            s1[k] = p1[i][k];
            s2[k] = p2[i][k];
        }
        b.p1.push_back(s1);
        b.p2.push_back(s2);
        std::array<uint32_t, kMix> m;
        for (int k = 0; k < kMix; ++k) m[k] = mx[i][k];
        b.mx.push_back(m);
    }
    g_blocks.push_back(std::move(b));
}
} // namespace spky

// A deck call's slots in the order they are taken (probe_mark.h): the
// modulation split 6..9 comes first, with the lanes' own 10 and 11 inside
// 6..7 and the pitch lane's 12..14 inside 6..10, then 0..5 with GRIT's
// 15..20 inside 2..3.
constexpr int kDeckOrder[kSlots] = {6, 12, 13, 14, 10, 11, 7, 8, 9, 0, 1, 2,
                                    15, 16, 17, 18, 19, 20, 3, 4, 5};

// Samples of a block whose stamps are not in process order: top ->
// [priority deck: pre, lanes, fire bookkeeping, control tick, trigger, engine,
// smoothers, grit, flux, comp, send] -> [yielding deck: the same eleven] ->
// [mix: observers + taps, blend + duck, reverb, return + sleep] -> limiter ->
// next top.
int out_of_order(const Block& b) {
    int bad = 0;
    for (int i = 0; i < b.n; ++i) {
        bool ok = after(b.ts[i], b.p1[i][kDeckOrder[0]]);
        for (int k = 1; k < kSlots; ++k)
            ok = ok && after(b.p1[i][kDeckOrder[k - 1]], b.p1[i][kDeckOrder[k]]);
        ok = ok && after(b.p1[i][kDeckOrder[kSlots - 1]], b.d1[i]);
        ok = ok && after(b.d1[i], b.p2[i][kDeckOrder[0]]);
        for (int k = 1; k < kSlots; ++k)
            ok = ok && after(b.p2[i][kDeckOrder[k - 1]], b.p2[i][kDeckOrder[k]]);
        ok = ok && after(b.p2[i][kDeckOrder[kSlots - 1]], b.d2[i]);
        ok = ok && after(b.d2[i], b.mx[i][0]);
        for (int k = 1; k < kMix; ++k) ok = ok && after(b.mx[i][k - 1], b.mx[i][k]);
        ok = ok && after(b.mx[i][kMix - 1], b.ts[i + 1]);
        if (!ok) ++bad;
    }
    return bad;
}

// The full FX chain and the reverb, as the firmware has them.
std::vector<float> s_echo[spky::PART_COUNT][2];
std::vector<float> s_bbd[spky::PART_COUNT][2];
spky::AmbientReverb s_reverb;
spky::FxMem fx_mem() {
    spky::FxMem m;
    for (int p = 0; p < spky::PART_COUNT; ++p)
        for (int c = 0; c < 2; ++c) {
            s_echo[p][c].assign(spky::Flux::kMaxSamples, 0.f);
            s_bbd[p][c].assign(spky::BbdEngine::kCells, 0.f);
            m.echo[p][c] = s_echo[p][c].data();
            m.bbd[p][c]  = s_bbd[p][c].data();
        }
    m.reverb = &s_reverb;
    return m;
}

TEST_CASE("probe marks: every sample's stamps come in process order") {
    Spinner spin;
    g_blocks.clear();
    spky::Instrument inst;
    inst.init(48000.f);
    std::vector<float> l(96), r(96);
    for (int blk = 0; blk < 8; ++blk) inst.process(nullptr, nullptr, l.data(), r.data(), 96);
    REQUIRE(g_blocks.size() == 8);
    int moved = 0;
    for (const Block& b : g_blocks) {
        REQUIRE(b.n == 96);
        CHECK((b.pri == 0 || b.pri == 1));
        CHECK(out_of_order(b) == 0);
        if (b.ts[b.n] != b.ts[0]) ++moved;
    }
    // The clock ran: a block's stamps are not all one value. (A frozen clock
    // would pass every order check above and prove nothing.)
    CHECK(moved == 8);
}

TEST_CASE("probe marks: the engine segment is non-empty on a playing deck") {
    // Both decks of the init patch run an engine; the stamps around it are
    // apart on a running clock. The first block is skipped: it wakes voices.
    Spinner spin;
    g_blocks.clear();
    spky::Instrument inst;
    inst.init(48000.f);
    std::vector<float> l(96), r(96);
    for (int blk = 0; blk < 4; ++blk) inst.process(nullptr, nullptr, l.data(), r.data(), 96);
    REQUIRE(g_blocks.size() == 4);
    uint64_t eng1 = 0, eng2 = 0;
    for (size_t k = 1; k < g_blocks.size(); ++k) {
        const Block& b = g_blocks[k];
        for (int i = 0; i < b.n; ++i) {
            eng1 += b.p1[i][1] - b.p1[i][0];
            eng2 += b.p2[i][1] - b.p2[i][0];
        }
    }
    CHECK(eng1 > 0);
    CHECK(eng2 > 0);
}

TEST_CASE("probe marks: the reverb segment runs while a send is open, and its clear steps are counted") {
    // The mix split (fault R4): with a send open the reverb runs between
    // slots 1 and 2; with both sends closed the return fades, the room is
    // cleared in 32 steps on the control ticks (counted in probe_rev_clear)
    // and the segment empties.
    Spinner spin;
    g_blocks.clear();
    static spky::Instrument inst;   // ~100 KB: not on the test's stack
    inst.init(48000.f, fx_mem());
    inst.set_reverb_mix(1.f);
    std::vector<float> in(96), l(96), r(96);
    for (int i = 0; i < 96; ++i) in[i] = 0.3f * std::sin(0.05f * i);
    for (int blk = 0; blk < 20; ++blk) inst.process(in.data(), in.data(), l.data(), r.data(), 96);
    REQUIRE_FALSE(inst.reverb_asleep());
    uint64_t rev = 0;
    int bad = 0;
    for (size_t k = 1; k < g_blocks.size(); ++k) {
        bad += out_of_order(g_blocks[k]);
        for (int i = 0; i < g_blocks[k].n; ++i) rev += g_blocks[k].mx[i][2] - g_blocks[k].mx[i][1];
    }
    CHECK(bad == 0);
    CHECK(rev > 0);

    const uint32_t clears = spky::probe_rev_clear.n;
    inst.set_reverb_mix(0.f);
    int blocks = 0;
    while (!inst.reverb_asleep() && blocks < 2000) {
        inst.process(in.data(), in.data(), l.data(), r.data(), 96);
        ++blocks;
    }
    REQUIRE(inst.reverb_asleep());
    for (int blk = 0; blk < 40; ++blk) inst.process(in.data(), in.data(), l.data(), r.data(), 96);
    CHECK(spky::probe_rev_clear.n == clears + 32);
    g_blocks.clear();
    for (int blk = 0; blk < 4; ++blk) inst.process(in.data(), in.data(), l.data(), r.data(), 96);
    bad = 0;
    uint32_t asleep_rev = 0;
    for (const Block& b : g_blocks) {
        bad += out_of_order(b);
        for (int i = 0; i < b.n; ++i) asleep_rev += b.mx[i][2] - b.mx[i][1];
    }
    CHECK(bad == 0);
    // Asleep, slots 1 and 2 are taken back to back: a stamp's own cost only.
    CHECK(asleep_rev < rev / 10);
}

TEST_CASE("probe marks: the lanes segment is non-empty and the lanes' stamps come in order") {
    // The modulation split (fault R4): SuperModulator::process runs on every
    // sample, its pitch lane first. Order, and presence of the whole lanes
    // segment (its texture tick), only. The pitch lane alone is too cheap
    // on the desktop for the spinner to step across it reliably (0 in 8 of
    // 20 runs on deck A). Two attempts to pin WHERE the control tick lands -- a size
    // floor on its segment, then the position of each block's largest tick
    // segment -- both flaked on the free-running spinner clock, red with the
    // code in place and green with slot 9 misplaced (2026-10-24), so they are
    // gone: the board's DWT split (CBP_WIN5 / CBP_WIN6 / CBP_WORST) is the
    // evidence for what each segment costs and where.
    Spinner spin;
    g_blocks.clear();
    spky::Instrument inst;
    inst.init(48000.f);
    std::vector<float> l(96), r(96);
    for (int blk = 0; blk < 6; ++blk) inst.process(nullptr, nullptr, l.data(), r.data(), 96);
    REQUIRE(g_blocks.size() == 6);
    uint64_t lanes[2] = {0, 0};
    int bad = 0;
    for (size_t k = 1; k < g_blocks.size(); ++k) {
        const Block& b = g_blocks[k];
        bad += out_of_order(b);
        for (int i = 0; i < b.n; ++i) {
            lanes[0] += b.p1[i][7] - b.p1[i][6];
            lanes[1] += b.p2[i][7] - b.p2[i][6];
        }
    }
    CHECK(bad == 0);
    CHECK(lanes[0] > 0);
    CHECK(lanes[1] > 0);
}

TEST_CASE("probe marks: GRIT's stamps come in order with GRIT running, both modes") {
    // The GRIT split (fault R4): deck A on CRSH, deck B on DRIVE, both at a
    // real mix, so Grit::process takes its DSP path and every one of slots
    // 15..20 is written there (not only on the idle path that writes them
    // all at once). Order only, for the reason the lanes case gives.
    Spinner spin;
    g_blocks.clear();
    spky::Instrument inst;
    inst.init(48000.f);
    inst.set_grit_mode(spky::PART_A, spky::GritMode::Reduce);
    inst.set_grit_mode(spky::PART_B, spky::GritMode::Drive);
    inst.set_grit_mix(spky::PART_A, 0.5f);
    inst.set_grit_mix(spky::PART_B, 0.5f);
    inst.set_fx_on(spky::PART_A, spky::FxBlock::Grit, true);
    inst.set_fx_on(spky::PART_B, spky::FxBlock::Grit, true);
    std::vector<float> l(96), r(96);
    for (int blk = 0; blk < 6; ++blk) inst.process(nullptr, nullptr, l.data(), r.data(), 96);
    REQUIRE(g_blocks.size() == 6);
    REQUIRE(inst.grit_engaged_for_test(spky::PART_A));
    REQUIRE(inst.grit_engaged_for_test(spky::PART_B));
    int bad = 0;
    for (size_t k = 1; k < g_blocks.size(); ++k) bad += out_of_order(g_blocks[k]);
    CHECK(bad == 0);
}

// The control law's TIDE split (util/probe_law.h, fault R4), built into this
// executable with SPKY_PROBE_LAW=1. Counts and nesting, not cost: one TIDE
// turn that re-slots lanes is one set_tide, one re-slot and one slew rebuild
// per lane whose slot count moved, and each part's cycles fit inside the
// part around it. An unchanged TIDE stamps only its early-out.
TEST_CASE("probe law: a TIDE turn's re-slots and slew rebuilds are counted and nest") {
    Spinner spin;
    spky::probe_law_clock = &g_clock;
    spky::SuperModulator m;
    m.init(48000.f, 4242u);
    m.set_tide(0.5f);
    m.set_step(true, 8);
    for (int i = 0; i < 4800; ++i) m.process(spky::probe_clock, spky::probe_part_scratch);

    spky::probe_law = {};
    m.set_tide(0.95f);
    const spky::ProbeLaw t = spky::probe_law;
    // TIDE 0.5 -> 0.95 moves the texture lanes' slot counts (lane_slots),
    // not the pitch lane's; every lane that moved re-slots live (it has run).
    CHECK(t.n_tide == 1);
    CHECK(t.n_reslot >= 1);
    CHECK(t.n_reslot < spky::LANE_COUNT);
    CHECK(t.n_sinc == t.n_reslot);
    CHECK(t.tide - t.steps - t.rate < 0x80000000u);       // steps and rate inside tide
    CHECK(t.steps - t.reslot - t.sinc < 0x80000000u);     // re-slots and rebuilds inside steps
    CHECK(t.rate - t.rinc < 0x80000000u);

    spky::probe_law = {};
    m.set_tide(0.95f);
    const spky::ProbeLaw u = spky::probe_law;
    CHECK(u.n_tide == 1);
    CHECK(u.n_reslot == 0);
    CHECK(u.n_sinc == 0);
    CHECK(u.steps == 0);
    CHECK(u.rate == 0);
}

TEST_CASE("probe marks: WAVE's voice calls are counted by voice state") {
    // Fault R4: every voice call of a block lands in exactly one bin. Before
    // a note all four voices idle; a note far from the voices' init pitch
    // (220 Hz -> 880 Hz) moves the mip, so its voice starts in attack + mip
    // crossfade, and the three others stay idle.
    Spinner spin;
    spky::WaveEngine e;
    e.init(48000.f);
    auto bins = [&] {
        const auto& p = e.probe_eng();
        return std::array<uint32_t, 5>{p.vcnt[0], p.vcnt[1], p.vcnt[2], p.vcnt[3], p.vcnt[4]};
    };
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 96; ++i) e.process(l, r);
    const auto idle = bins();
    CHECK(idle[4] == 4u * 96u);
    CHECK(idle[0] + idle[1] + idle[2] + idle[3] == 0u);

    e.trigger(1.f);
    for (int i = 0; i < 96; ++i) e.process(l, r);
    const auto played = bins();
    uint32_t calls = 0;
    for (int k = 0; k < 5; ++k) calls += played[k] - idle[k];
    CHECK(calls == 4u * 96u);
    CHECK(played[4] - idle[4] == 3u * 96u);   // three voices still idle
    CHECK(played[3] - idle[3] > 0u);          // attack + crossfade
    uint64_t cyc = 0;
    for (int k = 0; k < 5; ++k) cyc += e.probe_eng().vcyc[k];
    CHECK(cyc > 0u);
    CHECK(cyc <= e.probe_eng().voices);       // the bins sit inside voices
}
