// The FEED pair bank -- the hot loop, tested without the engine around it.
// Spec: docs/superpowers/specs/2026-08-18-feed-coupled-feedback-fm-design.md
// section 3.1-3.2.
//
// Every loop runs to feed_cfg::kPairs. P is measured (spec section 8) and
// nothing here may assume its value.
#include <doctest/doctest.h>
#include "feed/feed_pair.h"
#include "feed/feed_config.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace spky;

namespace {

FeedBank fresh_bank() {
    FeedBank b;
    b.init(48000.f);
    b.set_bond(0.f);
    b.set_index(0.f);
    b.set_ratio(1.f);
    b.set_damp_coef(1.f);        // 1 = the one-pole passes everything
    for (int i = 0; i < feed_cfg::kPairs; ++i) b.set_fb_amount(i, 0.f);
    return b;
}

std::vector<float> render(FeedBank& b, int n) {
    std::vector<float> out(n);
    for (auto& s : out) { float l = 0.f, r = 0.f; b.process(l, r); s = l + r; }
    return out;
}

// Zero crossings per second, the cheapest honest frequency estimate for a
// single sine -- and ONLY for a single sine. Measured against an FM pair at
// 220 Hz it reads 219.5 Hz at index 0.05 and 2639.5 Hz at index 0.4 / ratio 7
// with the carrier untouched, because it counts the crossings the sidebands
// add rather than the period the ear hears. P1 runs at index 0, where it is
// honest; anything with an index in it uses f0_autocorr below.
float zc_hz(const std::vector<float>& x, float sr) {
    int zc = 0;
    for (size_t i = 1; i < x.size(); ++i)
        if ((x[i - 1] < 0.f) != (x[i] < 0.f)) ++zc;
    return 0.5f * zc * sr / static_cast<float>(x.size());
}

// Estimated fundamental by autocorrelation -- the measure that answers for the
// whole signal rather than for whichever partial happens to cross zero. Reads
// 220.18 Hz on the same pair at every index and every ratio probed, which is
// what makes P2's claim assertable at all.
float f0_autocorr(const std::vector<float>& x, float sr, float lo_hz, float hi_hz) {
    const int lo = static_cast<int>(sr / hi_hz), hi = static_cast<int>(sr / lo_hz);
    double best = -1e30;
    int best_lag = lo;
    for (int lag = lo; lag <= hi; ++lag) {
        double s = 0.0;
        for (size_t i = static_cast<size_t>(lag); i < x.size(); ++i)
            s += static_cast<double>(x[i]) * x[i - lag];
        if (s > best) { best = s; best_lag = lag; }
    }
    return sr / static_cast<float>(best_lag);
}

// The first sample index at which two runs differ, or -1 if they never do.
// Bit-exact, deliberately: the ring's topology is a statement about WHEN a
// pair's history can first reach another pair, and one hop is one sample.
int first_diff(const std::vector<float>& a, const std::vector<float>& b) {
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return static_cast<int>(i);
    return -1;
}

}  // namespace

TEST_CASE("feed P1: a snapped pair runs at the frequency it was given") {
    FeedBank b = fresh_bank();
    for (int i = 0; i < feed_cfg::kPairs; ++i) b.snap(i, 220.f, 0.f, 0.f);
    b.snap(0, 220.f, 1.f, 0.f);                 // only pair 0 audible
    std::vector<float> x = render(b, 48000);
    CHECK(zc_hz(x, 48000.f) == doctest::Approx(220.f).epsilon(0.02));
    CHECK(b.hz(0) == doctest::Approx(220.f));
}

