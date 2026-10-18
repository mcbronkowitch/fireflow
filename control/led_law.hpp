#pragma once
// The panel's LED display law. Rack-free on purpose so spky_tests can drive
// it -- the same arrangement as bbd_edge_state.hpp -- and shared: the VCV
// module (Fireflow.cpp keeps only the wiring) and the Daisy firmware both
// include this one header. Spec: docs/superpowers/specs/2026-08-16-led-feedback-design.md
// and, for the move to control/, 2026-10-18-rev-a-p6b2-mod-keys-leds-design.md.
#include <algorithm>
#include <cmath>
#include "instrument.h"
#include "control/params.hpp"

namespace spkyled {

// The brightness raster: how many duty levels a lamp has (0 .. kLedSteps-1).
// On the board it is the block-rate latch -- the lamps change once per block,
// in the latch that carries the mux address (P2 §4) -- not the mux width.
// Rack quantises to the same raster, because a module that breathes more
// finely than the panel can is validating itself against the wrong
// instrument.
//
// MEASURED on the coupon, 2026-10-18 (docs/hardware/p6b2-led-and-touch-measured.md):
// LED_1 at duty 1 of N showed no visible flicker at N = 4, 6 and 8 and
// flickered at N = 16; 8 is the largest tested N (4, 6, 8, 16) without visible flicker.
// tests/test_led_law.cpp pins the value; change both together.
constexpr int kLedSteps = 8;

// UNUSED SINCE P6b-2: kFloor, kEnvFall, kEnvOff, Lamp and intensity() below
// belong to the excursion-envelope lamps of the 2026-08-16 LED spec. The
// 2026-10-07 lamp meanings replaced every one of those lamps, and fill() calls
// none of this; only tests/test_led_law.cpp's G-tests do. Kept, with those
// tests, for a future lane lamp; the by-ear status below is as it was.
//
// Trough of a breath, as a fraction of the envelope. A FIXED floor would make
// an idle lane and a gently moving one look alike, and would push every
// breath's bottom through the same dark band -- which at this instrument's
// slow rates reads as a loose contact. By-ear candidate, not an invariant.
constexpr float kFloor = 0.28f;

// Perceived lightness goes roughly as the cube root of duty, so perceptual
// linearity needs duty = intensity^gamma with gamma above 1. That puts the
// midpoint duty BELOW the linear midpoint; a linear ramp looks static across
// its top half.
constexpr float kGamma = 2.2f;

// Envelope release, in seconds. Long enough to hold through a breath at
// audible rates, short enough to let go when modulation stops. NOTE the
// limit this implies: for lane cycles much longer than this the envelope
// follows the excursion directly rather than holding its peak, so a very
// slow lane tracks its own position instead of showing a steady depth. It is
// still never dark while it moves, which is what the design needs.
constexpr float kEnvFall = 2.0f;

// Envelope floor: below this a lamp is off, not merely dim. It has to exist and
// it has to be well above a denormal guard, because duty()'s lift holds any
// positive intensity at one step -- so this constant, not the release, is what
// makes "dark" reachable. At 1e-6 a lamp would sit at its lowest step for
// 27 s after modulation stopped. 0.02 is 3.9 release constants, about 8 s, and
// stays an order of magnitude below the shallowest breath the lift protects.
// By-ear candidate, not an invariant.
constexpr float kEnvOff = 0.02f;

// One light's state: a peak-tracked envelope of |excursion|, i.e. the
// modulation DEPTH.
struct Lamp {
    float env = 0.f;

