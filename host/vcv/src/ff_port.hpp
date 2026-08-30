#pragma once
// FireFlow's own jack. Both panels print a well under every jack, and the
// stock PJ301MPort covered it with a chrome ring -- the last thing on either
// plate that still read as Rack's component library rather than as this
// instrument. FfPort keeps the barrel dark and the ring pewter.
//
// No colour coding, on purpose (owner's call, 2026-08-30). Position would be
// the only thing to code by, and position lies here: on the hardware plate IN
// sits at the far left and OUT at the far right, so a side tint would file
// two global jacks under deck A and deck B.
//
// Footprint is the stock port's to the millimetre. Rack lands a cable end on
// the port's centre and sizes nothing by the widget, but the plug it draws on
// top assumes a jack of that size -- and every printed well on both plates was
// laid out around it.
#include "plugin.hpp"
#include "generated_panel.hpp"
#include "ff_knob.hpp"   // ffRgb

namespace spkyvcv {

// Drawing taste, not data.
static constexpr float kPortRingW = 0.55f;  // mm
// 1.30 was the printed hole's radius on both plates. Opened up by eye on
// 2026-08-30 -- at 1.30 the barrel read as a dot rather than as something a
// plug goes into. The print underneath is covered either way.
static constexpr float kPortHoleR = 1.50f;  // mm

struct FfPort : app::PortWidget {
    FfPort() {
        const float d = mm2px(2.f * kFfPortR);
        box.size = math::Vec(d, d);
    }

    void draw(const DrawArgs& args) override {
        const float cx = box.size.x * 0.5f, cy = box.size.y * 0.5f;
        const float R = std::min(cx, cy);
        const float ringW = mm2px(kPortRingW);

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, R - ringW * 0.5f);
        nvgFillColor(args.vg, ffRgb(kFfPortWell));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, ffRgb(kFfPortRing));
        nvgStrokeWidth(args.vg, ringW);
        nvgStroke(args.vg);

        // The hole. Once a cable is patched Rack draws its plug over most of
        // this, which is why the ring carries the design and the centre stays
        // plain -- the ring is the part that survives a full patch.
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, mm2px(kPortHoleR));
        nvgFillColor(args.vg, ffRgb(kFfPortHole));
        nvgFill(args.vg);

        PortWidget::draw(args);
    }
};

} // namespace spkyvcv