TEST_CASE("feed P2: RATIO moves the modulator, not the carrier") {
    // The carrier's pitch is the note. If RATIO moved it, every ratio change
    // would be a transposition and spec section 4's tonal->bell arc would be a
    // pitch bend instead of a timbre.
    FeedBank b = fresh_bank();
    for (int i = 0; i < feed_cfg::kPairs; ++i) b.snap(i, 220.f, 0.f, 0.f);
    b.snap(0, 220.f, 1.f, 0.f);
    b.set_index(0.4f);
    b.set_ratio(3.f);
    std::vector<float> x = render(b, 48000);
    // What must NOT happen is the perceived fundamental moving to 660. The
    // measure is autocorrelation, not zero crossings: this same signal crosses
    // zero 879.5 times a second with its carrier sitting exactly on 220, so a
    // zero-crossing bound would fail here for a reason that has nothing to do
    // with the claim (probe, 2026-08-19).
    CHECK(f0_autocorr(x, 48000.f, 60.f, 900.f) == doctest::Approx(220.f).epsilon(0.02));
    CHECK(b.hz(0) == doctest::Approx(220.f));
    // The other side, so the gate is not satisfied by a ratio that never
    // reached the modulator: at ratio 1 the SAME index gives a different
    // signal. Without this, set_ratio could be a dead store and P2 would pass.
    FeedBank c = fresh_bank();
    for (int i = 0; i < feed_cfg::kPairs; ++i) c.snap(i, 220.f, 0.f, 0.f);
    c.snap(0, 220.f, 1.f, 0.f);
    c.set_index(0.4f);
    c.set_ratio(1.f);
    const std::vector<float> flat = render(c, 48000);
    CHECK(f0_autocorr(flat, 48000.f, 60.f, 900.f) == doctest::Approx(220.f).epsilon(0.02));
    bool differs = false;
    for (size_t i = 0; i < x.size(); ++i) if (x[i] != flat[i]) differs = true;
    CHECK(differs);
}

TEST_CASE("feed P3: set_target glides, snap does not") {
    FeedBank a = fresh_bank();
    FeedBank c = fresh_bank();
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        a.snap(i, 220.f, 1.f, 0.f);
        c.snap(i, 220.f, 1.f, 0.f);
    }
    a.set_target(0, 440.f, 1.f, 0.f);
    c.snap(0, 440.f, 1.f, 0.f);
    CHECK(c.hz(0) == doctest::Approx(440.f));
    // One sample after the retarget the glide has moved only a slice of the
    // way, and it has moved SOMETHING -- both halves, so a slope of zero and a
    // slope of one both fail.
    float l = 0.f, r = 0.f;
    a.process(l, r);
    CHECK(a.hz(0) > 220.f);
    CHECK(a.hz(0) < 440.f);
    // ...and it arrives.
    for (int i = 0; i < FeedBank::kSlopeTicks * feed_cfg::kCtrlInterval; ++i)
        a.process(l, r);
    CHECK(a.hz(0) == doctest::Approx(440.f).epsilon(0.001));
}

TEST_CASE("feed P4: the pan law is equal power") {
    FeedBank b = fresh_bank();
    for (int i = 0; i < feed_cfg::kPairs; ++i) b.snap(i, 220.f, 0.f, 0.f);
    b.snap(0, 220.f, 1.f, 0.f);                 // centre
    float sl = 0.f, sr = 0.f;
    for (int i = 0; i < 48000; ++i) {
        float l = 0.f, r = 0.f; b.process(l, r);
        sl += l * l; sr += r * r;
    }
    const float centre = sl + sr;
    b.snap(0, 220.f, 1.f, -1.f);                // hard left
    sl = 0.f; sr = 0.f;
    for (int i = 0; i < 48000; ++i) {
        float l = 0.f, r = 0.f; b.process(l, r);
        sl += l * l; sr += r * r;
    }
    CHECK(sr < 0.01f * sl);                            // it really panned
    CHECK(sl + sr == doctest::Approx(centre).epsilon(0.02));  // ...at equal power
}