    void follow(float excursion, float dt) {
        const float a = std::fabs(excursion);
        if (a >= env) { env = a; return; }           // instant attack
        const float k = dt / (kEnvFall + dt);        // one-pole release
        env += (a - env) * k;
        if (env < kEnvOff) env = 0.f;
    }
};

// The envelope sets the ceiling, the instantaneous excursion breathes inside
// it, and the trough scales with depth. Three readings fall out of one
// expression: dark (nothing modulating), dim breath (shallow), bright breath
// (deep).
inline float intensity(float env, float excursion) {
    if (env <= 0.f) return 0.f;
    const float a   = std::fabs(excursion);
    const float rel = a >= env ? 1.f : a / env;
    return env * (kFloor + (1.f - kFloor) * rel);
}

// The gamma quantiser without a pow per lamp. round(v^gamma * (steps-1)) >= k
// exactly when v >= t_k = ((k - 0.5) / (steps - 1))^(1/gamma), so the whole
// law is a comparison against steps-1 thresholds, computed once per step
// count. The largest count the table holds; the 256 is the fine ladder
// tests/test_led_law.cpp uses to test the curve rather than the raster.
constexpr int kMaxSteps = 256;

struct GammaTable {
    float t[kMaxSteps] = {};   // t[k], k = 1 .. steps-1; t[0] unused
    int   steps = 0;
    void build(int n) {
        for (int k = 1; k < n; ++k)
            t[k] = std::pow((static_cast<float>(k) - 0.5f) / static_cast<float>(n - 1),
                            1.f / kGamma);
        steps = n;
    }
};

// Two tables, so neither needs a lock. The shipped step count has its own,
// built once on first use through a function-local static (thread-safe, and
// two Rack modules may well run on two audio threads). Any other count --
// only the unit tests ask -- shares one rebuilt-on-change cache, which is
// single-threaded by that same fact.
inline const GammaTable& gamma_table(int steps) {
    static const GammaTable shipped = [] { GammaTable g; g.build(kLedSteps); return g; }();
    if (steps == kLedSteps) return shipped;
    static GammaTable other;
    if (other.steps != steps) other.build(steps);
    return other;
}

// Quantised duty, 0 .. steps-1 (steps in 2 .. kMaxSteps; kLedSteps in the
// shipped hosts, a parameter so the tests can sweep it).
// Every non-zero intensity must reach at least one step: naive quantisation
// of a gamma curve sends the bottom third of every breath to zero, which
// would read as "nothing is modulating" and destroy the whole distinction.
inline int duty(float intens, int steps) {
    if (intens <= 0.f) return 0;
    const float v = std::min(1.f, intens);
    steps = std::min(steps, kMaxSteps);          // never index past the table
    const GammaTable& g = gamma_table(steps);
    int q = 1;                                   // the lift: never below one step
    for (int k = 2; k < steps && v >= g.t[k]; ++k) q = k;
    return q;
}

// For values that were tuned as LIGHT output rather than as perceived
// brightness. The REC lamp's three states were set by eye against Rack's
// setBrightness long before this law existed, so they are already light
// levels; duty() expects a perceptual value and applies the gamma, and
// handing it one of these directly comes out visibly darker -- 0.15 and 0.25
// collapse onto the same step and the fill meter goes flat over its lower
// third. A light level is already linear in duty, so it is just rounded onto
// the raster (lifted to one step): algebraically the pow round trip this
// replaces, duty(light^(1/gamma)), without the two pow calls.
inline int duty_from_light(float light, int steps) {
    if (light <= 0.f) return 0;
    const int q = static_cast<int>(std::min(1.f, light) * static_cast<float>(steps - 1) + 0.5f);
    return q < 1 ? 1 : q;
}

// Fraction of the transport beat the TEMPO lamp is on. Short on purpose: a
// 50% square would read as "the grid is running", a tick reads as the beat.
// Scales with TEMPO and PACE (beat_phase already carries both) instead of a
// wall-clock flash that would eat the whole beat at high BPM. By-ear
// candidate, not an invariant.
constexpr float kTempoPulse = 0.12f;

constexpr float kSongFlash = 0.15f;

inline bool tempo_on(float beat_phase) {
    return beat_phase < kTempoPulse;
}

// The MOD latch's lamp: two short flashes, then a long dark tail, twice a
// second. The latch re-points every wreathed knob on the plate at once, so
// forgetting it is engaged is the expensive mistake this lamp exists to
// prevent -- and the steady lamp it replaces sits in peripheral vision as
// furniture. An EVEN blink of the same rate would be louder but reads as a
// loose contact, the way a flickering panel light always does; a pair reads
// as deliberate. Owner's call, 2026-08-29. All three numbers are by-eye
// candidates, not invariants.
//
// kModPulsePeriod must divide 1 s exactly. The phase comes from Panel::blink,
// which wraps at 1 s, so a period that does not fit a whole number of times
// into the wrap would jump mid-pulse once a second.
constexpr float kModPulsePeriod = 0.5f;   // s, one flash-flash-gap cycle
constexpr float kModPulseOn     = 0.08f;  // s, each of the two flashes
constexpr float kModPulseGap    = 0.08f;  // s, the dark between the pair

inline bool mod_pulse_on(float blink) {
    const float t = std::fmod(blink, kModPulsePeriod);
    return t < kModPulseOn
        || (t >= kModPulseOn + kModPulseGap
            && t <  2.f * kModPulseOn + kModPulseGap);
}

// The LVL lamp's floor: a deck whose output into the mix (Instrument::
// deck_out_level) is above this delivers signal. By-eye candidate derived from
// a probe, not an invariant; the printout is in the commit message of
// "feat(engine): const observers for the lamp law" (git log -S kLvlFloor).
// The probe found a clean gap: every silent state (LVL 0, FILT closed, empty
// sampler) reads exactly 0, a STEP deck's rests spend 24 % of 40 s below 1e-5,
// while the quietest continuously playing deck read 0.0022 (FEED at LVL 0.05)
// and 0.0040 (SYNTH FLOW). 1e-3 sits below both continuous minima.
constexpr float kLvlFloor = 1e-3f;

// The three single-shot flashes of the 2026-10-07 lamp meanings. All three
// lengths are by-eye candidates (spec 2026-10-18 rev-a-p6b2 §4.2), not
// invariants, and none has been seen on a lamp yet.
//
// kFtimeFlash: one FLUX time period elapsed. Short, so a period of a few
//   hundred ms still reads as a tick and not as a steady light.
// kSwitchPulse: an engine or a scale took effect, on the SHIFT lamp. ONE long
//   flash on purpose: it has to read differently from the input level lit
//   under it and from MOD's double pulse on the lamp next to it.
// kJackFlash: a pulse at the CLOCK / RESET jack. As short as the FTIME tick:
//   a 40 ms flash per pulse merges into a continuous light only above
//   1 / 0.04 s = 25 pulses per second (~25 Hz); a slower clock blinks.
// kInputFloor: below this input peak the SHIFT lamp's level display is dark
//   (about -60 dBFS; IN is scaled so 1.0 is +-5 V). Needed because duty()
//   lifts every positive value to one step and the peak follower only ever
//   approaches zero.
constexpr float kInputFloor  = 1e-3f;
constexpr float kFtimeFlash  = 0.05f;   // s
constexpr float kSwitchPulse = 0.25f;   // s
constexpr float kJackFlash   = 0.04f;   // s

// What the host knows and the law cannot read off the Instrument.
struct LedInputs {
    bool mod_latched   = false;
    bool shift_latched = false;   // always false this round (SHFT reserved)
    bool clock_pulse   = false;   // a CLOCK edge since the last fill()
    bool reset_pulse   = false;   // a RESET edge since the last fill()
};

struct Panel {
    float blink = 0.f;                 // free-running 1 Hz, for REC
    bool    song_armed[2]  = {false, false};
    uint8_t song_pat[2]    = {0, 0};
    float   song_remain[2] = {0.f, 0.f};

