#pragma once

#include "factory_meta.h"
#include "instrument.h"


namespace audition {

void init_memory();
const spky::FxMem& fx_memory();

float* factory_left();
float* factory_right();

}  // namespace audition

extern "C" {
extern float g_factory_upload[2][audition::kFactoryFrames];
}
