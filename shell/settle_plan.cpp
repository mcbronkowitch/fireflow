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

} // namespace shell
