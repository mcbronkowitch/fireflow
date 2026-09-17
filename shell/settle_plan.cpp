#include "settle_plan.h"

namespace shell {

// Section 6's table. Each target's source impedance is the switch on-
// resistance plus what the netlist wires to that channel, and the two
// reference channels bracket the pot range the panel will actually use --
// REF_A at 5.15k sits on the model's 20k-pot row, REF_B an order of
// magnitude below it.
const SettlePair kSettlePlan[kSettlePairs] = {
    // group from  to   R_src  9.01 tau  reference
    {0, 1, 10, 150, 88, true},      // P0  R_HI1 (A+3V3) -> R_SP10 (AGND)
    {0, 7, 8, 5150, 3016, false},   // P1  R_LO2 (AGND)  -> REF_A
    {0, 5, 8, 5150, 3016, false},   // P2  R_HI2 (A+3V3) -> REF_A
    {0, 7, 9, 650, 381, false},     // P3  R_LO2 (AGND)  -> REF_B
    {1, 7, 6, 5150, 1856, false},   // P4  R_LO4 (AGND)  -> REF_C
    {1, 5, 3, 150, 54, true},       // P5  R_HI4 (A+3V3) -> R_LO3 (AGND)
};

int d_settle_index(const Point* pts, int n, int32_t settled)
{
    if(pts == nullptr || n <= 0) return -1;

    // Walk backwards. The definition is "settled from here on", so the answer
    // is the start of the final settled run -- which a forward scan can only
    // find by rescanning the tail at every candidate.
    int first = -1;
    for(int i = n - 1; i >= 0; --i)
    {
        const int32_t d = pts[i].mean - settled;
        const int32_t a = (d < 0) ? -d : d;
        if(a > kSettleCounts) break;
        first = i;
    }
    return first;
}

Gates settle_gates(const RunSummary& s)
{
    Gates g{};

    // G1: both reference pairs must collapse to the first grid step or the
    // next. They step between 0 ohm ties, so anything slower is the
    // instrument, and the section 7 subtraction would be a guess.
    g.g1_knee = true;
    for(int p = 0; p < kSettlePairs; ++p)
    {
        if(!kSettlePlan[p].is_reference) continue;
        if(s.knee_ns[p] < 0 || s.knee_ns[p] > kKneeMaxNs) g.g1_knee = false;
    }

    // G2: the measured noise floor must leave the mean's own uncertainty well
    // inside the decision threshold.
    g.g2_floor = s.b0 >= 0 && s.b0 <= kFloorMaxCounts;

    // G3: no point may be wider than three floors -- but never demand better
    // than the criterion itself, or a quiet run fails for being quiet.
    const int32_t allowed = (kBandFactor * s.b0 > kSettleCounts)
                                ? kBandFactor * s.b0
                                : static_cast<int32_t>(kSettleCounts);
    g.g3_band = s.widest_band >= 0 && s.widest_band <= allowed;

    // G4: aperture jitter inside one grid step, and a latency the conversion
    // model does not forbid. A negative mean means the 2034 ns subtraction is
    // wrong, which invalidates every delay in the run.
    g.g4_jitter = s.lat_mean_ns >= 0 && s.lat_max_ns >= s.lat_min_ns
                  && (s.lat_max_ns - s.lat_min_ns) <= kJitterMaxNs;

    return g;
}

} // namespace shell
