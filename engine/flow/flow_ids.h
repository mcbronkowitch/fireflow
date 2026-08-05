// engine/flow/flow_ids.h
#pragma once
namespace spky { namespace flow {

enum Macro { M_MOTION = 0, M_DENSITY, M_BRIGHT, M_DIRT, M_WANDER, M_SPACE,
             MACRO_COUNT };
enum Archetype { ARCH_DRONE = 0, ARCH_PULSE, ARCH_ARP, ARCH_FRAGMENT,
                 ARCH_COUNT };

} } // namespace spky::flow
