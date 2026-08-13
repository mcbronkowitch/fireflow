#include "glow_panel.hpp"

#if !defined(SPKY_TESTING)
#include "generated_flow_panel.hpp"

#include <exception>
#endif

namespace spkyvcv::glow_panel {

int switchFrameIndex(float value) {
    // Glow's existing Rack parameters are snapped integer switches with the
    // unchanged range 0 (down), 1 (centre), 2 (up). Round around those values
    // and clamp anything beyond the configured range to the nearest frame.
    if (value < 0.5f)
        return 0;
    if (value < 1.5f)
        return 1;
    return 2;
}

bool allRequiredLayersAvailable(const bool* availability, std::size_t count) {
    if (!availability || count < kLayers.size())
        return false;
    for (std::size_t i = 0; i < kLayers.size(); ++i) {
        if (kLayers[i].required && !availability[i])
            return false;
    }
    return true;
}

#if !defined(SPKY_TESTING)
namespace {

constexpr float kUpperRearSeparatorMm = 13.5f;
constexpr float kLowerTouchSeparatorMm = 77.1f;

void fillImage(NVGcontext* vg, const Vec& size, int handle) {
    const NVGpaint paint = nvgImagePattern(
        vg, 0.f, 0.f, size.x, size.y, 0.f, handle, 1.f);
    nvgBeginPath(vg);
    nvgRect(vg, 0.f, 0.f, size.x, size.y);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
}

void beginPadPath(NVGcontext* vg, const glow::PadShape& shape) {
    const int pointCount = static_cast<int>(shape.pointCount);
    if (pointCount < 3)
        return;
    auto point = [&](int i) {
        const auto& p = shape.points[(i + pointCount) % pointCount];
        return mm2px(Vec(p.x, p.y));
    };

    Vec p1 = point(0);
    nvgBeginPath(vg);
    nvgMoveTo(vg, p1.x, p1.y);
    for (int i = 0; i < pointCount; ++i) {
        const Vec p0 = point(i - 1);
        const Vec p2 = point(i + 1);
        const Vec p3 = point(i + 2);
        const Vec c1(p1.x + (p2.x - p0.x) / 6.f,
                     p1.y + (p2.y - p0.y) / 6.f);
        const Vec c2(p2.x - (p3.x - p1.x) / 6.f,
                     p2.y - (p3.y - p1.y) / 6.f);
        nvgBezierTo(vg, c1.x, c1.y, c2.x, c2.y, p2.x, p2.y);
        p1 = p2;
    }
    nvgClosePath(vg);
}

void drawFallbackPanel(const widget::Widget::DrawArgs& args) {
    nvgSave(args.vg);

    nvgBeginPath(args.vg);
    nvgRect(args.vg, 0.f, 0.f,
            mm2px(glow::kPanelW), mm2px(glow::kPanelH));
    nvgFillColor(args.vg, nvgRGB(15, 16, 14));
    nvgFill(args.vg);

    // The fallback boundary consumes the generated physical panel dimensions,
    // so it cannot change the 16 HP module footprint when a raster disappears.
    nvgBeginPath(args.vg);
    nvgRoundedRect(args.vg, mm2px(0.65f), mm2px(0.65f),
                   mm2px(glow::kPanelW - 1.3f),
                   mm2px(glow::kPanelH - 1.3f), mm2px(1.2f));
    nvgStrokeColor(args.vg, nvgRGB(95, 89, 77));
    nvgStrokeWidth(args.vg, mm2px(0.28f));
    nvgStroke(args.vg);

    nvgBeginPath(args.vg);
    nvgMoveTo(args.vg, 0.f, mm2px(kUpperRearSeparatorMm));
    nvgLineTo(args.vg, mm2px(glow::kPanelW),
              mm2px(kUpperRearSeparatorMm));
    nvgMoveTo(args.vg, 0.f, mm2px(kLowerTouchSeparatorMm));
    nvgLineTo(args.vg, mm2px(glow::kPanelW),
              mm2px(kLowerTouchSeparatorMm));
    nvgStrokeColor(args.vg, nvgRGB(74, 70, 61));
    nvgStrokeWidth(args.vg, mm2px(0.3f));
    nvgStroke(args.vg);

    for (const auto& shape : glow::kPadShapes) {
        beginPadPath(args.vg, shape);
        nvgStrokeColor(args.vg, nvgRGB(185, 101, 50));
        nvgStrokeWidth(args.vg, mm2px(glow::kPadStrokeWidth));
        nvgStroke(args.vg);
    }

    nvgRestore(args.vg);
}

void drawFallbackToggle(const widget::Widget::DrawArgs& args,
                        const Vec& size, int frame) {
    const float centreX = size.x * 0.5f;
    const float centreY = size.y * 0.5f;
    const float leverEndY = frame == 0 ? size.y * 0.78f
                          : frame == 2 ? size.y * 0.22f
                                       : centreY;

    nvgBeginPath(args.vg);
    nvgRoundedRect(args.vg, 0.f, 0.f, size.x, size.y, mm2px(0.7f));
    nvgFillColor(args.vg, nvgRGB(30, 31, 28));
    nvgFill(args.vg);
    nvgStrokeColor(args.vg, nvgRGB(111, 108, 99));
    nvgStrokeWidth(args.vg, mm2px(0.25f));
    nvgStroke(args.vg);

    nvgBeginPath(args.vg);
    nvgMoveTo(args.vg, centreX, centreY);
    nvgLineTo(args.vg, centreX, leverEndY);
    nvgStrokeColor(args.vg, nvgRGB(204, 202, 194));
    nvgStrokeWidth(args.vg, mm2px(0.85f));
    nvgLineCap(args.vg, NVG_ROUND);
    nvgStroke(args.vg);
}

} // namespace

GlowHardwarePanel::GlowHardwarePanel() {
    box.size = Vec(kPanelWidthHp * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
}

void GlowHardwarePanel::draw(const DrawArgs& args) {
    // Image is intentionally local. Rack 2.6.6 warns that a Window change can
    // invalidate it between frames, while loadImage() itself is cached.
    bool available[kLayers.size()] = {};
    for (std::size_t i = 0; i < kLayers.size(); ++i) {
        try {
            const std::shared_ptr<window::Image> image =
                APP && APP->window
                    ? APP->window->loadImage(
                          asset::plugin(pluginInstance,
                                        kLayers[i].resourcePath))
                    : nullptr;
            available[i] = image && image->handle >= 0;
            if (available[i])
                fillImage(args.vg, box.size, image->handle);
        }
        catch (const std::exception&) {
            available[i] = false;
        }

        if (!available[i] && !missingLogged_[i]) {
            WARN("Glow: missing required panel layer %s",
                 kLayers[i].resourcePath);
            missingLogged_[i] = true;
        }
    }

    if (!allRequiredLayersAvailable(available, kLayers.size()))
        drawFallbackPanel(args);

    widget::Widget::draw(args);
}

GlowToggle::GlowToggle() {
    momentary = false;
    box.size = mm2px(Vec(glow::kSwitchW, glow::kSwitchH));
}

void GlowToggle::draw(const DrawArgs& args) {
    float value = 0.f;
    if (engine::ParamQuantity* quantity = getParamQuantity())
        value = quantity->getValue();
    const int frame = switchFrameIndex(value);

    bool available = false;
    try {
        const std::shared_ptr<window::Image> image =
            APP && APP->window
                ? APP->window->loadImage(
                      asset::plugin(pluginInstance, kSwitchFrames[frame]))
                : nullptr;
        available = image && image->handle >= 0;
        if (available)
            fillImage(args.vg, box.size, image->handle);
    }
    catch (const std::exception&) {
        available = false;
    }

    if (!available) {
        if (!missingLogged_[frame]) {
            WARN("Glow: missing required switch frame %s",
                 kSwitchFrames[frame]);
            missingLogged_[frame] = true;
        }
        drawFallbackToggle(args, box.size, frame);
    }

    // The custom frame is the moving overlay. app::Switch owns interaction
    // and tooltip recursion only; it contributes no stock SVG child that
    // could obscure the physical switch base in GlowTouch.png.
    app::Switch::draw(args);
}
#endif

} // namespace spkyvcv::glow_panel