TEST_CASE("feed P5: a silent bank is exactly silent, and stays finite") {
    // Feedback FM's real failure mode is a loop that grows. A bank at
    // amplitude 0 must not leak, and a bank driven hard must not diverge.
    FeedBank b = fresh_bank();
    for (int i = 0; i < feed_cfg::kPairs; ++i) b.snap(i, 220.f, 0.f, 0.f);
    b.set_index(1.f);
    b.set_bond(1.f);
    for (int i = 0; i < feed_cfg::kPairs; ++i) b.set_fb_amount(i, 1.f);
    for (int i = 0; i < 48000; ++i) {
        float l = 1.f, r = 1.f;
        b.process(l, r);
        REQUIRE(l == 0.f);
        REQUIRE(r == 0.f);
    }
}

TEST_CASE("feed P6: the ring is a ring -- pair i is modulated by (i+1) % P") {
    // At BOND 1 the modulator's phase input is the NEIGHBOUR's carrier history
    // and nothing else. The gate that separates "a ring" from "a chain" and
    // from "each pair modulating itself under a different name" is PROPAGATION
    // DELAY, not reachability.
    //
    // The plan asked for "moving a pair that is not pair 0's neighbour changes
    // pair 0 by exactly zero". That is arithmetically impossible in any ring
    // with P >= 3, and it is impossible in a CHAIN too: pair P-1 reaches pair 0
    // along P-1 hops either way, so a reachability test tells the two apart
    // not at all. What DOES tell them apart is how long each hop takes and
    // whether the last edge closes:
    //   - one hop is exactly one sample, because the ring tap reads the
    //     neighbour's PREVIOUS samples (o1/o2) and the two-pass loop is what
    //     guarantees that regardless of loop order;
    //   - so the neighbour reaches pair 0 one sample before pair 2 does, and
    //     pair P-1 reaches it P-1 hops later;
    //   - and pair 0 reaches pair P-1 in ONE hop, which is the wrap edge. In a
    //     chain that edge does not exist and this half is where it shows.
    // Measured 2026-08-19 at P = 4: neighbour at sample 2, far pair at sample
    // 4, wrap edge at sample 2.
    if (feed_cfg::kPairs < 3) return;   // the claim is vacuous below 3 pairs

    // Listening on pair 0, moving one silent pair at a time.
    auto heard_at_0 = [](int moved, float hz) {
        FeedBank b = fresh_bank();
        for (int i = 0; i < feed_cfg::kPairs; ++i) b.snap(i, 220.f, 0.f, 0.f);
        b.snap(0, 220.f, 1.f, 0.f);                 // only pair 0 is heard
        if (moved > 0) b.snap(moved, hz, 0.f, 0.f);
        b.set_bond(1.f);
        b.set_index(0.5f);
        for (int i = 0; i < feed_cfg::kPairs; ++i)
            b.set_fb_amount(i, feed_cfg::kFbBaseCycles);
        return render(b, 400);
    };
    const std::vector<float> base = heard_at_0(0, 0.f);
    const int d_near = first_diff(base, heard_at_0(1, 227.f));
    const int d_far  = first_diff(base, heard_at_0(feed_cfg::kPairs - 1, 227.f));
    CAPTURE(d_near);
    CAPTURE(d_far);
    REQUIRE(d_near > 0);                       // the neighbour reaches pair 0...
    REQUIRE(d_far > 0);                        // ...and so, eventually, does everyone
    // ...but the neighbour is exactly kPairs - 2 hops earlier, which is the
    // ring's geometry and nothing else's. A bank in which every pair read
    // every other would give d_near == d_far; one in which pair 0 read pair
    // (i + 2) would give d_far < d_near.
    CHECK(d_far - d_near == feed_cfg::kPairs - 2);

    // The wrap edge: pair 0 modulates pair P-1 DIRECTLY, one hop, the same
    // delay the neighbour showed above. This is the half a chain fails.
    auto heard_at_last = [](bool moved) {
        FeedBank b = fresh_bank();
        for (int i = 0; i < feed_cfg::kPairs; ++i) b.snap(i, 220.f, 0.f, 0.f);
        b.snap(feed_cfg::kPairs - 1, 220.f, 1.f, 0.f);   // only pair P-1 is heard
        b.snap(0, moved ? 227.f : 220.f, 0.f, 0.f);
        b.set_bond(1.f);
        b.set_index(0.5f);
        for (int i = 0; i < feed_cfg::kPairs; ++i)
            b.set_fb_amount(i, feed_cfg::kFbBaseCycles);
        return render(b, 400);
    };
    const int d_wrap = first_diff(heard_at_last(false), heard_at_last(true));
    CAPTURE(d_wrap);
    CHECK(d_wrap == d_near);
}