    // FTIME: the last FLUX phase read while engaged. `ftime_primed` is false
    // whenever FLUX was idle on the previous call, so the first engaged read
    // only records a phase -- the phase is frozen while FLUX is idle and a
    // stale value must not read as a wrap.
    float ftime_prev[2]   = {0.f, 0.f};
    bool  ftime_primed[2] = {false, false};
    float ftime_remain[2] = {0.f, 0.f};

    // SHIFT's switch pulse: the epochs seen on the previous call. Both epochs
    // tick once at boot (the init patch's first set_engine / set_scale), so the
    // first fill() ARMS -- stores them, no pulse -- like song_armed above.
    bool     epochs_armed = false;
    uint32_t engine_seen[2] = {0, 0};
    uint32_t scale_seen     = 0;
    float    switch_remain  = 0.f;

    float clk_remain = 0.f;
    float rst_remain = 0.f;
};

// A single-shot flash: lit while `remain` > 0, then it counts down by dt.
// Returns whether the lamp is lit on this call (the SONG lamp's pattern, so a
// flash armed on a call is seen by that call).
inline bool flash_step(float& remain, float dt) {
    if (remain <= 0.f) return false;
    remain -= dt;
    if (remain < 0.f) remain = 0.f;
    return true;
}

// The whole panel in one call, so a gate can assert that every light is
// written -- six lamps sat on this plate for months with no LightId and
// nothing noticed. Writes exactly NUM_LIGHTS entries of duty_out.
inline void fill(const spky::Instrument& inst, Panel& p, float dt,
                 int steps, const LedInputs& in, int* duty_out) {
    using namespace ffctl;

    // Written, not skipped. FLOW has no lamp on the plate at all, but the id
    // exists and a gate asserts that every light is written.
    for (int id : {FLOW_A_L, FLOW_B_L})
        duty_out[id] = 0;

    // Metronome: a short pulse on the transport downbeat. Hard on/off, no
    // envelope -- this reports where the beat is, which is the question the
    // lamp at TEMPO is there to answer.
    duty_out[TEMPO_L] = tempo_on(inst.beat_phase()) ? steps - 1 : 0;

    p.blink += dt;
    if (p.blink >= 1.f) p.blink -= 1.f;

    // The MOD layer's lamp: hard on/off, because it reports a mode and not a
    // level (spec 2026-08-22 mod-latch-layer §5) -- but a double pulse rather
    // than the steady light that spec described, see kModPulsePeriod above.
    // Written here, AFTER the blink advance, so it reads the same phase REC
    // reads further down; from above the advance it would sit one tick behind
    // its twin and the two lamps would beat against each other.
    //
    // One lamp, two jobs: the latch while engaged, otherwise the master
    // limiter that the separate ceiling lamp used to show (spec 2026-10-07 §5).
    duty_out[MODBTN_L] = in.mod_latched ? (mod_pulse_on(p.blink) ? steps - 1 : 0)
                                        : duty(inst.limiter_squash(), steps);

    // LVL: this deck delivers signal at all (spec 2026-10-07 §5). Hard on/off
    // against the floor, straight off the deck's level with no hold -- on a
    // STEP deck it blinks with the notes, as the level does (Bastian,
    // 2026-10-18). The old LANE_LEVEL envelope is gone; deck_out_level is
    // already a peak follower with a 20 ms release, which is all the
    // smoothing wanted here.
    duty_out[LVL_A_L] = inst.deck_out_level(0) > kLvlFloor ? steps - 1 : 0;
    duty_out[LVL_B_L] = inst.deck_out_level(1) > kLvlFloor ? steps - 1 : 0;

    // FTIME: one flash per FLUX time period, only while FLUX is engaged
    // (mix > 0 -- the gate that switches the block on). The phase runs free
    // over the delay time, so a period is a phase that DECREASED since the
    // last engaged read. It can read >= 1 for a moment right after the delay
    // time shrinks, so wrap it on read. While FLUX is idle the phase is
    // frozen: drop the priming so the first engaged read cannot see a stale
    // value as a wrap.
    const int ftimeId[2] = {FTIME_A_L, FTIME_B_L};
    for (int part = 0; part < 2; ++part) {
        if (!inst.flux_engaged(part)) {
            p.ftime_primed[part] = false;
            p.ftime_remain[part] = 0.f;
            duty_out[ftimeId[part]] = 0;
            continue;
        }
        float ph = inst.flux_repeat_phase(part);
        ph -= std::floor(ph);
        if (p.ftime_primed[part] && ph < p.ftime_prev[part])
            p.ftime_remain[part] = kFtimeFlash;
        p.ftime_prev[part]   = ph;
        p.ftime_primed[part] = true;
        duty_out[ftimeId[part]] = flash_step(p.ftime_remain[part], dt) ? steps - 1 : 0;
    }

    // SHIFT: steady while latched (never this round); otherwise the input
    // level, and over that one long flash when an engine (either deck) or the
    // scale has taken effect. The epochs move when the new engine SOUNDS and
    // when the scale changes, not while a knob turns. Both tick once at boot,
    // so the first call arms and never pulses.
    {
        const uint32_t e0 = inst.engine_epoch(0);
        const uint32_t e1 = inst.engine_epoch(1);
        const uint32_t sc = inst.scale_epoch();
        if (!p.epochs_armed) {
            p.epochs_armed = true;
        } else if (e0 != p.engine_seen[0] || e1 != p.engine_seen[1]
                   || sc != p.scale_seen) {
            p.switch_remain = kSwitchPulse;
        }
        p.engine_seen[0] = e0;
        p.engine_seen[1] = e1;
        p.scale_seen     = sc;
        const bool pulse = flash_step(p.switch_remain, dt);
        // The follower decays towards zero without ever reaching it, and
        // duty() lifts any positive value to one step -- so without a floor
        // the lamp would glow at step 1 for seconds after the input stops.
        const float level = inst.input_peak();
        duty_out[SHIFTBTN_L] = (in.shift_latched || pulse) ? steps - 1
                             : level > kInputFloor        ? duty(level, steps)
                                                          : 0;
    }

    // CLK / RST: a flash per jack pulse. Hard on/off, like the gate lamps.
    if (in.clock_pulse) p.clk_remain = kJackFlash;
    if (in.reset_pulse) p.rst_remain = kJackFlash;
    duty_out[CLK_L] = flash_step(p.clk_remain, dt) ? steps - 1 : 0;
    duty_out[RST_L] = flash_step(p.rst_remain, dt) ? steps - 1 : 0;

    const int songId[2] = {SONG_A_L, SONG_B_L};
    for (int part = 0; part < 2; ++part) {
        const uint8_t now = inst.active_pattern(part);
        if (!p.song_armed[part]) {
            p.song_pat[part] = now;
            p.song_armed[part] = true;
        } else if (now != p.song_pat[part]) {
            p.song_pat[part] = now;
            p.song_remain[part] = kSongFlash;
        }
        if (p.song_remain[part] > 0.f) {
            duty_out[songId[part]] = steps - 1;
            p.song_remain[part] -= dt;
            if (p.song_remain[part] < 0.f) p.song_remain[part] = 0.f;
        } else {
            duty_out[songId[part]] = 0;
        }
    }

    // Straight through, no envelope: this reports that a note is sounding, not
    // that one sounded recently. The code it replaces smoothed the gate with a
    // 0.42 ms one-pole, which at the LED update rate is indistinguishable from
    // following it directly.
    const int gateId[2] = {GATE_A_L, GATE_B_L};
    for (int part = 0; part < 2; ++part)
        duty_out[gateId[part]] = inst.gate(part) ? steps - 1 : 0;

    // REC keeps the three-state behaviour it already had: pulsing while
    // recording, steady at the fill level when the part holds content, dark
    // otherwise and on any non-Sampler engine. The three constants below
    // (1.f, 0.25f, the 0.15f..0.70f fill range) were tuned by eye as LIGHT
    // output against Rack's setBrightness, before this law existed -- so
    // they go through duty_from_light(), not duty(), or the gamma darkens
    // them and the fill meter goes flat over its lower third.
    //
    // 2 Hz, as the code this replaces pulsed it. blink exists for REC: it
    // runs at 1 Hz so REC can read it at double rate rather than carrying
    // a second phase.
    //
    // One behavioural difference from the code it replaces, worth naming
    // rather than claiming away: recPhase used to advance only inside the
    // is_recording branch, so a deck lit bright the instant recording began
    // and the two decks pulsed independently. blink is free-running, so
    // recording can now start anywhere in the cycle -- up to 0.25 s dark
    // before it brightens -- and both decks pulse in phase with each other.
    // Harmless, arguably nicer, but not what the earlier commit claimed.
    const float recPh = p.blink < 0.5f ? p.blink * 2.f : p.blink * 2.f - 1.f;
    const int recId[2] = {REC_A_L, REC_B_L};
    for (int part = 0; part < 2; ++part) {
        float v = 0.f;
        const bool sampler = inst.engine_id(part) == spky::ENGINE_SAMPLER;
        if (inst.sampler_is_recording(part))
            v = recPh < 0.5f ? 1.f : 0.25f;
        else if (sampler && !inst.sampler_empty(part))
            v = 0.15f + 0.55f * inst.sampler_fill(part);
        duty_out[recId[part]] = duty_from_light(v, steps);
    }
}

} // namespace spkyled
