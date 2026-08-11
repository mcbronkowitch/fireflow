// host/vcv/src/touch_pads.hpp
//
// FireFlow Glow on Simple Touch 2: the module logic that needs no Rack type --
// the pad gesture, the assignable fader/switch mappings and the pool.tsv
// export. Kept out of Glow.cpp so the desktop doctest suite can test it
// headlessly, the same split glow_ui.hpp and bbd_edge_state.hpp already use.
//
// No <rack.hpp>, no jansson, no widgets. Glow.cpp is the only file that knows
// what a Module is.
#pragma once
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include "flow/flow_ids.h"
#include "flow/terrain.h"
#include "flow/terrain_code.h"

namespace spkyvcv {

inline constexpr int kPadCount = 12;

// How many places Glow.cpp's drawTwelve() draws per archetype. It lives HERE,
// beside the count it has to multiply into, and not next to its own loop, for
// one reason: no ctest target compiles Glow.cpp -- the desktop build has no
// Rack headers and is not getting any -- so an assert stated there is checked
// only when somebody builds the plugin. This one guards a memory WRITE rather
// than an index convention, and it needs neither a Rack type nor a Module:
// drawTwelve() fills ARCH_COUNT * kPlacesPerArch entries of places[kPadCount],
// and those two counts have separate owners that have never been told about
// each other. ARCH_COUNT is the engine's (engine/flow/flow_ids.h), kPadCount is
// the board's MPR121. 4 * 3 == 12 is a coincidence between an archetype list
// and a touch chip, not a relationship. Add a fifth archetype and the loop
// writes places[12..14] straight through the Module object, over its
// std::string-adjacent members, with no bounds check anywhere on the path.
//
// If this fires: change how many places each archetype gets in drawTwelve() --
// do NOT enlarge places[], which is sized by the pads and by nothing else.
inline constexpr int kPlacesPerArch = 3;

static_assert(spky::flow::ARCH_COUNT * kPlacesPerArch == kPadCount,
              "drawTwelve() writes ARCH_COUNT * kPlacesPerArch places into "
              "places[kPadCount]; give each archetype a different share of the "
              "pads, do not widen the array");

// Hold threshold for "lean on a pad to reroll it" (spec §5.3). A STARTING
// VALUE tuned by ear, not a measurement: NEW's 1.5 s is far too sluggish for a
// pad, and a mouse button is not a capacitive plate anyway. Whoever retunes it
// should not mistake this number for a result.
inline constexpr double kPadHoldS = 0.4;

enum class PadAction { NONE, WAKE, REROLL };

struct PadEvent {
    PadAction action = PadAction::NONE;
    int pad = -1;
};

// The pad gesture (spec §5.3). Waking happens on PRESS, not on release: that
// keeps a tap latency-free, and it is what makes "tap the same pad again"
// return to the curated state without a special case -- the second tap is a
// plain wake, and a wake IS the curated state.
//
// State is one live pad and one flag, not twelve states, because an excursion
// is transient by design (parent spec §3: "tap the pad again -> back to the
// curated state. No undo mechanism is needed for this").
struct PadGesture {
    int  live = -1;          // which pad's place is playing, -1 = none yet
    bool excursion = false;  // has the live pad been rerolled since its wake

    void reset() {
        live = -1;
        excursion = false;
        _held = -1;
        _fired = false;
        for (int i = 0; i < kPadCount; ++i) _prev[i] = false;
    }

    // Adopt `down` as the baseline WITHOUT emitting an edge. Called after a
    // patch load: Rack saves momentary params, so a pad can come back pressed,
    // and treating that as a rising edge would wake -- then reroll 400 ms
    // later -- on top of the terrain that was just restored. Same rule
    // GestureBridge encodes for NEW.
    void prime(const bool* down) {
        for (int i = 0; i < kPadCount; ++i) _prev[i] = down[i];
    }

