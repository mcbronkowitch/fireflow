// The write side of the panel scan is data logic and belongs on the host,
// exactly like shell/controls.h. On the board a wrong address pattern shows
// up as "the wrong knob moved", which is expensive to find; here it is a
// line. Nothing in this file may include a hardware header.
#include <doctest/doctest.h>
#include <set>
#include "../shell/mux_plan.h"

TEST_CASE("mux plan: the address walks 0..15 once per group") {
    for(int g = 0; g < shell::kMuxGroups; ++g)
        for(int a = 0; a < shell::kMuxChannels; ++a)
        {
            const shell::StepPattern p
                = shell::step_pattern(g * shell::kMuxChannels + a);
            CHECK(static_cast<int>(p.address) == a);
        }
}

TEST_CASE("mux plan: exactly one group is enabled per step") {
    // Enables are active low. Two groups on at once shorts two mux outputs
    // onto one sense pin -- silently, and the reading looks plausible.
    for(int s = 0; s < shell::kScanSteps; ++s)
    {
        const shell::StepPattern p = shell::step_pattern(s);
        int low = 0;
        for(int g = 0; g < shell::kMuxGroups; ++g)
            if(((p.enable_mask >> g) & 1u) == 0u) ++low;
        CHECK(low == 1);
        CHECK(((p.enable_mask >> (s / shell::kMuxChannels)) & 1u) == 0u);
    }
}

TEST_CASE("mux plan: a step that does not exist enables nothing") {
    // The safe answer, and not an obvious one: an out-of-range ADDRESS
    // would still select some channel and return a foreign knob's voltage.
    for(int s : {-1, shell::kScanSteps, shell::kScanSteps + 7})
    {
        const shell::StepPattern p = shell::step_pattern(s);
        for(int g = 0; g < shell::kMuxGroups; ++g)
            CHECK(((p.enable_mask >> g) & 1u) == 1u);
    }
}

TEST_CASE("mux plan: every channel is reached exactly once per sweep") {
    std::set<int> seen;
    for(int s = 0; s < shell::kScanSteps; ++s)
        for(int i = 0; i < shell::kSensePins; ++i)
        {
            const int ch = shell::mux_channel(s, i);
            CHECK(ch >= 0);
            CHECK(ch < shell::kMuxTotal);
            seen.insert(ch);
        }
    CHECK(static_cast<int>(seen.size()) == shell::kMuxTotal);
}

TEST_CASE("mux plan: an index that does not exist is answered, not assumed") {
    CHECK(shell::mux_channel(-1, 0) == -1);
    CHECK(shell::mux_channel(0, -1) == -1);
    CHECK(shell::mux_channel(shell::kScanSteps, 0) == -1);
    CHECK(shell::mux_channel(0, shell::kSensePins) == -1);
}

TEST_CASE("mux plan: the LED field cannot collide with address or enable") {
    // One chain carries both, which is why LEDs cost no extra CPU. It is
    // also why an overlap would make a lit LED move a knob.
    const shell::StepPattern p = shell::step_pattern(5);
    const uint32_t no_leds  = shell::chain_word(p, 0u);
    const uint32_t all_leds = shell::chain_word(p, 0x7FFFFu);
    CHECK((no_leds & 0x0Fu) == p.address);
    CHECK(((no_leds >> shell::kEnableShift) & 0x03u) == p.enable_mask);
    CHECK((all_leds & 0xFFu) == (no_leds & 0xFFu));
}
