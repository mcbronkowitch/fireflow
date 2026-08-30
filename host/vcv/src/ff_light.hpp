#pragma once
// FireFlow's own lamp. The plates have printed a zone-tinted dot under every
// LED since the accent scheme existed; a saturated Rack YellowLight covered it
// with the one colour neither plate uses. FfLight lights it in the colour the
// print already promised.
//
// This one keeps Rack's drawing. ModuleLightWidget already paints exactly the
// shape wanted -- bed, bezel, glow, halo -- and only its colours were wrong,
// so unlike FfKnob, FfPort and FfPad there is no draw() here. The colours come
// from kLightAccent; the bed and bezel from the same generator that prints
// them (spec: none -- owner's call, 2026-08-30).
//
// The one lamp that takes no side colour is REC: record-is-red is a device
// convention rather than Rack's, and it reports real writing. See LED_REC in
// res/gen_panel.py.
#include "plugin.hpp"
#include "generated_panel.hpp"
#include "ff_knob.hpp"   // ffRgb

namespace spkyvcv {

struct FfLight : app::ModuleLightWidget {
    FfLight() {
        bgColor = ffRgb(kFfLedBed);
        borderColor = ffRgb(kFfLedBezel);
        const float d = mm2px(2.f * kFfLedR);
        box.size = math::Vec(d, d);
    }

    // One channel per lamp, so this replaces rather than appends -- calling it
    // twice must not leave a light mixing two colours.
    void setAccent(const FfAccent& a) {
        baseColors.clear();
        addBaseColor(ffRgb(a.rgbA));
    }
};

} // namespace spkyvcv
