#include "workload.h"
#include "serial_arena.h"
#include "feed/feed_pair.h"
#include "feed/feed_config.h"

namespace bench {
namespace {

using namespace spky;

// Rows run strictly serially in table order, so their state shares one arena
// slot -- the pattern workloads_body.cpp and workloads_system.cpp use.
struct FeedBankGroup {
    FeedBank bank;
    int      tick;
};

SerialArena<FeedBankGroup> g_feed_arena;

// The worst case the ring can be in: every pair audible, every pair retargeted
// every control tick so no slope is ever zero, BOND at the coupled end so the
// ring taps are live, and an index high enough that fast_sin's argument is
// never a constant the compiler can hoist.
void setup_feed_pairs()
{
    auto& g = g_feed_arena.emplace<FeedBankGroup>();
    g.bank.init(kSampleRate);
    g.tick = 0;
    g.bank.set_bond(0.7f);
    g.bank.set_index(feed_cfg::kIndexMaxCycles);
    g.bank.set_ratio(3.5f);
    g.bank.set_damp_coef(0.3f);
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        g.bank.snap(i, 110.f + 13.f * i, 1.f / feed_cfg::kPairs,
                    -1.f + 2.f * static_cast<float>(i) / feed_cfg::kPairs);
        g.bank.set_fb_amount(i, feed_cfg::kFbBaseCycles);
    }
}

float proc_feed_pairs()
{
    auto& g = g_feed_arena.get<FeedBankGroup>();
    // One control tick per block, the WHOLE bank retargeted -- FeedEngine
    // retargets every pair every tick. There is no round-robin slice: P is
    // small by construction and the allocation is a chord read, not a spectral
    // map. Pricing anything less would price a loop the engine does not run.
    const float wob = (g.tick & 1) ? 1.002f : 0.998f;
    for (int i = 0; i < feed_cfg::kPairs; ++i)
        g.bank.set_target(i, (110.f + 13.f * i) * wob, 1.f / feed_cfg::kPairs,
                          -1.f + 2.f * static_cast<float>(i) / feed_cfg::kPairs);
    ++g.tick;

    float acc = 0.f;
    for (size_t n = 0; n < kBlock; ++n) {
        float l = 0.f, r = 0.f;
        g.bank.process(l, r);
        acc += l + r;
    }
    return acc;
}

}  // namespace

// ONE row, several builds -- not several rows. P is a compile-time constant,
// so "P = 4 against P = 8" is two images, not two rows, and a row that
// instantiated a second bank at a second P would double the icache footprint
// and price neither honestly. The sweep is: build, measure, edit
// feed_cfg::kPairs, rebuild, measure. The per-pair cost is the slope of that
// line, and the whole point of taking three points is that a single point
// cannot tell a linear loop from one with a fixed overhead.
const Workload kFeedWorkloads[] = {
    { "feed", "feed_pairs", setup_feed_pairs, proc_feed_pairs },
};
const int kFeedCount = sizeof(kFeedWorkloads) / sizeof(kFeedWorkloads[0]);

}  // namespace bench
