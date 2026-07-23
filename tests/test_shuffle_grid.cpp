#include <doctest/doctest.h>

#include <initializer_list>

#include "mod/shuffle_grid.h"

using namespace spky;

TEST_CASE("shuffle-grid: endpoints are straight and two-to-one") {
    CHECK(shuffle_boundary_phase(1, 8, 0.f) == doctest::Approx(1.f / 8.f));
    CHECK(shuffle_boundary_phase(1, 8, 1.f) == doctest::Approx((4.f / 3.f) / 8.f));
    CHECK(shuffle_step_length(0, 8, 1.f) == doctest::Approx(4.f / 3.f));
    CHECK(shuffle_step_length(1, 8, 1.f) == doctest::Approx(2.f / 3.f));
}

TEST_CASE("shuffle-grid: pairs and cycles keep their duration") {
    for (float s : {0.f, 0.25f, 0.5f, 1.f}) {
        for (int step = 0; step < 8; step += 2)
            CHECK(shuffle_step_length(step, 8, s)
                + shuffle_step_length(step + 1, 8, s) == doctest::Approx(2.f));
        CHECK(shuffle_boundary_phase(8, 8, s) == doctest::Approx(1.f));
    }
}

TEST_CASE("shuffle-grid: odd final step stays straight") {
    CHECK(shuffle_step_length(4, 5, 1.f) == doctest::Approx(1.f));
    CHECK(shuffle_boundary_phase(4, 5, 1.f) == doctest::Approx(4.f / 5.f));
    CHECK(shuffle_boundary_phase(5, 5, 1.f) == doctest::Approx(1.f));
}

TEST_CASE("shuffle-grid: position round-trips") {
    for (int steps : {1, 2, 5, 8, 16})
        for (float s : {0.f, 0.4f, 1.f})
            for (int i = 0; i < 100; ++i) {
                float ph = static_cast<float>(i) / 100.f;
                int step = shuffle_step_index(ph, steps, s);
                float pos = static_cast<float>(step)
                    + shuffle_step_fraction(ph, step, steps, s);
                CHECK(shuffle_phase_for_position(pos, steps, s)
                    == doctest::Approx(ph).epsilon(0.0001));
            }
}