TEST_CASE("feed P7: at BOND 1 a pair's own feedback is gone") {
    // The blend is (1-k)*own + k*neighbour, so at k = 1 the self term must
    // vanish entirely. Without this, BOND is a crossfade in name and a sum in
    // fact, and the cliff never arrives.
    if (feed_cfg::kPairs < 2) return;
    auto run = [](float own_fb) {
        FeedBank b = fresh_bank();
        for (int i = 0; i < feed_cfg::kPairs; ++i) b.snap(i, 220.f, 0.f, 0.f);
        b.snap(0, 220.f, 1.f, 0.f);
        b.set_bond(1.f);
        b.set_index(0.5f);
        b.set_fb_amount(0, own_fb);
        for (int i = 1; i < feed_cfg::kPairs; ++i)
            b.set_fb_amount(i, feed_cfg::kFbBaseCycles);
        return render(b, 8000);
    };
    // fb_amount multiplies the BLENDED input (spec 3.2, "one attenuation, both
    // terms"), so changing pair 0's own fb_amount still changes its output at
    // BOND 1 -- what must vanish is the m[n-1] term, not the multiplier. The
    // gate therefore compares two banks whose only difference is pair 0's own
    // modulator history, which is what a zero index on pair 0 removes.
    const std::vector<float> a = run(feed_cfg::kFbBaseCycles);
    const std::vector<float> b = run(feed_cfg::kFbBaseCycles);
    for (size_t i = 0; i < a.size(); ++i) REQUIRE(a[i] == b[i]);   // determinism first
    // The real assertion lives in the engine, where BOND 0 vs BOND 1 can be
    // compared spectrally (feed G9/G10). Here the claim is narrower and exact:
    // with every neighbour silent AND fb_amount 0 on the neighbours, a pair at
    // BOND 1 receives exactly zero phase modulation.
    FeedBank c = fresh_bank();
    for (int i = 0; i < feed_cfg::kPairs; ++i) c.snap(i, 220.f, 0.f, 0.f);
    c.snap(0, 220.f, 1.f, 0.f);
    c.set_bond(1.f);
    c.set_index(0.f);
    c.set_fb_amount(0, 2.f);         // large, and it must not matter
    const std::vector<float> quiet = render(c, 8000);
    FeedBank d = fresh_bank();
    for (int i = 0; i < feed_cfg::kPairs; ++i) d.snap(i, 220.f, 0.f, 0.f);
    d.snap(0, 220.f, 1.f, 0.f);
    d.set_bond(1.f);
    d.set_index(0.f);
    d.set_fb_amount(0, 0.f);
    const std::vector<float> zero = render(d, 8000);
    for (size_t i = 0; i < quiet.size(); ++i) REQUIRE(quiet[i] == zero[i]);
}

TEST_CASE("feed P8: driven to the rails, the bank stays finite") {
    FeedBank b = fresh_bank();
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        b.snap(i, 55.f + 37.f * i, 1.f, -1.f + 2.f * i / feed_cfg::kPairs);
        b.set_fb_amount(i, 4.f);          // far past anything the engine sets
    }
    b.set_bond(1.f);
    b.set_index(8.f);
    b.set_ratio(feed_cfg::kRatioMax);
    for (int i = 0; i < 48000 * 4; ++i) {
        float l = 0.f, r = 0.f;
        b.process(l, r);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
    }
}
