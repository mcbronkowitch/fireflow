#include "doctest/doctest.h"

#include "vcv/src/glow_panel.hpp"

#include <string_view>

namespace panel = spkyvcv::glow_panel;

TEST_CASE("glow panel manifest preserves rear, faceplate, touch order and names") {
    REQUIRE(panel::kLayers.size() == 3);

    CHECK(panel::kLayers[0].layer == panel::Layer::Rear);
    CHECK(std::string_view(panel::kLayers[0].resourcePath) ==
          "res/GlowRear.png");
    CHECK(panel::kLayers[1].layer == panel::Layer::Faceplate);
    CHECK(std::string_view(panel::kLayers[1].resourcePath) ==
          "res/GlowFaceplate.png");
    CHECK(panel::kLayers[2].layer == panel::Layer::Touch);
    CHECK(std::string_view(panel::kLayers[2].resourcePath) ==
          "res/GlowTouch.png");
}

TEST_CASE("glow panel manifest requires every visual layer") {
    for (const auto& layer : panel::kLayers)
        CHECK(layer.required);
}

TEST_CASE("glow toggle preserves the existing 0, 1, 2 switch range") {
    CHECK(panel::switchFrameIndex(0.f) == 0);
    CHECK(panel::switchFrameIndex(1.f) == 1);
    CHECK(panel::switchFrameIndex(2.f) == 2);
}

TEST_CASE("glow toggle rounds near integer positions and clamps extremes") {
    CHECK(panel::switchFrameIndex(-100.f) == 0);
    CHECK(panel::switchFrameIndex(-0.49f) == 0);
    CHECK(panel::switchFrameIndex(0.49f) == 0);
    CHECK(panel::switchFrameIndex(0.51f) == 1);
    CHECK(panel::switchFrameIndex(1.49f) == 1);
    CHECK(panel::switchFrameIndex(1.51f) == 2);
    CHECK(panel::switchFrameIndex(2.49f) == 2);
    CHECK(panel::switchFrameIndex(100.f) == 2);
}

TEST_CASE("each missing required layer selects fallback without changing 16 HP") {
    const bool complete[] = {true, true, true};
    const bool rearMissing[] = {false, true, true};
    const bool faceplateMissing[] = {true, false, true};
    const bool touchMissing[] = {true, true, false};

    CHECK(panel::allRequiredLayersAvailable(complete, 3));
    CHECK_FALSE(panel::allRequiredLayersAvailable(rearMissing, 3));
    CHECK_FALSE(panel::allRequiredLayersAvailable(faceplateMissing, 3));
    CHECK_FALSE(panel::allRequiredLayersAvailable(touchMissing, 3));
    CHECK(panel::kPanelWidthHp == 16);
}

TEST_CASE("incomplete availability input selects fallback safely") {
    const bool incomplete[] = {true, true};

    CHECK_FALSE(panel::allRequiredLayersAvailable(nullptr, 0));
    CHECK_FALSE(panel::allRequiredLayersAvailable(incomplete, 2));
}
