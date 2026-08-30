#pragma once
// FireFlow's own keycap. The stock VCVLatch was a 6.1 mm circle sitting on an
// 8 mm printed square, so cap and bed read as two objects; FfPad is the square,
// at the bed's own size, in the family FfKnob and FfPort belong to -- dark cap,
// accent edge. Owner's pick, 2026-08-30.
//
// It derives from app::Switch, not SvgSwitch. SvgSwitch's `latch` only picks
// which SVG frame shows while the mouse is down; the actual toggle-on-click
// behaviour is Switch's own, from `momentary = false`. So nothing is lost by
// leaving the SVG path behind.
//
// The accent edge is honest only because the generator makes it so: MOD and
// SHFT sit at the two ends of the jack row and would read as deck B and deck A
// by position, so gen_hw_panel.py's GLOBAL_KEYS hands them the neutral centre
// accent instead. See pad_accent() there.
#include "plugin.hpp"
#include "generated_panel.hpp"
#include "ff_knob.hpp"   // ffRgb

namespace spkyvcv {

// Drawing taste, not data.
static constexpr float kPadEdgeW = 0.42f;   // mm
static constexpr float kPadRadius = 1.20f;  // mm, corner rounding -- the rx the
                                            // plate prints under the cap
// How much of the accent an engaged cap takes. Full strength would out-shout
// the LED beside it, which is the control that actually reports the state.
static constexpr float kPadOnMix = 0.34f;

struct FfPad : app::Switch {
    NVGcolor accent = ffRgb(kFfKnobRim);

    // Keeps the centre. The two panels print different bed sizes, so the hw
    // widget re-sizes AFTER createParamCentered has already placed it from the
    // constructor's size -- resizing about the origin would shift it.
    void setRadiusMm(float rMm) {
        const math::Vec c = box.pos.plus(box.size.div(2.f));
        const float d = mm2px(2.f * rMm);
        box.size = math::Vec(d, d);
        box.pos = c.minus(box.size.div(2.f));
    }

    void setAccent(const FfAccent& a) { accent = ffRgb(a.rgbA); }

    void draw(const DrawArgs& args) override {
        engine::ParamQuantity* pq = getParamQuantity();
        const bool on = pq && pq->getValue() > pq->getMinValue() + 0.5f;

        const float edge = mm2px(kPadEdgeW);
        const float x = edge * 0.5f, y = edge * 0.5f;
        const float w = box.size.x - edge, h = box.size.y - edge;

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, x, y, w, h, mm2px(kPadRadius));
        nvgFillColor(args.vg, on
            ? nvgLerpRGBA(ffRgb(kFfPortWell), accent, kPadOnMix)
            : ffRgb(kFfPortWell));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, accent);
        nvgStrokeWidth(args.vg, edge);
        nvgStroke(args.vg);

        Switch::draw(args);
    }
};

// A latch toggles on click; a momentary cap is high only while held. Rack's
// own names for the same split are VCVLatch and VCVButton.
struct FfPadLatch : FfPad {
    FfPadLatch() { momentary = false; setRadiusMm(kFfPadR); }
};

struct FfPadMomentary : FfPad {
    FfPadMomentary() { momentary = true; setRadiusMm(kFfPadR); }
};

} // namespace spkyvcv
