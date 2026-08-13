#include "doctest/doctest.h"

#include "vcv/src/pad_geometry.hpp"
#include "vcv/src/generated_flow_panel.hpp"

namespace geometry = spkyvcv::pad_geometry;

TEST_CASE("pad geometry: Catmull-Rom midpoint follows the closed-curve basis") {
    const geometry::Point midpoint = geometry::catmullRom(
        {-1.f, 1.f}, {-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, 0.5f);

    CHECK(midpoint.x == doctest::Approx(0.f));
    CHECK(midpoint.y == doctest::Approx(-1.25f));
}

TEST_CASE("pad geometry: closed spline accepts its centre and rejects a far point") {
    const geometry::Point points[] = {
        {-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f},
        {0.2f, 1.f}, {0.f, 0.2f}, {-0.2f, 1.f}, {-1.f, 1.f},
    };

    CHECK(geometry::pointInClosedCatmullRom(points, 7, {0.f, 0.f}));
    CHECK_FALSE(geometry::pointInClosedCatmullRom(points, 7, {4.f, 4.f}));
}

TEST_CASE("pad geometry: concave gap remains outside the closed spline") {
    const geometry::Point points[] = {
        {-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f},
        {0.2f, 1.f}, {0.f, 0.2f}, {-0.2f, 1.f}, {-1.f, 1.f},
    };

    CHECK_FALSE(geometry::pointInClosedCatmullRom(points, 7, {0.f, 0.8f}));
}

TEST_CASE("pad geometry: curved edge distinguishes immediately inside and outside") {
    const geometry::Point square[] = {
        {-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f},
    };

    CHECK(geometry::pointInClosedCatmullRom(square, 4, {0.f, -1.2498f}));
    CHECK_FALSE(geometry::pointInClosedCatmullRom(square, 4, {0.f, -1.2502f}));
}

TEST_CASE("pad geometry: flattened boundary is clickable within tolerance") {
    const geometry::Point square[] = {
        {-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f},
    };

    CHECK(geometry::pointInClosedCatmullRom(square, 4, {0.f, -1.25005f}));
}

TEST_CASE("pad geometry: fewer than three anchors are rejected safely") {
    const geometry::Point points[] = {{0.f, 0.f}, {1.f, 0.f}};

    CHECK_FALSE(geometry::pointInClosedCatmullRom(nullptr, 0, {0.f, 0.f}));
    CHECK_FALSE(geometry::pointInClosedCatmullRom(points, 1, {0.f, 0.f}));
    CHECK_FALSE(geometry::pointInClosedCatmullRom(points, 2, {0.f, 0.f}));
}

TEST_CASE("pad geometry: generated lower pad excludes documented silver and black gaps") {
    const auto& pad = spkyvcv::glow::kPadShapes[3];

    CHECK(geometry::pointInClosedCatmullRom(
        pad.points, pad.pointCount, pad.centre));
    // Inside the left documented silver-decoration polygon, above P03.
    CHECK_FALSE(geometry::pointInClosedCatmullRom(
        pad.points, pad.pointCount, {5.f, 96.f}));
    // Inside P03's rectangular bounds but in the black gap at its upper right.
    CHECK_FALSE(geometry::pointInClosedCatmullRom(
        pad.points, pad.pointCount, {16.5f, 96.f}));
}
