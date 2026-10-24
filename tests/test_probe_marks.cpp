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

struct Block {
    std::vector<uint32_t>                ts, d1, d2;
    std::vector<std::array<uint32_t, 2>> p1, p2;
    int                                  n, pri;
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
                 const ProbePartSlots* p1, const ProbePartSlots* p2, int n, int pri)
{
    Block b;
    b.n   = n;
    b.pri = pri;
    b.ts.assign(ts, ts + n + 1);
    b.d1.assign(d1, d1 + n);
    b.d2.assign(d2, d2 + n);
    for (int i = 0; i < n; ++i) {
        b.p1.push_back({p1[i][0], p1[i][1]});
        b.p2.push_back({p2[i][0], p2[i][1]});
    }
    g_blocks.push_back(std::move(b));
}
} // namespace spky

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
        for (int i = 0; i < b.n; ++i) {
            // top -> [priority deck: mod, engine, fx] -> [yielding deck: mod,
            // engine, fx] -> mix -> next top.
            CHECK(after(b.ts[i], b.p1[i][0]));
            CHECK(after(b.p1[i][0], b.p1[i][1]));
            CHECK(after(b.p1[i][1], b.d1[i]));
            CHECK(after(b.d1[i], b.p2[i][0]));
            CHECK(after(b.p2[i][0], b.p2[i][1]));
            CHECK(after(b.p2[i][1], b.d2[i]));
            CHECK(after(b.d2[i], b.ts[i + 1]));
        }
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
