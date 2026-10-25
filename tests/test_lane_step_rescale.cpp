#include <doctest/doctest.h>
#include "mod/lane.h"
#include "mod/shuffle_grid.h"
#include <cstdint>
#include <cstring>
#include <vector>

using namespace spky;

// set_step's live re-slot (fault R4: a TIDE turn re-slots four lanes per deck
// in one control block). The cheap form -- the old step from the grid cache,
// the wrap by subtraction, the new step tested against int(pos) -- must land
// on the same phase and step as the search + fmod form it replaced. One lane
// re-slotted every 480 samples to a pseudo-random count in 1..64, with
// SHUFFLE moving and kicks in between, run with the reference form
// (set_step_cache_for_test(false)) and with the cheap one: output, step and
// phase bits must match on every sample.
namespace {

constexpr int kSamples = 48000 * 20;
constexpr int kEvery = 480;

struct Run {
    std::vector<float> out, phase;
    std::vector<int>   step;
    int reslots = 0;       // set_step calls that took the live re-slot branch
    int wraps = 0;         // ... of them with the position past the new count
};

uint32_t next(uint32_t& s) { s = s * 1664525u + 1013904223u; return s >> 8; }

Run run(bool cheap, bool melodic) {
    ModLane::set_step_cache_for_test(cheap);
    ModLane l;
    l.set_melodic(melodic);
    l.init(48000.f, 777);
    l.set_step(true, 8);
    l.set_rate_hz(3.f);
    l.set_smooth(0.2f);
    l.set_shuffle(0.6f);
    uint32_t seed = 12345;
    Run r;
    r.out.reserve(kSamples); r.phase.reserve(kSamples); r.step.reserve(kSamples);
    for (int i = 0; i < kSamples; ++i) {
        if (i > 0 && i % kEvery == 0) {
            const int slots = 1 + static_cast<int>(next(seed) % 64u);
            if (l.cur_step() >= 0 && slots != l.steps()) {
                ++r.reslots;
                // The position the re-slot carries: the step index plus the
                // fraction in it, at least the step index.
                r.wraps += l.cur_step() >= slots;
            }
            // Every third re-slot right behind a kick, no sample between:
            // the phase has left _cur_step's interval and only the grid
            // knows the step it now sits in (the same-tick SPOT + STEPS
            // case set_step's comment names).
            if ((i / kEvery) % 3 == 0) l.kick(static_cast<float>(next(seed) % 1000u) / 1000.f - 0.5f, 0.f);
            l.set_step(true, slots);
        }
        if (i % (kEvery * 7) == 240) l.set_shuffle(static_cast<float>(next(seed) % 1000u) / 999.f);
        if (i % (kEvery * 5) == 120) l.kick(static_cast<float>(next(seed) % 1000u) / 1000.f - 0.5f, 0.f);
        r.out.push_back(l.process());
        r.phase.push_back(l.phase());
        r.step.push_back(l.cur_step());
    }
    ModLane::set_step_cache_for_test(true);
    return r;
}

void compare(bool melodic) {
    const Run ref = run(false, melodic);
    const Run dut = run(true, melodic);
    long out_d = 0, phase_d = 0, step_d = 0;
    for (int i = 0; i < kSamples; ++i) {
        out_d   += std::memcmp(&ref.out[i], &dut.out[i], sizeof(float)) != 0;
        phase_d += std::memcmp(&ref.phase[i], &dut.phase[i], sizeof(float)) != 0;
        step_d  += ref.step[i] != dut.step[i];
    }
    CHECK(out_d == 0);
    CHECK(phase_d == 0);
    CHECK(step_d == 0);
    // Both runs really re-slotted live, and often past the new count, so the
    // wrap is exercised and the test does not compare two idle lanes.
    CHECK(ref.reslots > 1500);
    CHECK(ref.wraps > 200);
    CHECK(dut.reslots == ref.reslots);
}

} // namespace

TEST_CASE("lane STEP re-slot: the cheap form lands on the search + fmod form's bits (texture lane)") {
    compare(false);
}

TEST_CASE("lane STEP re-slot: the cheap form lands on the search + fmod form's bits (melodic lane)") {
    compare(true);
}

// The guess test inside shuffle_step_index_near(), wrong guesses included:
// the lane run above almost never hands it a wrong one, so its fallback is
// checked here against the search directly, over a phase sweep through
// every interval and past both outer edges.
TEST_CASE("shuffle_step_index_near returns the search's step for any guess") {
    const float amounts[] = {0.f, 0.3f, 0.6f, 1.f};
    long checked = 0, wrong_guesses = 0, mismatches = 0;
    for (int steps = 1; steps <= 64; ++steps)
        for (float a : amounts)
            for (int k = -2; k <= 4002; ++k) {
                const float phase = static_cast<float>(k) / 4000.f;
                const int want = shuffle_step_index(phase, steps, a);
                for (int guess = want - 2; guess <= want + 2; ++guess) {
                    mismatches += shuffle_step_index_near(phase, steps, a, guess) != want;
                    wrong_guesses += guess != want;
                    ++checked;
                }
            }
    CHECK(mismatches == 0);
    CHECK(wrong_guesses > checked / 2);
}
