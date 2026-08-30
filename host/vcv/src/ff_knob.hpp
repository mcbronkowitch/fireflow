#pragma once
// FireFlow's own knob. Both panels used to wear the stock RoundBlackKnob and
// Trimpot, which are side-blind: nothing on a black cap says whether the hand
// is on deck A or deck B. FfKnob keeps the dark cap and adds the two things
// that carry the side -- a collar at the outer edge and the pointer -- in the
// deck accent. Owner's pick, 2026-08-30, of three mocked designs; the two he
// turned down were a full accent-coloured cap (too loud for a plate that
// spends its accent sparingly) and a value arc (no aluminium pot can do it).
//
// It derives from app::Knob, not SvgKnob, so dragging, snapping, tooltips, the
// right-click menu and Rack's global radial-knob setting all still come from
// the framework; only the painting is ours. Nothing here holds a colour or a
// radius of its own: kParamAccent, kFfKnobCap/Rim and kFfKnobR* are generated
// from res/gen_panel.py, the same rule kModRing follows.
#include "plugin.hpp"
#include "generated_panel.hpp"

namespace spkyvcv {

// Drawing taste, not data -- these describe the stroke, not the instrument.
static constexpr float kCollarW = 0.55f;   // mm, the accent ring's width
static constexpr float kCollarGap = 0.25f; // mm, cap to collar
static constexpr float kPointerW = 0.45f;  // mm
static constexpr float kPointerTip = 0.35f;// mm, how far short of the cap edge
// The accent is chosen to read on a PLATE. On a graphite cap the darker of
// them -- the big panel's solder green -- would sink into the cap, so the
// pointer is the accent mixed toward white. A rule, not a second colour: the
// generator keeps owning what the accent IS.
static constexpr float kPointerLift = 0.45f;

static NVGcolor ffRgb(unsigned c) {
    return nvgRGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

struct FfKnob : app::Knob {
    NVGcolor accA = ffRgb(kFfKnobCap);
    NVGcolor accB = ffRgb(kFfKnobCap);

    // Set by the two size flavours below, in mm. The box follows it, so the
    // knob occupies exactly the footprint the stock widget did.
    void setRadiusMm(float rMm) {
        const float d = mm2px(2.f * rMm);
        box.size = math::Vec(d, d);
    }

    void setAccent(const FfAccent& a) {
        accA = ffRgb(a.rgbA);
        accB = ffRgb(a.rgbB);
    }

    void draw(const DrawArgs& args) override {
        const float cx = box.size.x * 0.5f, cy = box.size.y * 0.5f;
        const float R = std::min(cx, cy);
        const float collarW = mm2px(kCollarW);
        const float collarR = R - collarW * 0.5f;
        const float capR = R - collarW - mm2px(kCollarGap);

        // Cap first: a flat body with a hairline, so it separates from the
        // dark hardware plate as well as from the light paper one.
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, capR);
        nvgFillColor(args.vg, ffRgb(kFfKnobCap));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, ffRgb(kFfKnobRim));
        nvgStrokeWidth(args.vg, mm2px(0.2f));
        nvgStroke(args.vg);

        // Collar. One solid ring when both halves agree; two arcs when they
        // do not, which on the big panel is MORPH alone -- the bridge knob
        // wears deck A's colour on its left and deck B's on its right, the
        // way the plate printed it before the collar moved onto the widget.
        nvgStrokeWidth(args.vg, collarW);
        if (accA.r == accB.r && accA.g == accB.g && accA.b == accB.b) {
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, cx, cy, collarR);
            nvgStrokeColor(args.vg, accA);
            nvgStroke(args.vg);
        } else {
            // Screen angles, y down: M_PI/2 is the bottom, so sweeping from
            // there to 3*M_PI/2 passes through M_PI -- the left half.
            nvgBeginPath(args.vg);
            nvgArc(args.vg, cx, cy, collarR, M_PI / 2, 3 * M_PI / 2, NVG_CW);
            nvgStrokeColor(args.vg, accA);
            nvgStroke(args.vg);
            nvgBeginPath(args.vg);
            nvgArc(args.vg, cx, cy, collarR, -M_PI / 2, M_PI / 2, NVG_CW);
            nvgStrokeColor(args.vg, accB);
            nvgStroke(args.vg);
        }

        // Pointer. minAngle/maxAngle are the framework's, measured from
        // straight up, so the tip is (sin, -cos) -- the same convention
        // SvgKnob's TransformWidget rotates by. No quantity means the module
        // browser: park it at centre rather than at whatever 0 happens to be.
        engine::ParamQuantity* pq = getParamQuantity();
        const float v = pq ? pq->getScaledValue() : 0.5f;
        const float a = math::rescale(v, 0.f, 1.f, minAngle, maxAngle);
        const float r0 = capR * 0.18f, r1 = capR - mm2px(kPointerTip);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, cx + std::sin(a) * r0, cy - std::cos(a) * r0);
        nvgLineTo(args.vg, cx + std::sin(a) * r1, cy - std::cos(a) * r1);
        nvgStrokeColor(args.vg, nvgLerpRGBA(accA, nvgRGB(0xFF, 0xFF, 0xFF),
                                            kPointerLift));
        nvgStrokeWidth(args.vg, mm2px(kPointerW));
        nvgLineCap(args.vg, NVG_ROUND);
        nvgStroke(args.vg);
        nvgLineCap(args.vg, NVG_BUTT);

        Knob::draw(args);
    }
};

// The two sizes, so createParamCentered<> has a default-constructible type.
// The angle limits are the stock widgets' own (RoundKnob 0.83*pi, Trimpot
// 0.75*pi) -- a knob that replaces another must not change how far it turns.
struct FfKnobBig : FfKnob {
    FfKnobBig() {
        minAngle = -0.83f * M_PI;
        maxAngle = 0.83f * M_PI;
        setRadiusMm(kFfKnobRBig);
    }
};

struct FfKnobSmall : FfKnob {
    FfKnobSmall() {
        minAngle = -0.75f * M_PI;
        maxAngle = 0.75f * M_PI;
        setRadiusMm(kFfKnobRSmall);
    }
};

} // namespace spkyvcv
