#pragma once
#include <rack.hpp>

using namespace rack;

// The plugin instance and the two module models it exposes: the full-control
// panel and the 60 HP hardware-panel draft.
extern Plugin* pluginInstance;
extern Model* modelFireflow;
extern Model* modelFireflowHW;
