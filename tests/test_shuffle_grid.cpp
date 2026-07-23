#include <doctest/doctest.h>

#include <cmath>
#include <initializer_list>
#include <limits>

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

TEST_CASE("shuffle-grid: every computed interior boundary belongs to the new step") {
    for (int steps = 1; steps <= 16; ++steps)
        for (float amount : {0.f, 0.125f, 0.4f, 0.75f, 1.f})
            for (int boundary = 1; boundary < steps; ++boundary) {
                const float edge =
                    shuffle_boundary_phase(boundary, steps, amount);
                INFO("steps=", steps, " amount=", amount,
                     " boundary=", boundary, " edge=", edge);
                CHECK(shuffle_step_index(edge, steps, amount) == boundary);
            }
}

TEST_CASE("shuffle-grid: next representable phase below a boundary stays in the old step") {
    for (int steps = 2; steps <= 16; ++steps)
        for (float amount : {0.f, 0.125f, 0.4f, 0.75f, 1.f})
            for (int boundary = 1; boundary < steps; ++boundary) {
                const float edge =
                    shuffle_boundary_phase(boundary, steps, amount);
                const float below =
                    std::nextafter(edge, -std::numeric_limits<float>::infinity());
                INFO("steps=", steps, " amount=", amount,
                     " boundary=", boundary, " edge=", edge,
                     " below=", below);
                CHECK(shuffle_step_index(below, steps, amount) == boundary - 1);
            }
}

TEST_CASE("shuffle-grid: boundaries are strictly monotonic with positive intervals") {
    for (int steps = 1; steps <= 16; ++steps)
        for (float amount : {0.f, 0.125f, 0.4f, 0.75f, 1.f}) {
            float previous = shuffle_boundary_phase(0, steps, amount);
            CHECK(previous == 0.f);
            for (int boundary = 1; boundary <= steps; ++boundary) {
                const float edge =
                    shuffle_boundary_phase(boundary, steps, amount);
                INFO("steps=", steps, " amount=", amount,
                     " boundary=", boundary, " previous=", previous,
                     " edge=", edge);
                CHECK(edge > previous);
                previous = edge;
            }
            CHECK(previous == 1.f);
        }
}
