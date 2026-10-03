#include "scan_value.h"

namespace shell {

Span panel_span(uint16_t zero, uint16_t rail)
{
    return Span{zero, rail, rail >= kRailFloor && zero <= kRailMargin};
}

float span_normalize(uint16_t raw, const Span& span)
{
    if(!span.valid || span.rail <= span.zero) return 0.0f;
    const float v = (static_cast<float>(raw) - static_cast<float>(span.zero))
                    / (static_cast<float>(span.rail)
                       - static_cast<float>(span.zero));
    if(v < 0.0f) return 0.0f;
    if(v > 1.0f) return 1.0f;
    return v;
}

bool pot_filter(PotFilter& f, uint16_t raw, const Span& span, int hysteresis,
                float* out)
{
    if(!span.valid || span.rail <= span.zero) return false;

    const int r = static_cast<int>(raw);
    float     v;
    bool      snapped = true;
    if(r <= static_cast<int>(span.zero) + hysteresis)
        v = 0.0f;
    else if(r >= static_cast<int>(span.rail) - hysteresis)
        v = 1.0f;
    else
    {
        v       = span_normalize(raw, span);
        snapped = false;
    }

    const int  d     = r - static_cast<int>(f.last_raw);
    const bool moved = d > hysteresis || -d > hysteresis;
    const bool stop  = snapped && v != f.last_v;
    if(f.emitted && !moved && !stop) return false;

    f.emitted  = true;
    f.last_raw = raw;
    f.last_v   = v;
    *out       = v;
    return true;
}

} // namespace shell
