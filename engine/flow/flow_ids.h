// engine/flow/flow_ids.h
#pragma once
namespace spky { namespace flow {

enum Macro { M_MOTION = 0, M_DENSITY, M_BRIGHT, M_DIRT, M_WANDER, M_SPACE,
             MACRO_COUNT };
enum Archetype { ARCH_DRONE = 0, ARCH_PULSE, ARCH_ARP, ARCH_FRAGMENT,
                 ARCH_COUNT };

// "No constraint" for draw_new's genre filter. Deliberately an int and not an
// Archetype enumerator: it is the ABSENCE of an archetype, and widening the
// enum with it would put a non-archetype into every array sized ARCH_COUNT
// and every switch over Archetype.
constexpr int ARCH_ANY = -1;

} } // namespace spky::flow
