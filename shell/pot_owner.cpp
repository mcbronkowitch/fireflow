// Pot ownership: see pot_owner.h.
#include "pot_owner.h"

namespace shell {

void pot_release(PotOwner& o)
{
    o.owned   = false;
    o.has_ref = false;
}

Own pot_own(PotOwner& o, uint16_t raw, int target, bool span_valid, int touch)
{
    if (!span_valid) return Own::Idle;

    if (target != o.target) {
        pot_release(o);
        o.target = target;
    }

    if (o.owned) return Own::Owned;

    if (!o.has_ref) {
        o.ref     = raw;
        o.has_ref = true;
        return Own::Idle;
    }

    // int, so the difference of two uint16_t cannot wrap.
    const int d = static_cast<int>(raw) - static_cast<int>(o.ref);
    if ((d < 0 ? -d : d) > touch) {
        o.owned = true;
        return Own::Took;
    }
    return Own::Idle;
}

} // namespace shell
