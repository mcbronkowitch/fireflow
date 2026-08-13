#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "generated_flow_panel.hpp"

#if !defined(SPKY_TESTING)
#include "plugin.hpp"
#endif

namespace spkyvcv::glow_panel {

enum class Layer : std::uint8_t { Rear, Faceplate, Touch };

struct LayerSpec {
    Layer layer;
    const char* resourcePath;
    bool required;
};

constexpr std::array<LayerSpec, 3> kLayers = {{
    {Layer::Rear, "res/GlowRear.png", true},
    {Layer::Faceplate, "res/GlowFaceplate.png", true},
    {Layer::Touch, "res/GlowTouch.png", true},
}};

constexpr std::array<const char*, 3> kSwitchFrames = {{
    "res/GlowSwitchDown.png",
    "res/GlowSwitchCenter.png",
    "res/GlowSwitchUp.png",
}};

constexpr int kPanelWidthHp = 16;

struct PadBinding {
    const glow::PadShape* shape;
    int paramId;
    std::string accessibleName;
};

struct ToggleBinding {
    int paramId;
    float minValue;
    float maxValue;
    float defaultValue;
    int positionCount;
};

int switchFrameIndex(float value);
bool allRequiredLayersAvailable(const bool* availability, std::size_t count);
const std::array<PadBinding, 12>& padBindings();
const std::array<ToggleBinding, 2>& toggleBindings();
const PadBinding* padBindingForParam(int paramId);
const ToggleBinding* toggleBindingForParam(int paramId);

#if !defined(SPKY_TESTING)
struct GlowHardwarePanel : widget::Widget {
    GlowHardwarePanel();
    void draw(const DrawArgs& args) override;

private:
    std::array<bool, kLayers.size()> missingLogged_{};
};

struct GlowToggle : app::Switch {
    GlowToggle();
    void draw(const DrawArgs& args) override;

private:
    std::array<bool, kSwitchFrames.size()> missingLogged_{};
};
#endif

} // namespace spkyvcv::glow_panel
