// The callback profile's stamps (engine/util/probe_mark.h), on the desktop.
// This file is its own executable (spky_probe_tests), the only desktop target
// built with SPKY_PROBE_MARKS=1, so the probe build of the engine cannot rot
// between two board sessions. It checks ORDER, not cost: every stamp of a
// sample is taken after the one before it, in the sequence Instrument::process
// and Part::process take them. What a segment costs is the board's to say
// (shell/cb_profile.cpp, docs/hardware/reva-bring-up.md 10.1).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <array>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>
#include "instrument.h"
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

// Samples of a block whose stamps are not in process order: top ->
// [priority deck: mod, engine, smoothers, grit, flux, comp, send] ->
// [yielding deck: the same seven] -> [mix: observers + taps, blend + duck,
// reverb, return + sleep] -> limiter -> next top. Slot k is taken after slot
// k-1 in each group.
int out_of_order(const Block& b) {
    int bad = 0;
    for (int i = 0; i < b.n; ++i) {
        bool ok = after(b.ts[i], b.p1[i][0]);
        for (int k = 1; k < kSlots; ++k) ok = ok && after(b.p1[i][k - 1], b.p1[i][k]);
        ok = ok && after(b.p1[i][kSlots - 1], b.d1[i]);
        ok = ok && after(b.d1[i], b.p2[i][0]);
        for (int k = 1; k < kSlots; ++k) ok = ok && after(b.p2[i][k - 1], b.p2[i][k]);
        ok = ok && after(b.p2[i][kSlots - 1], b.d2[i]);
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
