// The value path from a raw mux word to a normalised control value. Pure
// data logic, no hardware type: on the board a wrong clamp or a band that
// never lets go shows up as "the knob does not reach its stop", which is
// expensive to find. Spec: docs/superpowers/specs/
// 2026-09-28-coupon-panel-scan-design.md section 5.
#include <doctest/doctest.h>
#include "../shell/scan_value.h"

namespace {
constexpr shell::Span kSpan{100, 60100, true};   // 60000 counts wide
constexpr int kH = 32;
}

TEST_CASE("scan value: normalise maps the span onto 0..1 and clamps") {
    CHECK(shell::span_normalize(100, kSpan) == doctest::Approx(0.0f));
    CHECK(shell::span_normalize(60100, kSpan) == doctest::Approx(1.0f));
    CHECK(shell::span_normalize(30100, kSpan) == doctest::Approx(0.5f));
    CHECK(shell::span_normalize(0, kSpan) == doctest::Approx(0.0f));
    CHECK(shell::span_normalize(65535, kSpan) == doctest::Approx(1.0f));
}

TEST_CASE("scan value: an invalid or inverted span normalises to 0") {
    const shell::Span invalid{100, 60100, false};
    const shell::Span inverted{60100, 100, true};
    CHECK(shell::span_normalize(30100, invalid) == doctest::Approx(0.0f));
    CHECK(shell::span_normalize(30100, inverted) == doctest::Approx(0.0f));
}

TEST_CASE("scan value: nothing is emitted before the span is valid") {
    shell::PotFilter f{};
    float v = -1.0f;
    const shell::Span invalid{100, 60100, false};
    CHECK_FALSE(shell::pot_filter(f, 30100, invalid, kH, &v));
    CHECK(v == doctest::Approx(-1.0f));
    CHECK_FALSE(f.emitted);
}

TEST_CASE("scan value: the first valid reading is always emitted") {
    shell::PotFilter f{};
    float v = -1.0f;
    CHECK(shell::pot_filter(f, 30100, kSpan, kH, &v));
    CHECK(v == doctest::Approx(0.5f));
}

TEST_CASE("scan value: a move inside the band is silent, beyond it emits") {
    shell::PotFilter f{};
    float v = 0.0f;
    REQUIRE(shell::pot_filter(f, 30100, kSpan, kH, &v));
    CHECK_FALSE(shell::pot_filter(f, 30100 + kH, kSpan, kH, &v));
    CHECK_FALSE(shell::pot_filter(f, 30100 - kH, kSpan, kH, &v));
    CHECK(shell::pot_filter(f, 30100 + kH + 1, kSpan, kH, &v));
    CHECK(v == doctest::Approx((30000.0f + kH + 1) / 60000.0f));
}

TEST_CASE("scan value: the band is measured from the last EMITTED reading") {
    // A slow drift of half a band per read must still emit once it has
    // added up; measuring from the last SEEN reading would never emit.
    shell::PotFilter f{};
    float v = 0.0f;
    REQUIRE(shell::pot_filter(f, 30100, kSpan, kH, &v));
    CHECK_FALSE(shell::pot_filter(f, 30100 + kH / 2, kSpan, kH, &v));
    CHECK_FALSE(shell::pot_filter(f, 30100 + kH, kSpan, kH, &v));
    CHECK(shell::pot_filter(f, 30100 + kH + kH / 2, kSpan, kH, &v));
}

TEST_CASE("scan value: both stops are reachable through the band") {
    shell::PotFilter f{};
    float v = 0.5f;
    // Just outside the low snap region, then one count into it: a move of 1,
    // far inside the band, must still deliver an exact 0.
    REQUIRE(shell::pot_filter(f, 100 + kH + 1, kSpan, kH, &v));
    CHECK(v > 0.0f);
    CHECK(shell::pot_filter(f, 100 + kH, kSpan, kH, &v));
    CHECK(v == 0.0f);
    // Staying in the region is silent.
    CHECK_FALSE(shell::pot_filter(f, 100, kSpan, kH, &v));

    shell::PotFilter g{};
    REQUIRE(shell::pot_filter(g, 60100 - kH - 1, kSpan, kH, &v));
    CHECK(v < 1.0f);
    CHECK(shell::pot_filter(g, 60100 - kH, kSpan, kH, &v));
    CHECK(v == 1.0f);
    CHECK_FALSE(shell::pot_filter(g, 60100, kSpan, kH, &v));
}

TEST_CASE("scan value: the hysteresis band obeys the spec's rule") {
    CHECK(shell::kPotHysteresis >= 16);
    CHECK(shell::kPotHysteresis % 16 == 0);
}

TEST_CASE("scan value: the panel span is the two calibration channels") {
    const shell::Span s = shell::panel_span(31, 63484);
    CHECK(s.valid);
    CHECK(s.zero == 31);
    CHECK(s.rail == 63484);
}

TEST_CASE("scan value: a collapsed rail or a lifted zero is not a panel span") {
    CHECK_FALSE(shell::panel_span(31, shell::kRailFloor - 1).valid);
    CHECK(shell::panel_span(31, shell::kRailFloor).valid);
    CHECK_FALSE(shell::panel_span(shell::kRailMargin + 1, 63484).valid);
    CHECK(shell::panel_span(shell::kRailMargin, 63484).valid);
}

TEST_CASE("scan value: an invalid span emits nothing before a valid one") {
    // Boot: the play images start with an invalid span and nothing may reach
    // the engine until a sweep has measured one (Review Focus 2).
    shell::PotFilter f;
    float v = -1.0f;
    CHECK_FALSE(shell::pot_filter(f, 30000, shell::Span{0, 0, false}, 16, &v));
    CHECK(v == -1.0f);
    CHECK(shell::pot_filter(f, 30000, shell::panel_span(31, 63484), 16, &v));
}