    PadEvent update(const bool* down, double now_s) {
        PadEvent ev;

        // A rising edge only counts while nothing else is held: two pads down
        // at once (MIDI-mapped, or a stuck param) must not interleave.
        if (_held < 0) {
            for (int i = 0; i < kPadCount; ++i) {
                if (down[i] && !_prev[i]) {
                    _held = i;
                    _heldSince = now_s;
                    _fired = false;
                    live = i;
                    excursion = false;
                    ev.action = PadAction::WAKE;
                    ev.pad = i;
                    break;
                }
            }
        } else if (!down[_held]) {
            _held = -1;
        } else if (!_fired && now_s - _heldSince >= kPadHoldS) {
            _fired = true;
            ev.action = PadAction::REROLL;
            ev.pad = _held;
        }

        for (int i = 0; i < kPadCount; ++i) _prev[i] = down[i];
        return ev;
    }

private:
    bool   _prev[kPadCount] = {};
    int    _held = -1;
    double _heldSince = 0.0;
    bool   _fired = false;
};

inline constexpr std::size_t kNameCap = 32;
inline constexpr std::size_t kNoteCap = 120;

// One curated place as the module holds it. The code is the identity; name and
// note are the human half and travel into the export (spec §6.3, §6.4).
//
// Fixed buffers rather than std::string, and the static_assert below is the
// reason: Glow.cpp hands the whole twelve from the UI thread to the AUDIO
// thread as one staged copy (UiOp::SET_PLACES), because the audio thread is
// the only writer of the live array. A std::string member would put a malloc
// in that copy -- on the audio thread, for a name somebody typed. The caps are
// the buffer sizes, so a truncation rule cannot disagree with an array bound.
struct Place {
    char code[spky::flow::kTerrainCodeLen + 1] = {};
    char name[kNameCap + 1] = {};
    char note[kNoteCap + 1] = {};
};

static_assert(std::is_trivially_copyable<Place>::value,
              "Place is copied on the audio thread (Glow.cpp, UiOp::SET_PLACES); "
              "it must not own heap memory");

enum class FaderTarget  { OFF, TEMPO, MASTER };
enum class SwitchTarget { OFF, LOCK, SCALE };

// Fader 0..1 -> BPM across P_TEMPO_BPM's declared range (flow_params.h).
// The terrain owns the tempo and Flow re-pushes it on every terrain change, so
// the call site has to re-apply this EVERY control tick -- see Glow.cpp.
inline float fader_tempo_bpm(float knob01) { return 50.f + knob01 * 90.f; }

// Linear output gain. Default is unity at the top; a module that boots at half
// gain is a bug report.
inline float fader_master_gain(float knob01) { return knob01; }

// A three-position switch driving a two-valued target uses the end positions;
// the centre reads as the lower one. Anything out of range reads as off, the
// same rule glow_ui.hpp's clamp_* helpers apply -- a corrupt patch must not
// lock the generator.
inline bool lock_switch(int pos) { return pos == 2; }

struct TonalityGate {
    int scale_ovr = -1;   // -1 = AUTO
    int root_ovr  = -1;   // -1 = AUTO
};

// The SCALE switch GATES the menu's values (glow_ui.hpp's clamp_menu_scale
// keeps those in range); it never selects one. Position 0 is AUTO, and so is
// anything out of range.
inline TonalityGate scale_switch(int pos, int menu_scale, int menu_root) {
    TonalityGate g;
    if (pos == 1) {
        g.scale_ovr = menu_scale;
    } else if (pos == 2) {
        g.scale_ovr = menu_scale;
        g.root_ovr = menu_root;
    }
    return g;
}

// Strip, do not escape: a tab or a newline in a name would break the TSV of
// export_pool_tsv, and an escaped one would come back wrong through a
// hand-edited pool.tsv. Truncates to `cap` characters.
inline std::string sanitize_label(const std::string& in, std::size_t cap) {
    std::string out;
    out.reserve(in.size() < cap ? in.size() : cap);
    for (char c : in) {
        if (c == '\t' || c == '\n' || c == '\r') continue;
        if (out.size() >= cap) break;
        out.push_back(c);
    }
    return out;
}

// Sanitize `in` into a Place's fixed buffer. `cap` is the character cap, and
// `dst` must hold cap + 1 bytes -- the two Place fields are sized by exactly
// the two caps above, so pass the matching one. Always terminates.
inline void set_label(char* dst, std::size_t cap, const std::string& in) {
    const std::string s = sanitize_label(in, cap);
    std::memcpy(dst, s.c_str(), s.size() + 1);
}

inline const char* arch_name(int arch) {
    switch (arch) {
        case spky::flow::ARCH_DRONE:    return "DRONE";
        case spky::flow::ARCH_PULSE:    return "PULSE";
        case spky::flow::ARCH_ARP:      return "ARP";
        case spky::flow::ARCH_FRAGMENT: return "FRAGMENT";
        default:                        return "";
    }
}

// The pool.tsv rows for the twelve pads (parent spec §4.3), column order
// code / arch / date / fp / pad / name / note.
//
// date, fp and note-if-unwritten stay EMPTY on purpose. The fingerprint WILL be
// computed by a gate under tests/ -- that gate is an unbuilt deliverable of the
// parent spec (engine/flow/places/ does not exist yet), so today nothing
// computes it at all, and this column is empty because there is no producer,
// not because a checked one lives elsewhere. Adding one here anyway would put a
// second producer in a second language, which is the silent divergence that
// gate is meant to catch. They are interior columns, so their tabs are still
// emitted.
//
// Line ending is \n, not \r\n: the destination is a repo file.
inline std::string export_pool_tsv(const Place* places, int n) {
    std::string out = "code\tarch\tdate\tfp\tpad\tname\tnote\n";
    for (int i = 0; i < n; ++i) {
        const Place& p = places[i];
        spky::flow::TerrainState st;
        const bool ok = spky::flow::decode_code(p.code, st);
        char pad[8];
        std::snprintf(pad, sizeof pad, "%d", i + 1);

        out += p.code;
        out += '\t';
        out += ok ? arch_name(spky::flow::arch_of(st.master)) : "";
        out += "\t\t\t";              // date, fp
        out += pad;
        out += '\t';
        out += p.name;
        out += '\t';
        out += p.note;
        out += '\n';
    }
    return out;
}

}  // namespace spkyvcv
