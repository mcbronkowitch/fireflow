#pragma once
#include <string>
#include <vector>
#include "instrument.h"

namespace spky {

namespace flow { class Flow; }

struct Event {
    double      time_s = 0.0;
    std::string action;
    int         part = 0;
    int         slot = 0;
    float       value = 0.f;
    bool        flag = false;
    int         ivalue = 0;
    std::string svalue;   // string-valued args (e.g. sync mode)
};

struct Scenario {
    int    sample_rate = 48000;
    float  bpm = 120.f;
    double duration_s = 10.0;
    std::string input_wav;            // fed into Instrument::process inputs
    std::vector<Event> init_events;   // applied at t = 0
    std::vector<Event> events;        // timeline, sorted by time_s

    // Task 9: set by load_scenario() when ANY "flow_*" action appears
    // anywhere (init or timeline) -- this is what tells main.cpp whether to
    // construct a Flow at all. flow_code is informational only (the first
    // flow_wake code found, init before timeline order): the actual wake
    // still happens through the ordinary event dispatch, this field is not
    // consulted for that.
    bool        has_flow = false;
    std::string flow_code;
};

// Parse a scenario JSON file. Returns false (and sets err) on read/parse error.
bool load_scenario(const std::string& path, Scenario& out, std::string& err);

// Apply one event to the instrument.
void apply_event(Instrument& inst, const Event& e);

// Task 9: same dispatch, plus the flow_* actions (flow_wake/flow_macro/
// flow_cv/flow_new/flow_new_partial/flow_undo/flow_lock). Non-"flow_"
// actions still fall through to the two-argument overload above, so this is
// a strict superset, not a parallel path. fl may be null (a scenario with no
// flow_* actions never constructs a Flow); flow_* actions against a null fl
// warn on stderr and no-op.
void apply_event(Instrument& inst, flow::Flow* fl, const Event& e);

} // namespace spky
