#pragma once

#include "parts/engine_iface.h"

namespace spky {
class Instrument;
}

namespace audition {

void apply_engine_stages(spky::Instrument& instrument, int deck,
                         spky::EngineId engine, float value);
void apply_init_patch(spky::Instrument& instrument);

}  // namespace audition
