#pragma once

#include "parts/engine_iface.h"

namespace spky {
class Instrument;
}

namespace audition {

void apply_init_patch(spky::Instrument& instrument, const float* values);
void apply_init_patch(spky::Instrument& instrument);

}  // namespace audition
