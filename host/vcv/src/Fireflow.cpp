#include <cassert>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <vector>
#include <cstdlib>
#include <osdialog.h>
#include "plugin.hpp"
#include "generated_panel.hpp"   // enums + control table (generated from res/gen_panel.py)
#include "init_patch.hpp"       // sampler.vcvm snapshot + non-param init state
#include "form_song_migration.hpp"
#include "link_migration.hpp"
#include "bbd_edge_state.hpp"   // ENG->BBD edge detector (dependency-free, unit-tested)
#include "song_rung_state.hpp"  // SONG rung tracker (dependency-free, unit-tested)
#include "drift_settle_state.hpp"  // DRIFT left-stop edge detector (dependency-free, unit-tested)
#include "led_law.hpp"           // the panel's LED display law (Rack-free, unit-tested)
#include "mod_layer.hpp"         // the MOD latch layer's host-computed math (Rack-free, unit-tested)

// The portable engine core -- exactly the same headers the desktop render host
// and (later) the Daisy firmware use. No hardware type crosses this boundary.
#include "instrument.h"
#include "feed/feed_config.h"    // kDepthBase, the LANE_MOTION base on a FEED deck
#include "mod/divisions.h"
#include "mod/song_ladder.h"
#include "sampler_ui.hpp"

using namespace spkyvcv;

// COUPLE swallowed the SYNC switch (task 7, spec 2026-08-09
// hw-control-reduction): SYNC was the right-hand end of COUPLE's own axis.
// Below the split the knob is the FREE world (couple drives the Kuramoto
// lock); at or above it the GRID world (couple sets how tightly the texture
// lanes follow). Each half sweeps 0..1, so the grid world keeps its full
// spread -- "on the grid but breathing" stays reachable. Shared by the
// RATE/TIDE tooltips below and pushParams; mirrored (not shared -- see
// res/test_panel.py) in bench/audition/init_patch.cpp.
static constexpr float kCoupleZoneSplit = 0.5f;

// RATE tooltip: the division name while grid (COUPLE >= split) is on, free
// Hz otherwise. The free branch is multiplied by PACE (spec 2026-08-12
// modulation-pace): SuperModulator::set_pace scales _base_hz in both
// branches of _update_rate, and free_hz's own comment (mod/divisions.h:55-56)
// promises this tooltip shows exactly the Hz the engine runs -- so leaving
// PACE out here would make that promise false the moment PACE moves off
// centre.
//
// The grid branch does NOT multiply PACE in, on purpose: it prints a
// division NAME, not a Hz number, and that name states a ratio to the
// transport's own beat. _update_rate scales this lane's _base_hz by _pace
// (super_modulator.cpp:28) and Instrument::_apply_tempo scales the
// transport's bpm by the same _pace, so the lane still runs at exactly
// division d of the paced transport's beat whatever PACE is doing -- the
// ratio, and so the name, stays true without any PACE term of its own.
// Multiplying PACE into this branch would make the printed name wrong, not
// right.

struct RateQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        if (module && module->params[COUPLE].getValue() >= kCoupleZoneSplit)
            return spky::kDivisions[spky::division_index(getValue())].name;
        const float pace = module
            ? spky::pace_mult(module->params[PACE].getValue()) : 1.f;
        return string::f("%.3f Hz", spky::free_hz(getValue()) * pace);
    }
};

// TIDE tooltip: the ratio-ladder rung while grid is on, the free multiplier
// otherwise (same table the engine snaps to, mod/divisions.h).
struct TideQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        if (module && module->params[COUPLE].getValue() >= kCoupleZoneSplit)
            return spky::kTideNames[spky::tide_index(getValue())];
        return string::f("x%.2f", spky::tide_free(getValue()));
    }
};

// PACE tooltip: prints mod/divisions.h's own pace_mult curve, in the same
// fraction-below-x1 style that curve's doc comment uses ("x1/5.7"), so the
// panel never needs a second copy of what the number means.
static std::string paceNumStr(float v) {
    return std::fabs(v - std::round(v)) < 0.05f
        ? string::f("%.0f", std::round(v)) : string::f("%.1f", v);
}
struct PaceQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        const float m = spky::pace_mult(getValue());
        // Collapse near-unity BEFORE picking a branch. Without this, m just
        // below 1 (knob roughly (0.4929, 0.5)) takes the fraction branch,
        // 1.f / m rounds to 1 under the same 0.05 tolerance paceNumStr uses,
        // and the result is "x1/" + "1" == "x1/1" -- a real string a player
        // could see, not just a display case. The mirror band just above
        // centre already printed "x1" correctly (paceNumStr(m) rounds m
        // itself to 1 there), so this check makes both sides of centre use
        // the same rule instead of one relying on it by accident.
        if (std::fabs(m - 1.f) < 0.05f) return "x1";
        return m < 1.f ? "x1/" + paceNumStr(1.f / m) : "x" + paceNumStr(m);
    }
};

// FLUX RATE tooltip: the synced division name (always synced). The knob IS
// the index now (task 6, spec 2026-08-09 hw-control-reduction) -- no more
// flux_division_index() round-trip through a 0..1 float. PACE (spec
// 2026-08-12 modulation-pace) does not reach here: FLUX's own delay stays in
// real time and never sees PACE's paced BPM (engine/fx/flux.h:44), so this
// division name still means what it always meant -- the same is true, for a
// different reason, of RATE's grid-mode name above it in this file: PACE
// stretches the Hz a grid lane runs at, but stretches the transport's beat
// by the same factor, so that name is still an accurate ratio too. Neither
// tooltip needs a PACE term; only the free-Hz branches (both files) do.
struct FluxRateQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int k = spky::kFluxRateOffset + (int)std::lround(getValue());
        return spky::kDivisions[k].name;
    }
};

// FLUX FB tooltip: percent, reaching >100% into the tanh bloom.
struct FluxFbQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        return string::f("%.0f%%", getValue() * 120.f);
    }
};

// REVERB DECAY tooltip: the room's loop gain as a percentage, the same way
// FLUX FB reads. Past 100% the room feeds itself and blooms, so the number
// tells you exactly where that starts instead of leaving it to be found by
// ear. The figure comes from the engine's own curve -- never recompute it
// here, or the panel and the room drift apart.
struct RevDecayQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        return string::f("%.0f%%",
                         spky::AmbientReverb::decay_loop_gain(getValue()) * 100.f);
    }
};

// The append-only STAGES parameter is retained for patch compatibility, but
// now supplies the BBD engine's normalized PITCH lane base.
struct StagesQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        return string::f("pitch %.0f%%", getValue() * 100.f);
    }
};

// LINK tooltip: unipolar THIN depth over the full knob travel.
struct LinkQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        const float v = getValue();
        return v > 0.005f ? string::f("thin %.0f %%", 100.f * v) : "off";
    }
};

// DETUNE is a panel SMKNOB now (spec 2026-08-09 hw-control-reduction task
// 10, out of the context menu). pushParams squares the knob before it
// reaches the engine (the first ~20 ct is where the fine beating lives, and
// a linear map would squeeze it into a fifth of the travel now that the
// ceiling is 105 ct) -- this display mirrors that same taper so the tooltip
// reads the cents the engine actually applies, not the raw knob position.
struct DetuneQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        const float v = getValue();
        return string::f("%.1f ct",
            v * v * spky::SynthEngine::kDetuneCeilCt);
    }
};

// SUB tooltip on a BBD deck only, where the caption reads INPUT. The
// percentage is the literal quantity, not a knob position: BbdEngine::set_sub
// stores its argument unmapped as _in_gain and process_in() multiplies the
// incoming audio by exactly that (bbd_engine.cpp), so 40% means 40% of what
// arrives -- there is no curve in between to misreport.
//
// Every other engine falls through to Rack's default display on purpose. This
// movement renames one caption and gives that one caption a unit; re-uniting
// the rest of the panel is a separate decision (2026-08-04) and deliberately
// not started here.
//
// Resolves its own deck from paramId, never a hardcoded one, for the same
// reason MelodyQuantity does below: a part must never read the other part's
// ENG.
struct SubQuantity : ParamQuantity {
    bool bbd_deck() {
        if (!module) return false;
        const int engineId = paramId == SUB_B ? ENGINE_B : ENGINE_A;
        return (int)std::lround(module->params[engineId].getValue())
               == spky::ENGINE_BBD;
    }
    std::string getLabel() override {
        return bbd_deck() ? "Input" : ParamQuantity::getLabel();
    }
    std::string getDisplayValueString() override {
        if (!bbd_deck()) return ParamQuantity::getDisplayValueString();
        return string::f("%.0f %%", getValue() * 100.f);
    }
};

// MELODY tooltip: the job changes with the deck's own ENG (spec 2026-08-03
// vcv-engine-aware-captions) -- Variation off the Sampler, Scan on it, the
// same split the faceplate caption (VARY/SCAN) draws. Resolves its own deck
// from paramId (MELODY_A vs MELODY_B), never a hardcoded one, so A and B
// report independently and a part never reads the other part's ENG.
struct MelodyQuantity : ParamQuantity {
    std::string getLabel() override {
        if (!module) return "Variation";
        const int engineId = paramId == MELODY_B ? ENGINE_B : ENGINE_A;
        const int eng = (int)std::lround(module->params[engineId].getValue());
        return eng == 1 ? "Scan" : "Variation";
    }
};

// CHOKE tooltip: zone-aware since the knob went continuous (plan
// 2026-08-22-choke-sidechain-duck -- task 1 gave the engine three zones per
// side by |c| instead of 5 snapped detents; this reads the same zones back).
// |c| == 0 is bypass; the duck zone (0 < |c| <= 0.5) reports the depth
// percentage the engine's own ramp reaches at this knob position
// (instrument.cpp: depth = min(amt, 0.5) * 2.4, glided but the tooltip reads
// the target); past 0.5 the two choke zones read exactly what the old
// 5-state switch printed. Sign picks the side the same way instrument.cpp's
// own pri/yld split does: negative = A has priority (ducks/chokes B),
// positive = B has priority.
//
// The zone runs to 120 %, not to 100 %: past 100 % (|c| = 0.4167) the engine
// stops deepening the multiply and starts shortening the follower instead, so
// the reading is called "pumps" rather than "ducks" there. Same knob, same
// zone, different mechanism -- and the word is the only warning the panel gives
// that the last sixth behaves differently.
struct ChokeQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        const float c = getValue();
        const float amt = std::fabs(c);
        if (amt < 1e-6f) return "Off";
        const char* pri = c < 0.f ? "A" : "B";
        const char* yld = c < 0.f ? "B" : "A";
        if (amt <= 0.5f) {
            const float pct = std::min(amt, 0.5f) * 2.4f * 100.f;
            return string::f("%s %s %s %.0f %%", pri,
                              pct > 100.f ? "pumps" : "ducks", yld, pct);
        }
        if (amt <= 0.75f)
            return string::f("%s chokes %s while playing", pri, yld);
        return string::f("%s chokes %s thru decay", pri, yld);
    }
};

struct ParamMenuSlider : ui::Slider {
    explicit ParamMenuSlider(ParamQuantity* pq) {
        box.size.x = 180.f;
        quantity = pq;
    }
};

// ENG is a five-position Rack switch (Synth/Sampler/Wave/Body/BBD). VCVLatch
// retains Rack's native switch handling; this overlay only makes the
// non-Synth positions readable at a glance without changing its footprint.
// Indexed defensively: an out-of-range state (there isn't one today) reuses
// the last shade rather than reading past the array.
static const NVGcolor kEngineShades[] = {
    nvgRGBA(0, 0, 0, 0),          // Synth: no ring
    nvgRGBA(255, 174, 92, 105),   // Sampler: amber
    nvgRGBA(120, 210, 255, 145),  // Wave: blue
    nvgRGBA(160, 255, 150, 140),  // Body: green
    nvgRGBA(230, 140, 255, 140),  // BBD: violet
    nvgRGBA(230, 140, 110, 140),  // Feed: warm ember
};
struct EngineCycleLatch : VCVLatch {
    void drawLayer(const DrawArgs& args, int layer) override {
        VCVLatch::drawLayer(args, layer);
        if (layer != 1) return;
        engine::ParamQuantity* pq = getParamQuantity();
        if (!pq) return;
        const int state = static_cast<int>(std::round(pq->getValue()));
        if (state == 0) return;
        constexpr int kShadeCount = sizeof(kEngineShades) / sizeof(kEngineShades[0]);
        const NVGcolor color = kEngineShades[
            state >= 0 && state < kShadeCount ? state : kShadeCount - 1];
        const Vec c = box.size.div(2.f);
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, c.x, c.y, 5.2f);
        nvgStrokeWidth(args.vg, 1.4f);
        nvgStrokeColor(args.vg, color);
        nvgStroke(args.vg);
    }
};

// SCALE tooltip: the raw index carried meaning at six entries and stopped
// carrying it at thirteen. Names come from the engine's table, never a copy.
struct ScaleQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int i = (int)std::round(getValue());
        if (i < 0) i = 0;
        if (i >= spky::SCALE_LIST_COUNT) i = spky::SCALE_LIST_COUNT - 1;
        return spky::SCALE_NAMES[i];
    }
};

// Part A params occupy [0..PART_STRIDE), part B the next PART_STRIDE. The
// generator lays both part blocks out identically (same order, mirrored x) and
// emits PART_STRIDE; these guards catch any drift.
static_assert(RATE_B == RATE_A + PART_STRIDE, "part-block stride drifted from generator");
static_assert(TUNE_B == TUNE_A + PART_STRIDE, "part-block stride drifted from generator");
static_assert(SONG_B == SONG_A + PART_STRIDE, "part-block stride drifted from generator");

// REC is the exception and must never be read with pp()/ppb(). M5b appended it
// after the part blocks (gen_panel.py:359) so that adding it would not grow
// PART_STRIDE and shift every already-saved patch's part-B ids -- so REC_A/REC_B
// are adjacent trailing ids, not a strided pair. ppb(REC_A, 1) therefore indexed
// REC_A + 23 == 99 into a 78-element params vector: out of bounds, and part B
// could never record. Read them as `p ? REC_B : REC_A`, the way the REC lights
// already do.
static_assert(REC_B == REC_A + 1,
              "REC ids are trailing, not part-strided -- read them explicitly, never via pp()");

// REC used to close the param list; the MOD layer's appended block trails it
// now. The hazard the old assert guarded is UPGRADED, not gone: a pp()/ppb()
// read of any appended id no longer indexes out of bounds -- it silently
// aliases a mod-layer param. Appended params are read explicitly or through
// kModLayer, never via pp().
static_assert(MODBTN > REC_B, "mod-layer params must stay appended after REC");
static_assert(NUM_PARAMS == MODBTN + 49, "mod layer is 49 params: MODBTN + 48 depths");

struct Fireflow : Module {
    spky::Instrument inst;
    spky::FxMem fxmem;

    // The 4 MiB stereo tape arena is heap-backed so each Rack Module remains
    // small enough for normal stack/value construction.
    std::vector<float> echoMem[spky::PART_COUNT][2];
    // The BBD part engine's two lines per deck, held by value like the echo
    // buffer above: 32 KB per line, 128 KB per module instance.
    float bbd[spky::PART_COUNT][2][spky::BbdEngine::kCells];
    spky::AmbientReverb reverb;

    // The texture deck's record buffers. Unlike the echo/reverb memory above
    // these are NOT held by value: 42 s of stereo at 48 kHz is ~16 MB per
    // part. The engine's "no heap" contract binds engine/, not the host --
    // hosts allocate (desktop: std::vector, M6 firmware: SDRAM).
    static constexpr double kSamplerBufferSeconds = 42.0;
    std::vector<spky::SampleBuffer::Frame> samplerMem[spky::PART_COUNT];
    spkyvcv::SamplerPartState smp[spky::PART_COUNT];

    // First-use factory sample (Task 8): flipping ENG to Sampler on an empty
    // part autoloads res/factory.wav so the deck sounds within one gesture.
    // factoryTried[p] stops a failed load or a deliberate Clear from
    // re-triggering on the next control tick -- only onReset() clears it.
    //
    // The WAV itself must never be read from disk on the audio thread, so the
    // read is split from the decision: factoryNative holds the file decoded
    // at its OWN (native) sample rate, read at most once per module instance
    // in onAdd() (main thread, before process() ever runs for this
    // instance -- see onAdd() below). factoryL/factoryR hold that same audio
    // resampled to curSr, rebuilt in reinit() every time the engine rate
    // changes (so a device-rate change between part A's and part B's first
    // flip can't leave part B with audio pitched for the old rate).
    // pushParams (audio thread) only ever reads factoryL/factoryR -- a
    // memcpy via inst.load_sample, no disk I/O and no resample.
    bool factoryTried[spky::PART_COUNT] = {false, false};

    // DPTH was a menu slider for exactly one day (2026-08-19). It is a knob
    // now, so Rack persists it as a ParamId and this module holds no state
    // for it at all: the float array, its JSON key, its reset and the
    // slider all left together.
    // Edge-detects the ENG switch landing on BBD, so the FLUX-off and
    // excite-other-deck defaults below (spec 5.11/5.12) apply once on a
    // genuine player-driven transition and never fight a player who
    // deliberately turns them back on afterward -- and never fire at all on
    // a RESTORE that lands on BBD (fresh add, whole-patch load, Ctrl+D
    // duplicate, or an already-live preset Load/paste -- see
    // dataFromJson()'s rearm() call). Full reasoning, and why this is its
    // own unit-tested type rather than inline bools, lives in
    // bbd_edge_state.hpp.
    spkyvcv::BbdEdgeState bbdEdge[spky::PART_COUNT];
    spky::WavData factoryNative;
    bool factoryNativeTried = false;
    std::vector<float> factoryL, factoryR;

    float curSr = 0.f;
    // MOD latch layer state for one control tick: the lane outputs and the two
    // masters, sampled once at the top of pushParams so every mv() read in the
    // same tick sees the same modulation frame (spec 2026-08-22 §3b).
    float laneOut[spky::PART_COUNT][spky::LANE_COUNT] = {};
    float modMaster[spky::PART_COUNT] = {};
    // Reverse index into kModLayer, keyed by SOUND param id: -1 means the face
    // owns no depth param. Built once in the constructor -- mv() runs per param
    // per control tick and must not scan the table.
    int modIdxBySound[NUM_PARAMS];
    dsp::ClockDivider ctrlDiv;              // throttle param push to control rate
    dsp::SchmittTrigger clockTrig, resetTrig;
    // Tracks the SONG knob's current rung so pushParams can detect a genuine
    // rung change and re-roll the phrase -- SONG swallowed FORM and the NEW
    // pad (spec 2026-08-09 hw-control-reduction task 3). Seeded/rearm shape
    // (song_rung_state.hpp) so a RESTORED rung -- patch load, preset load,
    // module add, or Initialize -- adopts as a baseline instead of firing.
    spkyvcv::SongRungState songRung[spky::PART_COUNT];
    // Edge-detects DRIFT parking at its own left stop -- the old SETL pad's
    // job, folded into the knob's lower kDriftSettleZone (spec 2026-08-09
    // hw-control-reduction task 8). Same seeded/rearm shape as bbdEdge/
    // songRung above, for the same reason: a RESTORED DRIFT already in the
    // zone must adopt as a baseline instead of firing settle() on load. See
    // drift_settle_state.hpp.
    spkyvcv::DriftSettleState driftSettled;
    float clkSamples = 0.f;                 // samples since last external clock edge
    // The mux scan gives the hardware 16 brightness steps for free, so Rack
    // quantises to the same raster -- and applies the same perceptual gamma
    // curve on the way there (led_law.hpp's duty()) -- because a module that
    // breathes more finely, or more linearly, than the panel ever can is
    // validating itself against the wrong instrument.
    static constexpr int kLedSteps = 16;
    spkyled::Panel ledPanel;
    dsp::ClockDivider ledDiv;               // throttle the LED law; 750 Hz at 48 kHz, scales with sample rate
    int ledDuty[NUM_LIGHTS] = {0};
    std::atomic<bool> resyncReq { false };  // menu "Resync to bar" -> audio thread
    bool pendingRestore = false;    // dataFromJson ran before onAdd; content reload deferred

    Fireflow() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configControls();
        for (int i = 0; i < NUM_PARAMS; ++i) modIdxBySound[i] = -1;
        for (size_t i = 0; i < sizeof(kModLayer) / sizeof(kModLayer[0]); ++i)
            modIdxBySound[kModLayer[i].soundId] = (int)i;
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            fxmem.bbd[p][0] = bbd[p][0];
            fxmem.bbd[p][1] = bbd[p][1];
        }
        fxmem.reverb = &reverb;
        ctrlDiv.setDivision(16);
        ledDiv.setDivision(64);
    }

    void configControls() {
        for (const auto& c : kParamCtls) {
            const std::string lbl = c.tip;
            const float init = initParamDefault(c.id);
            switch (c.kind) {
                case WK_BIGKNOB:
                case WK_SMKNOB:
                    if (c.id == RATE_A || c.id == RATE_B)
                        configParam<RateQuantity>(c.id, 0.f, 1.f, init, lbl);
                    else if (c.id == CHOKE)  // event-priority, continuous, zone-aware tooltip
                        configParam<ChokeQuantity>(c.id, -1.f, 1.f, init, lbl);
                    else if (c.id == FILT_A || c.id == FILT_B)  // bipolar cutoff trim
                        configParam(c.id, -1.f, 1.f, init, lbl);
                    else if (c.id == TIDE)  // texture-lane rate, snaps in the GRID zone
                        configParam<TideQuantity>(c.id, 0.f, 1.f, init, lbl);
                    else if (c.id == FLUXFB_A || c.id == FLUXFB_B)
                        configParam<FluxFbQuantity>(c.id, 0.f, 1.f, init, lbl);
                    else if (c.id == REV_DECAY)
                        configParam<RevDecayQuantity>(c.id, 0.f, 1.f, init, lbl);
                    else if (c.id == LINK_A || c.id == LINK_B)
                        configParam<LinkQuantity>(c.id, 0.f, 1.f, init, lbl);
                    else if (c.id == STAGES_A || c.id == STAGES_B)
                        configParam<StagesQuantity>(c.id, 0.f, 1.f, init, lbl);
                    else if (c.id == SUB_A || c.id == SUB_B)  // INPUT (%) on BBD
                        configParam<SubQuantity>(c.id, 0.f, 1.f, init, lbl);
                    else if (c.id == SOURCE_A || c.id == SOURCE_B) {
                        auto* source = configParam(
                            c.id, 0.f, 1.f, init,
                            c.id == SOURCE_A ? "SOURCE A" : "SOURCE B");
                        source->description =
                            "Controls Synth TIMB, Sampler ORG, Wave FRAME, Body MATL, BBD DRIVE or Feed BOND according to the selected engine.";
                    }
                    else if (c.id == DETUNE_A || c.id == DETUNE_B)
                        // DETUNE is a real panel control now (kParamCtls),
                        // same as every other control in this loop -- only
                        // the display quantity is still bespoke, for the
                        // squared-cents tooltip (see DetuneQuantity above).
                        configParam<DetuneQuantity>(c.id, 0.f, 1.f, init, lbl);
                    else if (c.id == PACE)
                        configParam<PaceQuantity>(c.id, 0.f, 1.f, init, lbl);
                    else
                        configParam(c.id, 0.f, 1.f, init, lbl);
                    break;
                case WK_KNOBC:
                    if (c.id == MELODY_A || c.id == MELODY_B)
                        // MELODY (bipolar): both decks loop — A drifts a
                        // little, B is frozen. Tooltip name follows ENG
                        // through MelodyQuantity.
                        configParam<MelodyQuantity>(c.id, -1.f, 1.f, init, lbl);
                    else
                        // GRIT (bipolar): sign picks Drive/Reduce, magnitude
                        // is the mix (spec 2026-08-09 hw-control-reduction
                        // task 4; see pushParams for the dead-zone math).
                        configParam(c.id, -1.f, 1.f, init, lbl);
                    break;
                case WK_KNOBI:
                    if (c.id == SCALE)
                        // The default comes from the snapshot like every other
                        // control's. It used to be hard-coded to
                        // spky::SCALE_LYDIAN here, which is how the module
                        // could boot Lydian while INIT_DEFAULTS said
                        // Mixolydian -- the bench audition read the table, the
                        // panel did not, and nothing compared them.
                        configParam<ScaleQuantity>(c.id, 0.f, (float)(spky::SCALE_LIST_COUNT - 1),
                                                   init, "Scale");
                    else if (c.id == SONG_A || c.id == SONG_B) {
                        // SONG walks a curated 14-rung ladder through the
                        // (Principle, SongMode) grid (engine/mod/song_ladder.h)
                        // -- FORM is gone, absorbed into this one knob. Labels
                        // are composed from the ladder itself, one source, so
                        // the table and its labels can never drift apart.
                        static const char* kFormWords[] = {
                            "TWO MOTIFS", "ONE + VAR", "HIERARCHICAL",
                            "CALL / RESPONSE", "OSTINATO"};
                        static const char* kSongWords[] = {
                            "AAAB", "ABAB", "ABBB", "BUILD",
                            "ROTATE", "MIRROR", "OFF"};
                        std::vector<std::string> rungs;
                        for (int i = 0; i < spky::kSongLadderCount; ++i) {
                            const spky::SongRung& r = spky::song_ladder_at(i);
                            rungs.push_back(std::string(kFormWords[r.form]) +
                                            " / " + kSongWords[r.song]);
                        }
                        configSwitch(c.id, 0.f,
                                     float(spky::kSongLadderCount - 1),
                                     init, "Song", rungs);
                    }
                    // TIME: 12-detent knob over the synced FLUX divisions
                    // (task 6, spec 2026-08-09 hw-control-reduction). The
                    // knob value IS the index now -- 0..kFluxRateCount - 1,
                    // spky::kFluxRateCount reachable positions (0..11) --
                    // replacing the old 0..1 SMKNOB that ran through
                    // flux_division_index().
                    else if (c.id == FLUXRATE_A || c.id == FLUXRATE_B)
                        configParam<FluxRateQuantity>(
                            c.id, 0.f, (float)(spky::kFluxRateCount - 1),
                            init, lbl);
                    else  // STEPS_A / STEPS_B
                        configParam(c.id, 0.f, 16.f, init, "Steps");
                    getParamQuantity(c.id)->snapEnabled = true;
                    break;
                case WK_SW2:
                    // Unreachable: no control in kParamCtls carries WK_SW2 any
                    // more (it was SYNC's kind; COUPLE absorbed SYNC as the
                    // right-hand end of its own axis, task 7, spec 2026-08-09
                    // hw-control-reduction). The enum entry stays alive only
                    // because the HW draft widget's own switch (below) still
                    // handles it. This branch used to silently configure
                    // WHATEVER lands here next as a switch named "Sync" with
                    // "Free"/"Synced" labels -- a loud stop instead: do not
                    // resurrect that call for a future WK_SW2 control, write
                    // fresh labels for whatever it actually is.
                    assert(false &&
                           "WK_SW2: no live control uses this kind in configControls()");
                    break;
                case WK_LATCH:
                    if (c.id == REC_A || c.id == REC_B)
                        configSwitch(c.id, 0.f, 1.f, init, "Record",
                                     {"Stopped", "Recording"});
                    else if (c.id == ENGINE_A || c.id == ENGINE_B) {
                        configSwitch(c.id, 0.f, 5.f, init, "Engine",
                                     {"Synth", "Sampler", "Wave", "Body", "BBD",
                                      "Feed"});
                        getParamQuantity(c.id)->snapEnabled = true;
                    }
                    else {
                        // Unreachable: the old STEP pad's boolean latch merged
                        // into STEPS_A/STEPS_B (a WK_KNOBI knob) long before
                        // this branch, so no LATCH-kind control besides
                        // REC_A/B and ENGINE_A/B reaches here today. Loud stop
                        // instead of silently configuring a future third
                        // LATCH control as an "Off"/"On" STEP switch it may
                        // not be.
                        assert(false &&
                               "WK_LATCH: control is neither REC_A/B nor ENGINE_A/B");
                    }
                    break;
                case WK_SMBTN:
                    // Unreachable: no control in kParamCtls carries WK_SMBTN
                    // any more. The enum entry stays alive only because the HW
                    // draft widget's own switch (below) still handles it.
                    assert(false &&
                           "WK_SMBTN: no live control uses this kind in configControls()");
                    break;
                default: break;
            }
        }
        // DRIVE_A/B retired (spec 2026-08-09 hw-control-reduction task 9): the
        // menu-only quantity and its submenu slider are gone -- the value
        // never reached the engine (see the pushParams comment below where
        // its old id used to be read). DETUNE (task 10) rejoined kParamCtls
        // earlier and no longer needs an explicit call of its own -- see the
        // DETUNE_A/B branch inside the loop above.
        // panel labels are short ("L", "PIT"); the group legend carries the rest,
        // so tooltips use the control table's spelled-out tip instead
        for (const auto& c : kInputCtls)  configInput(c.id, c.tip);
        for (const auto& c : kHwModInputCtls) configInput(c.id, c.tip);
        for (const auto& c : kOutputCtls) configOutput(c.id, c.tip);

        // MOD latch layer (spec 2026-08-22). The depth params are deliberately
        // NOT in kParamCtls -- the big panel never shows them; the HW widget
        // stacks each one on its sound twin and the latch picks which is
        // visible. Names come from the generator, so the tooltip and the
        // wreath on the plate can never drift apart. Every depth is unipolar
        // 0..1 whatever its sound twin's range is: it scales a swing, it is
        // not a second copy of the knob.
        configSwitch(MODBTN, 0.f, 1.f, initParamDefault(MODBTN), "MOD layer",
                     {"Off", "On"});
        for (const auto& t : kModLayer)
            configParam(t.depthId, 0.f, 1.f, initParamDefault(t.depthId), t.name);
    }

    // Re-init the engine for a new sample rate. Without the snapshot below,
    // every rate change silently discarded a loaded or recorded sample (an
    // M5a finding). Two things destroy it: the buffers are sized in FRAMES so
    // a rate change resizes them, and inst.init() ends in SampleBuffer::clear()
    // which memsets the whole buffer. So the content must be copied OUT into
    // separate storage first and pushed back afterwards -- reading it out of
    // the buffer after init() would read zeroes.
    //
    // Up to 16 MB per part, twice, but only when the user changes their audio
    // device, and onSampleRateChange runs on the main thread with the engine
    // paused. The snapshot is NOT resampled -- it plays transposed at the new
    // rate. That is varispeed, and it is the instrument's idiom. (File LOADS
    // do resample, see sampler_ui.hpp: importing at the wrong pitch is a bug,
    // re-rating material already in the buffer is tape.)
    void reinit(float sr) {
        std::vector<float> snapL[spky::PART_COUNT], snapR[spky::PART_COUNT];
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            const size_t n = inst.sampler_rec_size(p);
            const spky::SampleBuffer::Frame* f = inst.sampler_data(p);
            if (!n || !f) continue;
            snapL[p].resize(n);
            snapR[p].resize(n);
            for (size_t i = 0; i < n; ++i) { snapL[p][i] = f[i].l; snapR[p][i] = f[i].r; }
        }

        const float prevSr = curSr;
        curSr = sr;
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            for (int ch = 0; ch < 2; ++ch) {
                if (echoMem[p][ch].size() != spky::Flux::kMaxSamples)
                    echoMem[p][ch].resize(spky::Flux::kMaxSamples);
                fxmem.echo[p][ch] = echoMem[p][ch].data();
            }
        }
        const size_t frames = (size_t)(kSamplerBufferSeconds * (double)sr);
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            if (samplerMem[p].size() != frames) samplerMem[p].resize(frames);
            fxmem.sampler_buf[p] = samplerMem[p].data();
        }
        fxmem.sampler_frames = frames;

        inst.init(sr, fxmem);

        for (int p = 0; p < spky::PART_COUNT; ++p)
            if (!snapL[p].empty())
                inst.load_sample(p, snapL[p].data(), snapR[p].data(), snapL[p].size());

        // Rebuild the factory-drone cache for the (possibly new) rate.
        // factoryNative is decoded from disk exactly once, in onAdd() --
        // this only resamples the already-decoded buffer, so reinit() stays
        // disk-free even when it's called from process()'s reactive
        // sample-rate-change fallback. Skipped when the rate hasn't actually
        // changed and the cache is already built, so calling reinit() twice
        // in a row at the same rate (documented above, and onReset() does
        // exactly this) doesn't redo the resample for nothing.
        if (!factoryNative.l.empty() && (sr != prevSr || factoryL.empty())) {
            factoryL = factoryNative.l;
            factoryR = factoryNative.r;
            if (factoryNative.sample_rate > 0
                && (float)factoryNative.sample_rate != sr) {
                const double ratio = (double)sr / (double)factoryNative.sample_rate;
                spkyvcv::resample_linear(factoryL, ratio);
                spkyvcv::resample_linear(factoryR, ratio);
            }
        }
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        reinit(e.sampleRate);
    }

    // Read a per-part param: baseId is the PART A enum, part in {0,1}.
    inline float pp(int baseA, int part) {
        return params[baseA + part * PART_STRIDE].getValue();
    }
    inline bool ppb(int baseA, int part) { return pp(baseA, part) > 0.5f; }

    // MOD-layer read of a host-computed sound param: knob + depth * MOD *
    // lane, in KNOB space, clamped to the param's own declared range (spec
    // 2026-08-22 §3b). Non-targets and the engine-backed faces fall straight
    // through to the knob -- their modulation happens inside the Part, so
    // adding a host-side term here would modulate them twice.
    //
    // At init every host-computed depth is 0 and modded() returns the knob by
    // early return, so this whole layer is a no-op on a fresh patch: the push
    // stream is exactly what it was before the layer existed.
    inline float mv(int soundId, int part) {
        const float v = params[soundId].getValue();
        const int mi = modIdxBySound[soundId];
        if (mi < 0) return v;
        const ModTarget& t = kModLayer[mi];
        if (t.kind != MODK_HOST) return v;
        // part == 2 marks a center-column target: both decks mixed, so both
        // masters down means the center is still.
        const float term = (t.part == 2)
            ? spkymod::center_term(modMaster[0], laneOut[0][t.slot],
                                   modMaster[1], laneOut[1][t.slot])
            : spkymod::lane_term(modMaster[part], laneOut[part][t.slot]);
        ParamQuantity* q = paramQuantities[soundId];
        return spkymod::modded(v, params[t.depthId].getValue(), term,
                               q->getMinValue(), q->getMaxValue());
    }
    // Strided twin of pp(). Only valid inside the part blocks, exactly like
    // pp() itself -- the appended pairs (COLOR/LINK/FILT/FLUX/FLUXFB/REV_MIX/
    // DEPTH/STAGES) must go through mv(p ? X_B : X_A, p).
    inline float mvp(int baseA, int part) {
        return mv(baseA + part * PART_STRIDE, part);
    }

    // GRIT is one bipolar knob (spec 2026-08-09 hw-control-reduction task
    // 4): sign picks the mode, magnitude is the mix. The dead zone exists
    // because a 9 mm pot on an ADC cannot hit an exact zero -- without it
    // "off" would be unreachable on hardware. Shared between the fx_on gate
    // below and the mode/mix push further down so both agree on what
    // "engaged" means.
    static constexpr float kGritDead = 0.03f;

    void pushParams() {
        // Sample the modulation frame once per control tick (spec 2026-08-22
        // §3b), before any mv() read: one frozen frame per tick means deck A's
        // first knob and deck B's last knob see the same lane positions, and
        // the center's mix of both decks is taken at one instant.
        for (int p = 0; p < 2; ++p) {
            modMaster[p] = pp(MOD_A, p);
            for (int s = 0; s < spky::LANE_COUNT; ++s)
                laneOut[p][s] = inst.lane_output(p, s);
        }

        // STEP entry latches the groove target immediately. Push the shared
        // amount before either deck sees its FLOW->STEP transition so both
        // decks latch the value from this same control update.
        inst.set_shuffle(params[SHUFFLE].getValue());
        for (int p = 0; p < 2; ++p) {
            inst.set_rate(p, mvp(RATE_A, p));
            inst.set_shape(p, mvp(SHAPE_A, p));
            inst.set_density(p, mvp(DENSITY_A, p));
            inst.set_smooth(p, mvp(SMOOTH_A, p));
            inst.set_range(p, mvp(RANGE_A, p));
            // MOD is the per-deck master in both modes and is never itself
            // modulated (spec §2) -- a raw pp() read on purpose.
            inst.set_depth(p, pp(MOD_A, p));
            inst.set_tune(p, mvp(TUNE_A, p));

            inst.set_voice_attack(p, mvp(ATTACK_A, p));
            inst.set_voice_decay(p, mvp(DECAY_A, p));
            inst.set_voice_resonance(p, mvp(RES_A, p));
            // FILT is engine-backed (its depth writes _tdepth[LANE_SIZE] in
            // step 6 below), so the knob stays the raw trim it always was.
            inst.set_voice_filt(p, params[p ? FILT_B : FILT_A].getValue());
            inst.set_color(p, mv(p ? COLOR_B : COLOR_A, p));
            inst.set_voice_sub(p, mvp(SUB_A, p));
            // Quadratic taper: the first ~20 ct is where the fine beating
            // lives, and a linear map would squeeze it into a fifth of the
            // travel now that the ceiling is 105 ct.
            //
            // Not on a FEED deck. There DETUNE means SPREAD and gets to the
            // engine as the LANE_SIZE base further down -- the sampler's
            // SUB -> LANE_SIZE re-point, one entry further down the same
            // ledger. It is passed RAW there, not squared: FEED owns its own
            // curve in feed_cfg's two-segment SPREAD map, and applying
            // DetuneQuantity's square on top would compress the single-digit
            // region the spec reserves for the lower half.
            if (inst.engine_id(p) != spky::ENGINE_FEED) {
                // The MOD-layer term lands in KNOB space, before the square
                // (spec §3b): modulating the mapped value would make the same
                // depth mean a different number of cents at every knob
                // position.
                const float detKnob = mvp(DETUNE_A, p);
                inst.set_voice_detune(p, detKnob * detKnob);
            }

            inst.set_flux_mix(p, pp(FLUX_A, p));
            inst.set_flux_rate(p, (int)std::lround(
                params[p ? FLUXRATE_B : FLUXRATE_A].getValue()));
            inst.set_fx_target_base(p, spky::FXT_FLUX_FB,
                params[p ? FLUXFB_B : FLUXFB_A].getValue());
            // The tape multiplier keeps its modulation sink but loses its knob:
            // 0.5 is the neutral multiplier (tape_time_mult(0.5) == 1), so CV
            // and the mod lanes still bend the tape while the panel does not.
            inst.set_fx_target_base(p, spky::FXT_FLUX_TIME, 0.5f);
            // Appended params are outside the stride, so pp() would compute the
            // wrong id — the explicit ternary is required (see FLUXRATE/FLUXFB).
            inst.set_link(p, mv(p ? LINK_B : LINK_A, p));
            // STAGES itself is pushed further down, alongside samplerPart's
            // analogous re-point gate -- it needs this tick's dispatched
            // engine_id(p), which set_engine (below) hasn't set yet here.
            // The FX blocks are gated by an explicit on/off (a pad on hardware,
            // a scenario action on the desktop). VCV has no such pad, so the mix
            // knob doubles as the on switch: knob up == engaged. At 0 the block
            // stays idle and the whole chain is skipped (bit-exact bypass).
            inst.set_fx_on(p, spky::FxBlock::Flux, pp(FLUX_A, p) > 1e-4f);
            // GRIT is bipolar now: "engaged" means the knob has cleared the
            // dead zone in either direction, not just a positive value --
            // the raw value alone would silently mute the whole CRSH
            // (negative) side (see kGritDead and pushParams' grit block).
            inst.set_fx_on(p, spky::FxBlock::Grit,
                            std::fabs(pp(GRIT_A, p)) > kGritDead);
            // LVL/COMP: the lower zone is pure output gain (Comp::set_amount(0)
            // is a bit-exact bypass, so it costs no compressor CPU); the top
            // two fifths engage the compressor with make-up, ending at the 0.7
            // that used to be the knob's working value.
            //
            // Both the split and the shape are about loudness per degree of
            // travel. Comp::update_curve makes make-up strongly superlinear in
            // the amount (_makeup_db = -_thr_db * (1 - 1/ratio) * 0.9, with
            // ratio = 1 + 9a^2), so a LINEAR ramp across a narrow zone dumps
            // most of its gain into the last few degrees: at the old 0.8 split
            // the final tenth of the knob was worth +11.2 dB while the tenth
            // just below the seam was worth +1.2 dB. A tenfold step change in
            // sensitivity exactly where the hand crosses over reads as the
            // volume pulling away at the top, which is what it was doing.
            //
            // Widening the zone alone does not fix that -- the a^2 term simply
            // moves the same cliff to the right. kCompShape is the other half:
            // raising the zone position to 0.6 front-loads the amount so
            // make-up grows nearly LINEARLY IN dB across the zone (3.6..4.8 dB
            // per tenth of travel, against 5.3 then 11.2 before). The exponent
            // is fitted to update_curve's law above; change one and the other
            // stops being right.
            //
            // kCompTop stays 0.7: full travel still reaches the compressor
            // character the old knob was habitually parked at.
            static constexpr float kLvlCompSplit = 0.6f;
            static constexpr float kCompTop      = 0.7f;
            static constexpr float kCompShape    = 0.6f;
            // One face, one read: the MOD-layer term is applied once here, so
            // the gain leg and the compressor leg stay two halves of the same
            // knob travel rather than drifting apart under modulation.
            const float lvlKnob = mvp(COMP_A, p);
            inst.set_part_level(p, std::min(1.f, lvlKnob / kLvlCompSplit));
            inst.set_comp(p, lvlKnob <= kLvlCompSplit ? 0.f
                             : kCompTop * std::pow(
                                   (lvlKnob - kLvlCompSplit) /
                                   (1.f - kLvlCompSplit), kCompShape));

            // Saved ENG meanings remain 0 = Synth and 1 = Sampler; 2 adds
            // Wave, 3 Body, 4 the BBD, 5 FEED. Each new engine needs its own
            // explicit arm here -- anything that isn't 0/2/3/4/5 still falls
            // through to Sampler (or the dev test tone), which is also why old
            // patches keep their exact meaning. The test tone stays a
            // Sampler-only override.
            const int eng = static_cast<int>(std::round(pp(ENGINE_A, p)));
            const spky::EngineId id =
                eng == 0 ? spky::ENGINE_SYNTH :
                eng == 2 ? spky::ENGINE_WAVE :
                eng == 3 ? spky::ENGINE_BODY :
                eng == 4 ? spky::ENGINE_BBD :
                eng == 5 ? spky::ENGINE_FEED :
                smp[p].testTone ? spky::ENGINE_TEST_TONE : spky::ENGINE_SAMPLER;
            inst.set_engine(p, id);

            // The excitation bus is patch state (design spec §6), pushed
            // every control tick like the other per-part settings below --
            // cheap, idempotent, and correct after a patch load without a
            // separate "apply on restore" path.
            inst.set_excitation_sources(p, smp[p].exciteTape,
                                         smp[p].exciteOtherDeck,
                                         smp[p].exciteAudioIn);

            // First-user experience: flipping ENG to Sampler on an empty part
            // loads the factory drone, so one pad press makes sound. It never
            // overwrites content -- sampler_empty() is the whole guard, and
            // once loaded it behaves like any other sample (REC overdubs it,
            // Clear clears it, and factoryLoaded keeps it out of patch
            // storage).
            // factoryL/factoryR are prepared off the audio thread (onAdd()
            // reads+decodes, reinit() resamples -- see the members' comment
            // above). load_sample() itself still begins with SampleBuffer::
            // clear(), which used to memset the WHOLE 42 s allocation (16.1
            // MB) unconditionally -- 1.5-3 ms inside one process() call
            // against a 5.3 ms budget at a 256-sample block, worse at 128/64
            // (I-3). This branch only ever fires when sampler_empty(p) is
            // true, i.e. the buffer's _size is already 0, so with
            // SampleBuffer::clear()'s _size==0 fast path (sample_buffer.cpp)
            // that memset is skipped here: what actually runs is the guard
            // check above plus a ~3.4 MB memcpy of the factory sample.
            if (eng == 1 && !smp[p].testTone && inst.sampler_empty(p)
                     && !factoryTried[p]) {
                factoryTried[p] = true;
                if (!factoryL.empty()) {
                    inst.load_sample(p, factoryL.data(), factoryR.data(),
                                     factoryL.size());
                    smp[p].factoryLoaded = true;
                }
            }

            inst.sampler_speed_mode(p, smp[p].tapeIdx != 0);
            inst.sampler_reverse(p, smp[p].reverse);
            inst.sampler_feedback(p, smp[p].feedback);

            // REC is a latch, so its value IS the desired state -- an edge
            // trigger would miss a state restored from a saved patch. The
            // engine's set_recording is idempotent, and sampler_record flips
            // monitoring with it, so pushing every control tick is correct.
            // On a synth part REC is inert: ENG is the only mode selector.
            // NOT ppb(REC_A, p): REC is not part-strided (see the static_assert
            // block near the top of this file).
            const bool wantRec = params[p ? REC_B : REC_A].getValue() > 0.5f
                                 && inst.engine_id(p) == spky::ENGINE_SAMPLER;
            if (wantRec != inst.sampler_is_recording(p)) {
                inst.sampler_record(p, wantRec);
                // path/factoryLoaded mean "the buffer still holds exactly
                // what that source provided" -- once recording starts, the
                // buffer no longer matches either source, so the part must
                // stop claiming one.
                if (wantRec) {
                    smp[p].path.clear();
                    smp[p].factoryLoaded = false;
                }
            }

            // --- sampler control surface (spec 2026-07-21 morphagene-controls) ---
            // Four knobs that do nothing in the sampler's FLOW cloud get a
            // job of their own. The param ids do not change, so no saved
            // patch moves; only what the knob means when ENG says Sampler.
            //
            // set_density above keeps firing unconditionally -- the "push to
            // both, let the inactive side ignore it" pattern the voice row
            // already uses. set_variation left that pattern when MELODY became
            // SCAN-only on a Sampler deck (spec 2026-08-03); it is pushed
            // below, behind the same samplerPart gate. DENS is the one knob
            // that genuinely does two things in sampler STEP mode: it still
            // thins the groove gate AND now sets grain overlap. Both point the
            // same direction (sparser), so this is left as-is.
            const bool samplerPart = inst.engine_id(p) == spky::ENGINE_SAMPLER;
            inst.sampler_overlap(p, pp(DENSITY_A, p));
            inst.set_target_base(p, spky::LANE_SOURCE, pp(SOURCE_A, p));

            // Ledger of every lane base this function re-points per engine, so
            // the next addition has one place to check itself against rather
            // than re-discovering the rule by breaking it a third time:
            //   - LANE_SIZE:  sampler (SUB_A -> GENE SIZE) and FEED
            //                 (DETUNE_A -> SPREAD), restored to 0.5f below
            //                 when the deck is neither.
            //   - LANE_PITCH: BBD-only (STAGES_A/B). Other engines retain
            //                 their existing base; this movement only rehomes
            //                 the preserved STAGES state while BBD is active.
            //   - LANE_MOTION: the DPTH knob's base, on every engine, since
            //                 2026-08-19 (no more FEED-only ternary). Before
            //                 2026-08-18 this host never wrote this base at
            //                 all, so the only thing that could reach
            //                 LANE_MOTION in Rack was MOD.
            const bool bbdPart = inst.engine_id(p) == spky::ENGINE_BBD;
            const bool feedPart = inst.engine_id(p) == spky::ENGINE_FEED;
            // STAGES is orphaned by movement 3 and becomes the LANE_PITCH base
            // on a BBD deck. Re-pointing a knob per engine is not new -- the
            // sampler already moves SUB_A to LANE_SIZE as GENE SIZE.
            //
            // STAGES_A/B are appended params (outside the stride, like
            // DRIVE/LINK above), so pp(STAGES_A, p) is wrong for Part B: it
            // would read params[STAGES_A + PART_STRIDE] = params[73 + 23] =
            // params[96], past the end of the 84-entry array. The explicit
            // ternary is required, exactly as for DRIVE/LINK.
            //
            if (bbdPart)
                inst.set_target_base(p, spky::LANE_PITCH,
                    params[p ? STAGES_B : STAGES_A].getValue());

            if (bbdEdge[p].tick(bbdPart)) {
                // Genuine player-driven entry into BBD (see bbd_edge_state.hpp
                // for why a restore can never reach this branch).
                //
                // FLUX defaults disengaged (spec 5.11). The BBD's output is
                // already six poles at 3600 Hz plus a loss pole breathing under
                // a compander, and its gappy repeats are its most distinctive
                // trait -- which a tape echo behind it fills in. The player can
                // add it back; the default should not be darker-and-smeared.
                params[p ? FLUX_B : FLUX_A].setValue(0.f);
                // The silence trap's first half (spec 5.12): a BBD deck with no
                // source selected is an FX unit wired to nothing. Default the
                // neighbouring deck ON. Audio-in already reaches process_in
                // unconditionally through Part::process; what the checkbox gates
                // is the cross-deck bus (movement 1, Part::_src_deck), and that
                // is what makes resampling work without external cabling.
                smp[p].exciteOtherDeck = true;
            }

            // SCAN nur fuer Sampler-Parts (K-03). Der urspruengliche Grund --
            // set_scan -> scan_rate enthielt im unteren Zweig ein std::pow,
            // und bei ctrlDiv = 16 waren das bis zu 6000 Aufrufe/s im
            // Audio-Callback fuer eine Engine, die niemand hoert -- ist mit
            // der linearen Kurve (spec 2026-07-23 sampler-performance-fixes)
            // weg: scan_rate() ruft kein pow mehr auf. Das Gate bleibt
            // trotzdem, jetzt aus demselben Grund wie beim sampler-only SIZE-
            // Routing weiter unten: SCAN treibt ein sampler-eigenes Stueck
            // Zustand (_scan_rate), das ein Synth-Deck nie liest, und es dort
            // unbedingt zu schreiben waere nur Arbeit ohne Wirkung. Das ist
            // ein Konsistenz-, kein Kosten-Argument mehr.
            //
            // Kein Soft-Takeover hier, und das ist eine Entscheidung, keine
            // Luecke. Der Review vom 2026-07-22 meldete als F-07, dass der
            // erste ENG-Flip den Lesekopf sofort losrasen laesst: MELO traegt
            // im Synth VARIATION, steht im Init-Patch an den Extremen
            // (-0.728 und -1.0), und als SCAN gelesen sind das jetzt -0.97x
            // und -4x Realtime rueckwaerts -- mit dem neuen Maximum naeher an
            // Realtime, nicht weiter davon weg. Das stimmt -- aber es ist
            // genau das Verhalten, das README.md unter "Known limitations"
            // ausdruecklich waehlt: die Knopfposition gilt ueber den
            // Engine-Wechsel hinweg, ohne getrenntes Gedaechtnis und ohne
            // Soft-Takeover, weil die Hardware kein Soft-Takeover hat und
            // beide Seiten dasselbe tun sollen. Eine Sperre einzubauen hiesse,
            // diese Linie zu verlassen -- und sie ueber Patch-Laden hinweg
            // dicht zu bekommen verlangt genau das persistente Gedaechtnis,
            // das dort ausgeschlossen ist. Offen fuer den Autor des
            // Instruments, nicht fuer die Engine.
            //
            // MELODY is one knob with one meaning per engine (spec 2026-08-03
            // vcv-engine-aware-captions): VARY off the Sampler, SCAN on it.
            // Both jobs at once is why SCAN had to be printed permanently
            // beside MELO. Variation parks at 0 (LOOP) here, the same shape
            // as the LANE_SIZE gate below, which parks at 0.5f off the
            // Sampler. The cost is deliberate and recorded in the spec: a
            // Sampler deck no longer renews its phrases on its own, and NEW
            // is the gesture that asks for a fresh pair.
            inst.set_variation(p, samplerPart ? 0.f : mvp(MELODY_A, p));
            if (samplerPart) inst.sampler_scan(p, mvp(MELODY_A, p));

            // GENE SIZE rides the lane base in the sampler, SPREAD in FEED.
            // The else branch is load-bearing -- a base left behind on an
            // engine flip would silently stick.
            //
            // Both re-pointed reads go through mv() too: a conditional face
            // follows its FACE, not its engine wiring (spec §4, last
            // paragraph). SUB is a modulated face on a synth deck, so it stays
            // one on a sampler deck even though the value now lands on a lane
            // base -- same for DTUN on FEED.
            if (samplerPart) {
                inst.set_target_base(p, spky::LANE_SIZE,   mvp(SUB_A, p));
            } else if (feedPart) {
                inst.set_target_base(p, spky::LANE_SIZE,   mvp(DETUNE_A, p));
            } else {
                inst.set_target_base(p, spky::LANE_SIZE,   0.5f);
            }

            // DPTH writes LANE_MOTION's base on every engine, because every
            // engine reads that lane: width (and drift) on SYNTH/WAVE, drift
            // alone on BODY, scatter on the sampler, the feedback amount on
            // the BBD, the FM index on FEED. This host never wrote the base at
            // all until 2026-08-18, so all six had a control whose ends the
            // player could not reach; FEED got the repair first, through a
            // ternary that pinned the other five to Part's compiled-in 0.5.
            // The knob's init default IS that 0.5 (and IS feed_cfg::kDepthBase),
            // so an untouched patch writes exactly what the ternary wrote --
            // the sampler excepted, which halves the base (sampler_config.h).
            inst.set_target_base(p, spky::LANE_MOTION, pp(DEPTH_A, p));

            // Stable pitch in the sampler: the lane still FIRES (that is what
            // keeps STEP triggering alive -- Part::process reads the fire as
            // _mod.lane_fired(LANE_PITCH), part.h:258, while _active gates
            // modulation only, part.cpp:101), it just stops moving the pitch.
            // Sample material and a synth deck can then sit in the same key.
            inst.set_target_active(p, spky::LANE_PITCH, !samplerPart);

            // GRIT is one bipolar knob: sign is the mode, magnitude the mix.
            // The dead zone exists because a 9 mm pot on an ADC cannot hit an
            // exact zero -- without it "off" would be unreachable on hardware.
            const float gritKnob = params[p ? GRIT_B : GRIT_A].getValue();
            inst.set_grit_mode(p, gritKnob < 0.f ? spky::GritMode::Reduce
                                                 : spky::GritMode::Drive);
            const float gritMag = std::fabs(gritKnob);
            inst.set_grit_mix(p, gritMag <= kGritDead ? 0.f
                                 : (gritMag - kGritDead) / (1.f - kGritDead));
            const int steps = (int)std::round(pp(STEPS_A, p));
            inst.set_step(p, steps > 0, steps);

            // SONG walks a curated 14-rung ladder through (Principle, SongMode)
            // (spec 2026-08-09 hw-control-reduction task 3) -- FORM and the NEW
            // pad are gone. songRung[p].tick() debounces the pot (so a value
            // parked on a seam does not re-quantise every tick) AND absorbs a
            // RESTORED rung as a baseline rather than a turn (song_rung_state.hpp)
            // -- see rearm() call sites in dataFromJson()/onReset() below. A
            // rung change re-rolls the phrase exactly as the retired NEW pad
            // used to, and in the sampler additionally punches a fresh grain
            // -- the playhead returns to ORGANIZE and a grain spawns
            // immediately, without which the long end of GENE SIZE is
            // unplayable.
            const float songNorm = pp(SONG_A, p) /
                                   float(spky::kSongLadderCount - 1);
            if (songRung[p].tick(songNorm, spky::kSongLadderCount)) {
                inst.new_phrase(p);          // turn the knob, get a new melody
                // Fires once per rung detent; inherited the retired NEW
                // pad's Sampler punch. Whether every detent should punch, or
                // only some, is still an open by-ear question -- on this
                // plan's listening checklist.
                if (samplerPart) inst.sampler_punch(p);
            }
            const spky::SongRung& r = spky::song_ladder_at(songRung[p].rung);
            inst.set_form(p, r.form);
            inst.set_song(p, r.song);
        }

        // Engine-backed mod depths (spec 2026-08-22 §3a): TIMB/DPTH/FILT write
        // the Part's own _tdepth slots, MIX/FB/SEND the FX row -- active iff
        // the depth is up. Nothing else in this host writes those slots, so
        // this loop is their sole owner and their boot values are exactly what
        // the init snapshot repeats back (1.0 / 0.7 / 0.55 and three zeroes).
        //
        // The engine already multiplies its own master MOD into the texture
        // lanes, so no modMaster factor appears here -- that is the whole
        // reason these six faces do NOT take the host-computed path.
        for (const auto& t : kModLayer) {
            if (t.kind == MODK_TDEPTH) {
                inst.set_target_depth(t.part, t.slot,
                                      params[t.depthId].getValue());
            } else if (t.kind == MODK_FXDEPTH) {
                const float d = params[t.depthId].getValue();
                inst.set_fx_target_depth(t.part, t.slot, d);
                inst.set_fx_target_active(t.part, t.slot, d > 0.f);
            }
        }

        inst.set_morph(mv(MORPH, 0));
        // COUPLE runs both worlds on one axis (kCoupleZoneSplit, declared
        // above). Below the split SYNC is off and couple drives the
        // Kuramoto lock; at or above it SYNC is on and couple sets how
        // tightly the texture lanes follow. Each half sweeps 0..1, so the
        // grid world keeps its full spread -- "on the grid but breathing"
        // is a real state and must stay reachable.
        const float coupleKnob = params[COUPLE].getValue();
        const bool  grid = coupleKnob >= kCoupleZoneSplit;
        inst.set_sync(grid);
        inst.set_couple(grid
            ? (coupleKnob - kCoupleZoneSplit) / (1.f - kCoupleZoneSplit)
            : coupleKnob / kCoupleZoneSplit);
        // The left stop IS the old SETL pad: Center::settle() is drift_target = 0
        // plus a ~1 s glide of EVOLVE and kick, so the button always lived at
        // the end of this axis. Edge-triggered via driftSettled -- a knob
        // parked at the stop must not re-fire the glide on every control
        // tick, and a patch that RESTORES with DRIFT already parked there
        // must not panic on the very first tick either (drift_settle_state.hpp).
        static constexpr float kDriftSettleZone = 0.02f;
        const float driftKnob = params[DRIFT].getValue();
        const bool  driftInZone = driftKnob <= kDriftSettleZone;
        if (driftSettled.tick(driftInZone)) inst.settle();
        inst.set_drift(driftInZone
            ? 0.f
            : (driftKnob - kDriftSettleZone) / (1.f - kDriftSettleZone));
        inst.set_tide(mv(TIDE, 0));
        inst.set_choke(params[CHOKE].getValue());   // continuous -1..+1, engine quantises zones
        // The room's four shape knobs are center targets: mixed from both
        // decks' SIZE lanes (mv() takes the center branch on t.part == 2), so
        // the reverb breathes with whichever deck is actually moving. SEND is
        // NOT here -- it is per-deck and engine-backed (step 6 above).
        inst.set_reverb_size(mv(REV_SIZE, 0));
        inst.set_reverb_decay(mv(REV_DECAY, 0));
        inst.set_reverb_tone(mv(REV_TONE, 0));
        inst.set_reverb_diffusion(mv(REV_DIFF, 0));
        inst.set_reverb_mix(spky::PART_A, params[REV_MIX_A].getValue());
        inst.set_reverb_mix(spky::PART_B, params[REV_MIX_B].getValue());
        // Fixed by ear (spec 2026-08-09 hw-control-reduction task 9): PUSH
        // sat at 0.40 in every patch, and once the limiter rides, DRIVE
        // stops controlling dirt anyway. SMEAR ("smear ... 0.3 sowas") and
        // WOBL/MOD ("wobbel fest auf .1 - .2") are the same kind of decision
        // -- the owner never moved them either. The engine API (set_master_
        // drive/set_reverb_smear/set_reverb_mod) is unchanged so the render
        // host and its scenarios can still drive them.
        inst.set_master_drive(0.40f);
        inst.set_reverb_smear(0.30f);
        inst.set_reverb_mod(0.15f);
        inst.set_scale((int)std::round(params[SCALE].getValue()));

        // Tempo: an external clock (one pulse per beat) overrides the knob.
        float bpm = 40.f + params[TEMPO].getValue() * 200.f;
        if (inputs[CLOCK].isConnected() && clkSamples > 1.f && curSr > 0.f) {
            float measured = 60.f * curSr / clkSamples;
            if (measured >= 20.f && measured <= 400.f) bpm = measured;
        }
        inst.set_tempo_bpm(bpm);
        inst.set_pace(params[PACE].getValue());
    }

    void process(const ProcessArgs& args) override {
        if (args.sampleRate != curSr) reinit(args.sampleRate);

        // external clock edge -> remember the period, and phase-align the transport
        if (inputs[CLOCK].isConnected()) {
            clkSamples += 1.f;
            if (clockTrig.process(inputs[CLOCK].getVoltage(), 0.1f, 1.f)) {
                clkSamples = 0.f;
                inst.clock_pulse();
            }
        }

        if ((inputs[RESET].isConnected() &&
             resetTrig.process(inputs[RESET].getVoltage(), 0.1f, 1.f)) ||
            resyncReq.exchange(false))
            inst.reset_transport();

        if (ctrlDiv.process()) pushParams();

        const bool haveIn = inputs[IN_L].isConnected() || inputs[IN_R].isConnected();
        float inl = inputs[IN_L].getVoltage() * 0.2f;   // ±5V -> ±1
        float inr = inputs[IN_R].isConnected() ? inputs[IN_R].getVoltage() * 0.2f : inl;
        float outl = 0.f, outr = 0.f;
        inst.process(haveIn ? &inl : nullptr, haveIn ? &inr : nullptr,
                     &outl, &outr, 1);

        outputs[OUT_L].setVoltage(clamp(outl, -1.f, 1.f) * 5.f);
        outputs[OUT_R].setVoltage(clamp(outr, -1.f, 1.f) * 5.f);

        // per-part modulation taps -> the rest of the rack
        outputs[PITCH_A].setVoltage(clamp(inst.pitch_cv(0), -1.f, 1.f) * 5.f);
        outputs[PITCH_B].setVoltage(clamp(inst.pitch_cv(1), -1.f, 1.f) * 5.f);
        outputs[GATE_A].setVoltage(inst.gate(0) ? 10.f : 0.f);
        outputs[GATE_B].setVoltage(inst.gate(1) ? 10.f : 0.f);

        // One law, one call, 21 lamps -- host/vcv/src/led_law.hpp. Quantised
        // to kLedSteps AND run through the same perceptual gamma even here:
        // that is what the mux scan gives the hardware for free, and a Rack
        // module that breathes more finely, or more linearly, than the panel
        // ever can is validating itself against the wrong instrument.
        if (ledDiv.process()) {
            const float dt = ledDiv.getDivision() * args.sampleTime;
            spkyled::fill(inst, ledPanel, dt, kLedSteps,
                          params[MODBTN].getValue() > 0.5f, ledDuty);
            for (int i = 0; i < NUM_LIGHTS; ++i)
                lights[i].setBrightness(float(ledDuty[i]) / float(kLedSteps - 1));
        }
    }

    void onReset() override {
        // Rack Initialize restores the complete init patch, including the
        // parameter defaults and an empty sampler ready for the bundled
        // factory WAV. A mid-session Clear still stays cleared.
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            smp[p] = SamplerPartState{};
            inst.sampler_clear(p);
            factoryTried[p] = false;
            // Rack resets params (including SONG_A/B) to their default
            // BEFORE calling onReset(). Without this re-arm, an
            // already-ticking module's stale pre-Initialize rung would make
            // the reset-to-default SONG value look like a giant turn of the
            // knob and fire a re-roll on the very next control tick
            // (song_rung_state.hpp).
            songRung[p].rearm();
        }
        // Rack resets DRIFT to its default BEFORE calling onReset() too --
        // without this re-arm, an already-ticking module whose DRIFT was off
        // the stop would see the reset-to-default value and, if that default
        // sits in the settle zone, treat it as a genuine entry and fire
        // settle() on the very next control tick (drift_settle_state.hpp).
        driftSettled.rearm();
        reinit(curSr > 0.f ? curSr : 48000.f);
    }

    // --- persistence -----------------------------------------------------
    // FORM and SONG themselves are ordinary Rack parameters. The marker lets
    // dataFromJson distinguish their new meanings from the stable numeric
    // slots used by older patches.
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "formSongVersion", json_integer(1));
        json_object_set_new(root, "linkVersion", json_integer(1));

        json_t* parts = json_array();
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            json_t* o = json_object();
            json_object_set_new(o, "path", json_string(smp[p].path.c_str()));
            json_object_set_new(o, "tape", json_integer(smp[p].tapeIdx));
            json_object_set_new(o, "reverse", json_boolean(smp[p].reverse));
            json_object_set_new(o, "feedback", json_real(smp[p].feedback));
            json_object_set_new(o, "testTone", json_boolean(smp[p].testTone));
            json_object_set_new(o, "factory", json_boolean(smp[p].factoryLoaded));
            // "autoload already consumed / user cleared this" -- without
            // persisting it, Clear -> Save -> reopen resets factoryTried to
            // its construction-time false, and the factory drone refills the
            // deliberately-cleared part on the first control tick after
            // patch open with no user gesture at all (I-1b).
            json_object_set_new(o, "factoryTried", json_boolean(factoryTried[p]));
            // BODY's excitation bus (design spec §6) -- patch state, not a
            // performance control. A missing key on load leaves the
            // constructor default (tape on, deck/audio off) in place, so old
            // patches load with tape-only excitation, matching what a
            // pre-Task-10 patch effectively had.
            json_object_set_new(o, "exciteTape", json_boolean(smp[p].exciteTape));
            json_object_set_new(o, "exciteOtherDeck", json_boolean(smp[p].exciteOtherDeck));
            json_object_set_new(o, "exciteAudioIn", json_boolean(smp[p].exciteAudioIn));
            json_array_append_new(parts, o);
        }
        json_object_set_new(root, "sampler", parts);
        return root;
    }

    // LINK migration must straddle Rack's base loader: paramsFromJson clamps
    // the old bipolar values to this build's 0..1 range before dataFromJson.
    // FORM/SONG stays in dataFromJson; the migrations touch disjoint ids.
    void fromJson(json_t* module_root) override {
        json_t* data = json_object_get(module_root, "data");
        json_t* link_version = data ? json_object_get(data, "linkVersion") : nullptr;
        const bool modern_link = is_modern_link_version(
            link_version && json_is_integer(link_version),
            link_version && json_is_integer(link_version)
                ? json_integer_value(link_version) : 0);

        bool have_raw[spky::PART_COUNT] = {};
        float migrated[spky::PART_COUNT] = {};
        if (!modern_link) {
            json_t* raw_params = json_object_get(module_root, "params");
            size_t i;
            json_t* raw_param;
            json_array_foreach(raw_params, i, raw_param) {
                json_t* id_json = json_object_get(raw_param, "id");
                json_t* value_json = json_object_get(raw_param, "value");
                if (!json_is_integer(id_json) || !json_is_number(value_json)) continue;
                const int id = (int)json_integer_value(id_json);
                for (int p = 0; p < spky::PART_COUNT; ++p) {
                    if (id != (p ? LINK_B : LINK_A)) continue;
                    have_raw[p] = true;
                    migrated[p] = migrate_legacy_link((float)json_number_value(value_json));
                }
            }
        }

        Module::fromJson(module_root);
        if (!modern_link)
            for (int p = 0; p < spky::PART_COUNT; ++p)
                if (have_raw[p])
                    params[p ? LINK_B : LINK_A].setValue(migrated[p]);
    }

    void dataFromJson(json_t* root) override {
        if (!root) return;
        json_t* version = json_object_get(root, "formSongVersion");
        const bool modern = is_modern_form_song_version(
            version && json_is_integer(version),
            version && json_is_integer(version) ? json_integer_value(version) : 0);
        if (!modern) {
            json_t* bases = json_object_get(root, "lastBasis");
            json_t* principles = json_object_get(root, "principle");
            for (int p = 0; p < spky::PART_COUNT; ++p) {
                json_t* basis = json_is_array(bases) ? json_array_get(bases, p) : nullptr;
                json_t* principle =
                    json_is_array(principles) ? json_array_get(principles, p) : nullptr;
                const FormSongMigration migrated = migrate_legacy_form_song(
                    bases != nullptr,
                    basis && json_is_integer(basis),
                    basis && json_is_integer(basis) ? json_integer_value(basis) : 0,
                    principle && json_is_integer(principle),
                    principle && json_is_integer(principle)
                        ? json_integer_value(principle) : 0);
                // The old FORM ParamIds no longer exist -- SONG absorbed them
                // (spec 2026-08-09 hw-control-reduction task 3). Land on the first
                // ladder rung that carries the migrated Principle; every
                // Principle appears in the ladder, so this always finds one.
                int rung = 0;
                for (int i = 0; i < spky::kSongLadderCount; ++i) {
                    if (spky::song_ladder_at(i).form == migrated.form) {
                        rung = i;
                        break;
                    }
                }
                params[p ? SONG_B : SONG_A].setValue((float)rung);
            }
        }

        json_t* parts = json_object_get(root, "sampler");
        if (!parts) return;
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            json_t* o = json_array_get(parts, p);
            if (!o) continue;
            if (json_t* v = json_object_get(o, "path"))
                smp[p].path = json_string_value(v) ? json_string_value(v) : "";
            if (json_t* v = json_object_get(o, "tape"))     smp[p].tapeIdx = (int)json_integer_value(v);
            if (json_t* v = json_object_get(o, "reverse"))  smp[p].reverse = json_boolean_value(v);
            if (json_t* v = json_object_get(o, "feedback")) smp[p].feedback = (float)json_real_value(v);
            if (json_t* v = json_object_get(o, "testTone")) smp[p].testTone = json_boolean_value(v);
            if (json_t* v = json_object_get(o, "factory"))  smp[p].factoryLoaded = json_boolean_value(v);
            // Explicit default (not the `if (v)` pattern used above): a
            // legacy patch with no such key must still zero this out, not
            // silently inherit whatever the live module's factoryTried
            // happened to be before this load. Persisted intent otherwise
            // always wins -- see restoreSamplerContent()'s terminal branch,
            // which no longer overwrites this.
            json_t* v = json_object_get(o, "factoryTried");
            factoryTried[p] = v ? json_boolean_value(v) : false;
            // Read-guarded like every other field here: a patch saved before
            // this task has none of these keys, so the struct's own
            // defaults (tape on, deck/audio off) stand.
            if (json_t* e = json_object_get(o, "exciteTape"))
                smp[p].exciteTape = json_boolean_value(e);
            if (json_t* e = json_object_get(o, "exciteOtherDeck"))
                smp[p].exciteOtherDeck = json_boolean_value(e);
            if (json_t* e = json_object_get(o, "exciteAudioIn"))
                smp[p].exciteAudioIn = json_boolean_value(e);
        }
        // On a whole-patch open, dataFromJson runs before the module is in
        // the engine (curSr == 0.f, reinit() hasn't sized the sampler
        // buffers), so content restore is deferred to onAdd(). But Rack also
        // calls dataFromJson on an already-live module -- right-click preset
        // load and module paste go through ModuleWidget::loadAction/
        // pasteJsonAction, not Engine::addModule_NoLock, so onAdd() never
        // fires again for those. curSr > 0.f (Task 7) is the "already
        // initialised" test: restore immediately in that case instead of
        // leaving the buffer stale forever. pendingRestore stays reserved
        // for the fresh-add path -- exactly one of the two restores the
        // content for a given JSON load, never both.
        if (curSr > 0.f) {
            pendingRestore = false;
            restoreSamplerContent();
            // This is a restore into an ALREADY-LIVE module (right-click Load
            // preset / module paste), not a fresh add -- pushParams() has
            // already run at least once, so bbdEdge[p] is already seeded from
            // BEFORE this restore. Re-arm both parts (see bbd_edge_state.hpp)
            // so the very next control tick treats whatever ENG this JSON
            // just set as a fresh baseline, not a transition -- otherwise a
            // preset saved on BBD, loaded onto a module currently on a
            // different engine, would fire the "entering BBD" edge and
            // clobber that preset's own saved FLUX/exciteOtherDeck. The
            // fresh-add path (curSr == 0.f, the else branch below) needs no
            // such re-arm: no tick has run yet, so bbdEdge[p] is still at its
            // construction-time unseeded state and the ordinary first-tick
            // baseline in tick() already applies. songRung[p] needs the same
            // re-arm for the same reason (song_rung_state.hpp) -- otherwise a
            // preset saved on a rung other than the module's current one
            // would look like a giant turn of the SONG knob and fire a
            // re-roll the instant the module ticks again. driftSettled needs
            // the identical re-arm for the identical reason (SHARED, not
            // per-part, so one call outside the loop): a preset saved with
            // DRIFT off the stop, loaded onto a module currently parked at
            // the stop, must not have the restore itself read as a genuine
            // entry and fire settle() on the very next tick
            // (drift_settle_state.hpp).
            for (int p = 0; p < spky::PART_COUNT; ++p) {
                bbdEdge[p].rearm();
                songRung[p].rearm();
            }
            driftSettled.rearm();
        } else {
            pendingRestore = true;
        }
    }

    std::string storedWavPath(int p) {
        return system::join(createPatchStorageDirectory(),
                            p ? "sample_b.wav" : "sample_a.wav");
    }

    // Same target file as storedWavPath(), but never creates the patch
    // storage directory. For check-and-delete callers: creating an empty
    // modules/<id>/ directory on every save (via createPatchStorageDirectory)
    // even for instances that never touched the sampler is pure litter.
    std::string storedWavPathNoCreate(int p) {
        return system::join(getPatchStorageDirectory(),
                            p ? "sample_b.wav" : "sample_a.wav");
    }

    void onSave(const SaveEvent& e) override {
        Module::onSave(e);
        // The recorded texture has no file of its own -- without this it dies
        // with the session. Only content that did not come from a file or the
        // factory WAV needs writing: those two reload from their source.
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            // Any part this save will NOT write its own stored WAV for --
            // it now has a file path, is factory-loaded, or has nothing
            // recorded -- must not be left holding a stale one from an
            // earlier save. Rack garbage-collects patch storage per MODULE,
            // not per file, so e.g. record -> save (writes a stored WAV) ->
            // load a WAV over it -> save again would otherwise leave that
            // first recording (up to 16 MB) sitting in patch storage
            // forever, unreachable but real disk weight inside the user's
            // patch (I-1a's empty-part case, generalised to every reason a
            // part might skip writing). Hoisted above the old skip guard so
            // it fires for the path/factory cases too, not just the empty
            // one. Deliberately structured so this can never race the write
            // below: willWrite and the delete are mutually exclusive by
            // construction, so a part about to be (re)written this save
            // never has its about-to-be-overwritten file deleted out from
            // under it first.
            const bool willWrite = smp[p].path.empty() && !smp[p].factoryLoaded
                                  && inst.sampler_rec_size(p) != 0;
            if (!willWrite) {
                // Uses the no-create variant: a pure check-and-delete must
                // not mkdir a patch storage directory that would otherwise
                // never exist for an instance that never touched the
                // sampler.
                const std::string stored = storedWavPathNoCreate(p);
                if (system::isFile(stored) && !system::remove(stored)) {
                    // Windows in particular can leave a locked or read-only
                    // file undeletable; failing silently here would let the
                    // stale WAV survive and restoreSamplerContent() reload it
                    // right back on reopen -- the original I-1a bug, with no
                    // diagnostic to explain why.
                    WARN("Fireflow: could not remove stale sampler %d WAV %s",
                         p, stored.c_str());
                }
                continue;
            }
            std::string err;
            if (!spkyvcv::save_wav_from(inst, p, storedWavPath(p), curSr, err))
                WARN("Fireflow: could not store sampler %d: %s", p, err.c_str());
        }
    }

    // Shared by onAdd() (fresh patch-open add) and dataFromJson() (preset
    // load / paste on an already-live module) -- exactly one of those two
    // call sites invokes this per JSON load, see the comments at each.
    void restoreSamplerContent() {
        // Engine::addModule_NoLock dispatches AddEvent *before*
        // SampleRateChangeEvent, so for a freshly-added instance curSr is
        // still its construction-time 0 and inst/samplerMem have never been
        // sized (reinit() has not run yet) -- loading here would write into
        // an uninitialised engine. reinit() is safe to call twice in a row
        // (it snapshots any already-loaded content out and back in around
        // the resize/init), so forcing it now does not race the
        // SampleRateChangeEvent that follows this call. On the already-live
        // path (called from dataFromJson) curSr is already > 0.f, so this
        // is a no-op there.
        // Captured BEFORE the reinit() above can change curSr: it is the
        // "this is the fresh-add path" test, not merely "was curSr 0 a
        // moment ago" -- see the stored-WAV comment below.
        const bool freshAdd = (curSr <= 0.f);
        if (freshAdd) reinit(APP->engine->getSampleRate());
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            std::string err;
            if (!smp[p].path.empty()) {
                if (spkyvcv::load_wav_into(inst, p, smp[p].path, curSr, err))
                    continue;
                WARN("Fireflow: sampler %d could not reload %s: %s",
                     p, smp[p].path.c_str(), err.c_str());
                // Deliberately falls through instead of continue-ing: the
                // file may have moved, been renamed, or the patch may have
                // been opened on another machine -- the ordinary case for a
                // user-loaded WAV, not a rare one. Without this the live
                // buffer keeps whatever audio the module was already playing
                // while path/menu/JSON all describe a load that never
                // actually happened (the same I-2 symptom, surviving in this
                // error path). Falls into the stored-WAV check below (fresh-
                // add only, harmless no-op on the live path) and then the
                // terminal clear if that also finds nothing.
            }
            // getPatchStorageDirectory() is scoped to the CURRENT patch and
            // module id, not to the preset being loaded. On a live preset
            // load or module paste (freshAdd == false) it points at THIS
            // patch's own autosave directory, so reading a stored WAV there
            // would pull content that belongs to neither the preset nor the
            // user's intent into a part the preset describes as empty.
            // Restrict the stored-WAV branch to the fresh-add path, where
            // the directory really is the patch being opened (I-2 note).
            if (freshAdd) {
                const std::string stored = system::join(getPatchStorageDirectory(),
                                                        p ? "sample_b.wav" : "sample_a.wav");
                if (system::isFile(stored)) {
                    if (spkyvcv::load_wav_into(inst, p, stored, curSr, err))
                        continue;
                    WARN("Fireflow: sampler %d could not reload stored %s: %s",
                         p, stored.c_str(), err.c_str());
                }
            }
            // Neither a file path nor (fresh-add only) a stored WAV exists
            // for this part. This function must be total: on the live path
            // (preset load / paste) the buffer may already hold whatever the
            // module was playing before, and nothing else will clear it --
            // the menu, the JSON and the REC LED would all describe the new
            // preset while the old audio kept playing (I-2). Safe on the
            // fresh-add path too: the buffer there is already zeroed by the
            // reinit() above.
            //
            // Deliberately does NOT touch factoryTried[p] here. dataFromJson
            // just restored it from this same JSON (defaulting to false for
            // legacy patches with no such key) -- overwriting it back to
            // false would discard exactly the intent a deliberate Clear ->
            // Save persisted, and the factory drone would refill a part the
            // user emptied on purpose, on the very first control tick after
            // reopen with no user gesture at all. onReset() (Rack
            // Initialize) remains the only thing that un-consumes this guard.
            inst.sampler_clear(p);
        }
    }

    // Decode res/factory.wav at its native rate, at most once per module
    // instance. Must only ever be called from onAdd() (main thread): onAdd()
    // completes as part of the synchronous addModule() call, before the
    // module widget exists for the user to touch and before process() ever
    // runs for this instance -- the same happens-before guarantee
    // restoreSamplerContent()'s onAdd() call already relies on. reinit()
    // deliberately does NOT read disk itself (only resamples this cache), so
    // that process()'s reactive `sampleRate != curSr` fallback -- which does
    // run on the audio thread -- can never trigger a file read.
    void loadFactoryNative() {
        if (factoryNativeTried) return;
        factoryNativeTried = true;
        std::string err;
        const std::string fp = asset::plugin(pluginInstance, "res/factory.wav");
        if (!spky::read_wav(fp, factoryNative, err)) {
            WARN("Fireflow: factory sample unavailable: %s", err.c_str());
            factoryNative.l.clear();
            factoryNative.r.clear();
        }
    }

    void onAdd(const AddEvent& e) override {
        Module::onAdd(e);
        // Must run before restoreSamplerContent() (which may force an
        // immediate reinit() below): reinit() only resamples factoryNative,
        // it doesn't read it, so the cache has to already be populated (or
        // confirmed unreadable) by the time any reinit() call happens.
        loadFactoryNative();
        if (!pendingRestore) return;
        pendingRestore = false;
        restoreSamplerContent();
    }
};

// --- live LED ring ------------------------------------------------------------
// One per part. Draws 32 dots in the light layer (glows additively over Rack's
// darkened room). Each of the five lanes lights a moving dot at its position;
// a lane that just fired flashes. Idle -> dark (no fake motion).
struct SpkyRing : Widget {
    Fireflow* module = nullptr;
    int part = 0;

    SpkyRing(Fireflow* m, int p) : module(m), part(p) {
        float d = mm2px(2.f * (kRingR + kRingDotR + 0.5f));
        box.size = Vec(d, d);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        const float TWO_PI = 6.2831853f;
        Vec c = box.size.div(2.f);
        float R  = mm2px(kRingR);
        float dr = mm2px(kRingDotR);

        float bright[kRingDots] = {};
        if (module) {
            for (int s = 0; s < spky::LANE_COUNT; ++s) {
                float v = clamp(module->inst.lane_output(part, s), -1.f, 1.f);
                float posf = (v * 0.5f + 0.5f) * kRingDots;      // 0..32
                int i0 = ((int)std::floor(posf)) % kRingDots;
                if (i0 < 0) i0 += kRingDots;
                int i1 = (i0 + 1) % kRingDots;
                float frac = posf - std::floor(posf);
                float boost = module->inst.lane_fired(part, s) ? 1.f : 0.6f;
                bright[i0] = std::max(bright[i0], (1.f - frac) * boost);
                bright[i1] = std::max(bright[i1], frac * boost);
            }
        }
        // per-part glow colour from the generator (A solder green, B copper)
        const unsigned gc = kColGlow[part];
        const float cr = ((gc >> 16) & 0xFF) / 255.f;
        const float cg = ((gc >> 8)  & 0xFF) / 255.f;
        const float cb = ( gc        & 0xFF) / 255.f;
        for (int i = 0; i < kRingDots; ++i) {
            float b = bright[i];
            if (b <= 0.02f) continue;
            float a = TWO_PI * i / kRingDots;
            Vec p = c.plus(Vec(std::sin(a), -std::cos(a)).mult(R));
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, p.x, p.y, dr * 2.6f);
            nvgFillColor(args.vg, nvgRGBAf(cr, cg, cb, 0.25f * b));
            nvgFill(args.vg);
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, p.x, p.y, dr);
            nvgFillColor(args.vg, nvgRGBAf(cr, cg, cb, b));
            nvgFill(args.vg);
        }

        // --- sampler read position (spec 2026-07-21 morphagene-controls) ---
        // Drawn separately rather than through bright[]: that array carries
        // brightness only, and every dot in it is kColGlow[part], so a head
        // folded into it would be indistinguishable from a lane. Warm white
        // at full brightness reads as "this one is different" without a
        // second palette. On a synth part, or with nothing recorded, nothing
        // is drawn -- an idle ring stays dark, as it does today.
        //
        // sampler_last_spawn_pos(), NOT sampler_scan_pos(): scan_pos() is only
        // the tape-head OFFSET accumulated by SCAN, not where grains actually
        // read from -- that is clamp(SOURCE)*span + scan_pos + jitter, folded
        // (SamplerEngine::_spawn_one). With ORGANIZE parked mid-buffer,
        // scan_pos() sits at 0 while the cloud reads from the middle, which
        // would show the dot in the wrong place. last_spawn_pos() is the
        // actual centre of the most recently spawned grain, which is what
        // the spec's "Der Kopf wird sichtbar" asks for.
        if (module && module->inst.engine_id(part) == spky::ENGINE_SAMPLER) {
            const size_t content = module->inst.sampler_rec_size(part);
            if (content > 0) {
                const float frac =
                    module->inst.sampler_last_spawn_pos(part) / float(content);
                const float a = TWO_PI * frac;
                Vec hp = c.plus(Vec(std::sin(a), -std::cos(a)).mult(R));
                nvgBeginPath(args.vg);
                nvgCircle(args.vg, hp.x, hp.y, dr * 3.0f);
                nvgFillColor(args.vg, nvgRGBAf(1.f, 0.95f, 0.85f, 0.20f));
                nvgFill(args.vg);
                nvgBeginPath(args.vg);
                nvgCircle(args.vg, hp.x, hp.y, dr * 1.15f);
                nvgFillColor(args.vg, nvgRGBAf(1.f, 0.95f, 0.85f, 0.95f));
                nvgFill(args.vg);
            }
        }
    }
};

// --- panel text ---------------------------------------------------------------
// Rack's SVG loader (NanoSVG) ignores <text>, so the faceplate ships with none
// of its lettering visible. Every caption is drawn here with nvgText, straight
// out of the generated tables: position, anchor, size and colour all come from
// res/gen_panel.py (PanelCtl::lbl/anchor/lblSize/lblRgb), so the SVG preview and
// Rack can never drift apart. Font is a stock Rack asset, present in every
// v2 install -- note it has no bold cut, so the SVG's bold legends render
// regular here. That is accepted.
static int roundedEngineState(Fireflow* module, int engineId) {
    return module
        ? static_cast<int>(std::round(module->params[engineId].getValue()))
        : 0;
}

static bool isBbdSelected(Fireflow* module, int engineId) {
    return roundedEngineState(module, engineId) == 4;
}

// One definition of "this deck is running the Sampler", shared by the REC
// pad's visibility rule (ctlVisible below) and the REC LED's (SamplerOnly
// below). Duplicating the comparison would let the two drift, and this
// file's guards match on source text, so a second copy of an expression can
// also silently redirect a mutation test elsewhere.
static bool samplerDeck(Fireflow* m, int engineId) {
    return roundedEngineState(m, engineId) == 1;
}

// A control is visible only where it does something. Two share the upper-left
// VOICE coordinate -- ATTACK on four engines, STAGES on the BBD -- so only one
// may ever draw there. REC has a coordinate of its own but no job outside the
// Sampler: pushParams gates it on the exact Sampler engine id, and its LED is
// already dark elsewhere, so the pad was the last thing still claiming
// otherwise.
static bool ctlVisible(Fireflow* m, int id) {
    switch (id) {
        case ATTACK_A: return !isBbdSelected(m, ENGINE_A);
        case ATTACK_B: return !isBbdSelected(m, ENGINE_B);
        case STAGES_A: return  isBbdSelected(m, ENGINE_A);
        case STAGES_B: return  isBbdSelected(m, ENGINE_B);
        case REC_A: return samplerDeck(m, ENGINE_A);
        case REC_B: return samplerDeck(m, ENGINE_B);
        default:       return true;
    }
}

// Widget half of ctlVisible. The caption loop and the widget must answer the
// same question from the same place, or a control can be hidden while its
// word is still drawn.
template <typename W>
struct SlotVisible : W {
    Fireflow* fireflow = nullptr;
    int ctlId = 0;

    void step() override {
        this->setVisible(ctlVisible(fireflow, ctlId));
        W::step();
    }
};

// The LED half of the Sampler-only rule. It takes the deck's ENGINE PARAM id,
// not the light's own id: a LightId is a different enum from a ParamId and the
// two spaces must never meet (REC_A_L == 2 == DENSITY_A). See the captions
// note further below (PanelText's `dynamic` flag) for what happened the last
// time an id crossed enum spaces here.
template <typename W>
struct SamplerOnly : W {
    Fireflow* fireflow = nullptr;
    int engineId = ENGINE_A;

    void step() override {
        this->setVisible(samplerDeck(fireflow, engineId));
        W::step();
    }
};

struct PanelText : Widget {
    Fireflow* module;
    explicit PanelText(Fireflow* m) : module(m) {}

    void draw(const DrawArgs& args) override {
        std::shared_ptr<Font> font =
            APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);

        auto col = [](unsigned rgb) {
            return nvgRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
        };
        auto text = [&](float xmm, float ymm, float szmm, NVGcolor c, const char* s) {
            nvgFontSize(args.vg, mm2px(szmm));
            nvgFillColor(args.vg, c);
            Vec p = mm2px(Vec(xmm, ymm));
            nvgText(args.vg, p.x, p.y, s, NULL);
        };
        auto alignOf = [](unsigned char a) {
            return a == 1 ? NVG_ALIGN_LEFT : a == 2 ? NVG_ALIGN_RIGHT
                                                    : NVG_ALIGN_CENTER;
        };
        // Every caption that depends on state resolves here, out of the
        // generated table. Position, anchor, size and colour still come from
        // the same PanelCtl, so a dynamic word can never land anywhere its
        // resting word would not have.
        auto caption = [&](const PanelCtl& c) -> const char* {
            for (const auto& d : kDynCaptions) {
                if (d.id != c.id) continue;
                if (!module) return d.words[0];   // browser preview = Synth
                int v = (int)std::round(module->params[d.driverId].getValue());
                if (v < 0) v = 0;
                if (v >= d.count) v = d.count - 1;
                return d.words[v];
            }
            return c.label;
        };
        // PanelCtl::id is a different enum per table -- a ParamId in
        // kParamCtls, an InputId in kInputCtls, an OutputId in kOutputCtls --
        // and all three start counting at 0. kDynCaptions/ctlVisible only
        // know about ParamIds, so resolving them against an Input/OutputId
        // is not "the same id, different table": it is a coincidence that a
        // jack id happens to equal some param's id, and the caption for that
        // unrelated param would draw on the jack instead. The `dynamic` flag
        // keeps that lookup inside the param id-space it was built for; do
        // not "simplify" it away by calling caption()/ctlVisible() for every
        // table unconditionally.
        auto captions = [&](const PanelCtl* t, size_t n, bool dynamic) {
            for (size_t i = 0; i < n; ++i) {
                if (!t[i].label[0]) continue;
                if (dynamic && !ctlVisible(module, t[i].id)) continue;
                nvgTextAlign(args.vg, alignOf(t[i].anchor) | NVG_ALIGN_BASELINE);
                text(t[i].lbl.x, t[i].lbl.y, t[i].lblSize, col(t[i].lblRgb),
                     dynamic ? caption(t[i]) : t[i].label);
            }
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
        };
        captions(kParamCtls,  sizeof(kParamCtls)  / sizeof(kParamCtls[0]),  true);
        captions(kInputCtls,  sizeof(kInputCtls)  / sizeof(kInputCtls[0]),  false);
        captions(kOutputCtls, sizeof(kOutputCtls) / sizeof(kOutputCtls[0]), false);

        // section titles + brand -- the shared TEXTS table from the generator,
        // so runtime lettering matches the SVG preview one-to-one. PanelTxt
        // carries the same (x, y, anchor, size, colour) shape as PanelCtl.lbl
        // for that reason alone: every row here sits middle-anchored. What
        // actually needs a non-middle anchor is PanelCtl's own lettering --
        // the radial orbit captions and the white-on-well jack labels --
        // not anything drawn from this table.
        for (const auto& t : kPanelTexts) {
            nvgTextLetterSpacing(args.vg, mm2px(t.spacing));
            nvgTextAlign(args.vg, alignOf(t.anchor) | NVG_ALIGN_BASELINE);
            text(t.mm.x, t.mm.y, t.size, col(t.rgb), t.str);
        }
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
        nvgTextLetterSpacing(args.vg, 0.f);
    }
};


// --- sampler edit-layer menu ---------------------------------------------------
// Overdub feedback is a continuous value with no panel home -- the menu
// slider is its only surface. 0.95 (~-3 dB) is the engine default. The knob
// is normalised 0..1; the engine maps it to -60..0 dB internally
// (SampleBuffer::set_feedback), so the display is a percentage of the knob,
// not a dB figure.
struct FeedbackQuantity : Quantity {
    float* v;
    explicit FeedbackQuantity(float* p) : v(p) {}
    void  setValue(float x) override { *v = clamp(x, 0.f, 1.f); }
    float getValue() override        { return *v; }
    float getMinValue() override     { return 0.f; }
    float getMaxValue() override     { return 1.f; }
    float getDefaultValue() override { return 0.95f; }
    std::string getLabel() override  { return "Overdub feedback"; }
    std::string getDisplayValueString() override {
        return string::f("%.0f%%", getValue() * 100.f);
    }
};

struct FeedbackSlider : ui::Slider {
    explicit FeedbackSlider(float* v) {
        box.size.x = 180.f;
        quantity = new FeedbackQuantity(v);
    }
    ~FeedbackSlider() override { delete quantity; }
};

// --- shared context menu --------------------------------------------------
// Lifted out of FireflowWidget::appendContextMenu verbatim (refactor only,
// no behaviour change) so the HW draft widget can share it: both widgets
// wrap the same Fireflow module, so the menu content is identical, just the
// panel around it differs.
static void appendFireflowMenu(Menu* menu, Fireflow* m) {
    menu->addChild(new MenuSeparator);
    // Same gesture as a pulse into RST: zero the downbeat and restart the
    // loops at the bar start (a live STEPS turn leaves them free-running).
    menu->addChild(createMenuItem("Resync loops to bar", "",
                                  [m]() { m->resyncReq = true; }));

    // DRIVE_A/B retired (spec 2026-08-09 hw-control-reduction task 9): the
    // menu-only slider never reached the engine (its BBD drive target is a
    // mod lane, not a panel/menu control -- see bbd_engine.cpp's
    // set_targets()), so it leaves with no replacement. DETUNE used to have
    // the same widgetless shape, but task 10 moved it back onto the panel as
    // a real performance control -- its slider lives there now
    // (kParamCtls/DetuneQuantity), not in this menu.

    if (isBbdSelected(m, ENGINE_A)) {
        menu->addChild(createSubmenuItem("BBD A — Freeze Attack", "", [m](Menu* sub) {
            sub->addChild(new ParamMenuSlider(m->getParamQuantity(ATTACK_A)));
        }));
    }
    if (isBbdSelected(m, ENGINE_B)) {
        menu->addChild(createSubmenuItem("BBD B — Freeze Attack", "", [m](Menu* sub) {
            sub->addChild(new ParamMenuSlider(m->getParamQuantity(ATTACK_B)));
        }));
    }

    // BODY's excitation bus (design spec §6) plus, since the
    // cross-deck-audio-bus branch (spec 2026-07-31 bbd-part-engine §4.4),
    // the audio-rate cross-deck tap: patch state, not a performance
    // control -- there is no panel knob for any of this, so it lives
    // here, same shape as Detune A/B above. Defaults: tape on, other
    // deck / audio in off.
    //
    // "Excite: FLUX tape" and "Excite: audio in" are still exactly what
    // they say: BODY-only, inert everywhere else. "Excite: other deck"
    // is NOT -- exciteOtherDeck feeds Part::_src_deck, which now gates
    // THREE paths: BODY's control-rate excitation bus (unchanged), the
    // audio-rate cross-deck bus SAMPLER consumes (process_in()), and now
    // the same bus BBD consumes to feed its delay line (spec 5.12's
    // silence-trap fix -- a BBD deck with no external cabling still has
    // something to echo). So on a SAMPLER or BBD deck this flag went
    // from inert to live: it audibly routes (and, for SAMPLER, records)
    // the neighbouring deck, and -- because the bound is
    // fast_tanh(engine_in + neighbour) rather than a separate path -- it
    // also puts the deck's own external audio-in through fast_tanh for
    // the first time even when the neighbour is silent (tanh(1) ~ 0.76),
    // measurably attenuating audio-in monitoring/recording on that deck.
    // A patch saved with this flag on for a SAMPLER or BBD deck
    // therefore behaves differently after upgrading to this branch.
    // Neither consequence is a bug -- see the spec sections above -- but
    // the label has to say so, because the old name promised BODY-only
    // and nothing here enforces that anymore.
    for (int p = 0; p < spky::PART_COUNT; ++p) {
        const std::string name = p ? "Excite B" : "Excite A";
        menu->addChild(createSubmenuItem(name, "", [m, p](Menu* sub) {
            sub->addChild(createBoolPtrMenuItem("Excite: FLUX tape", "",
                                                &m->smp[p].exciteTape));
            sub->addChild(createBoolPtrMenuItem(
                "Route: other deck (BODY excite, SAMPLER feed+rec, "
                "BBD feed)", "",
                &m->smp[p].exciteOtherDeck));
            sub->addChild(createBoolPtrMenuItem("Excite: audio in", "",
                                                &m->smp[p].exciteAudioIn));
        }));
    }

    menu->addChild(new MenuSeparator);
    for (int p = 0; p < spky::PART_COUNT; ++p) {
        const std::string name = p ? "Sampler B" : "Sampler A";
        menu->addChild(createSubmenuItem(name, "", [m, p](Menu* sub) {
            sub->addChild(createMenuItem("Load sample...", "", [m, p]() {
                char* path = osdialog_file(OSDIALOG_OPEN, nullptr, nullptr, nullptr);
                if (!path) return;
                std::string err;
                if (spkyvcv::load_wav_into(m->inst, p, path, m->curSr, err)) {
                    m->smp[p].path = path;
                    m->smp[p].factoryLoaded = false;
                } else {
                    osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, err.c_str());
                }
                std::free(path);
            }));
            sub->addChild(createMenuItem("Save sample...", "", [m, p]() {
                char* path = osdialog_file(OSDIALOG_SAVE, nullptr, "sample.wav", nullptr);
                if (!path) return;
                std::string err;
                if (!spkyvcv::save_wav_from(m->inst, p, path, m->curSr, err))
                    osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, err.c_str());
                std::free(path);
            }));
            sub->addChild(createMenuItem("Clear sample", "", [m, p]() {
                m->inst.sampler_clear(p);
                m->smp[p].path.clear();
                m->smp[p].factoryLoaded = false;
            }));
            sub->addChild(new MenuSeparator);
            sub->addChild(createIndexPtrSubmenuItem(
                "Speed mode", {"Digital", "Tape"}, &m->smp[p].tapeIdx));
            sub->addChild(createBoolPtrMenuItem("Reverse", "", &m->smp[p].reverse));
            sub->addChild(createSubmenuItem("Overdub feedback", "", [m, p](Menu* fb) {
                fb->addChild(new FeedbackSlider(&m->smp[p].feedback));
            }));
            sub->addChild(new MenuSeparator);
            sub->addChild(createBoolPtrMenuItem("Engine: test tone (dev)", "",
                                                &m->smp[p].testTone));
        }));
    }
}

// --- widget -------------------------------------------------------------------
struct FireflowWidget : ModuleWidget {
    FireflowWidget(Fireflow* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Fireflow.svg")));

        // panel lettering (NanoSVG can't render the SVG's <text>; see PanelText)
        auto* labels = new PanelText(module);
        labels->box.size = box.size;
        addChild(labels);

        for (const auto& c : kParamCtls) {
            Vec pos = mm2px(Vec(c.mm.x, c.mm.y));
            switch (c.kind) {
                case WK_BIGKNOB: case WK_KNOBC:
                    addParam(createParamCentered<RoundBlackKnob>(pos, module, c.id)); break;
                case WK_SMKNOB: case WK_KNOBI:
                    if (c.id == ATTACK_A || c.id == ATTACK_B
                            || c.id == STAGES_A || c.id == STAGES_B) {
                        auto* knob = createParamCentered<SlotVisible<Trimpot>>(
                            pos, module, c.id);
                        knob->fireflow = module;
                        knob->ctlId = c.id;
                        addParam(knob);
                    }
                    else {
                        addParam(createParamCentered<Trimpot>(pos, module, c.id));
                    }
                    break;
                case WK_SW2:
                    addParam(createParamCentered<CKSS>(pos, module, c.id)); break;
                case WK_LATCH:
                    if (c.id == ENGINE_A || c.id == ENGINE_B)
                        addParam(createParamCentered<EngineCycleLatch>(pos, module, c.id));
                    else if (c.id == REC_A || c.id == REC_B) {
                        auto* pad = createParamCentered<SlotVisible<VCVLatch>>(
                            pos, module, c.id);
                        pad->fireflow = module;
                        pad->ctlId = c.id;
                        addParam(pad);
                    }
                    else
                        addParam(createParamCentered<VCVLatch>(pos, module, c.id));
                    break;
                case WK_SMBTN:
                    addParam(createParamCentered<VCVButton>(pos, module, c.id)); break;
                default: break;
            }
        }
        for (const auto& c : kInputCtls)
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(c.mm.x, c.mm.y)), module, c.id));
        for (const auto& c : kOutputCtls)
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(c.mm.x, c.mm.y)), module, c.id));
        for (const auto& c : kLightCtls) {
            Vec pos = mm2px(Vec(c.mm.x, c.mm.y));
            if (c.id == REC_A_L || c.id == REC_B_L) {   // record = red, Sampler-only
                auto* led = createLightCentered<SamplerOnly<SmallLight<RedLight>>>(
                    pos, module, c.id);
                led->fireflow = module;
                led->engineId = (c.id == REC_A_L) ? ENGINE_A : ENGINE_B;
                addChild(led);
            }
            else                                       // gate glow = warm signal hue
                addChild(createLightCentered<MediumLight<YellowLight>>(pos, module, c.id));
        }

        // live LED rings, centred on each ring (same coords as the gate lights)
        for (int p = 0; p < 2; ++p) {
            auto* ring = new SpkyRing(module, p);
            Vec ctr = mm2px(Vec(kLightCtls[p].mm.x, kLightCtls[p].mm.y));
            ring->box.pos = ctr.minus(ring->box.size.div(2.f));
            addChild(ring);
        }

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    }

    void appendContextMenu(Menu* menu) override {
        appendFireflowMenu(menu, getModule<Fireflow>());
    }
};

Model* modelFireflow = createModel<Fireflow, FireflowWidget>("Fireflow");

#include "generated_hw_panel.hpp"

// The 60 HP hardware-envelope draft (envelope spec 2026-08-08 §4): same
// Module, same param ids, different sheet metal. Deliberately dumb -- no LED
// rings, no dynamic captions, no engine-aware hiding beyond the shared
// ATTACK/BEND knob and the sampler-only REC pads. An aluminium panel can do
// none of those tricks, so neither does its rehearsal.
//
// Font handling follows PanelText's proven idiom (loadFont from the shared
// system asset, not APP->window->uiFont) rather than the brief's sketch --
// uiFont is the Rack UI's own font, not the panel's ShareTechMono, and would
// have drawn every label in the wrong face.
struct HwPanelText : Widget {
    void draw(const DrawArgs& args) override {
        std::shared_ptr<Font> font =
            APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);

        auto text = [&](float x, float y, float size, float spacing,
                        unsigned rgb, unsigned char anchor, const char* s) {
            nvgFontSize(args.vg, mm2px(size));
            nvgTextLetterSpacing(args.vg, mm2px(spacing));
            nvgFillColor(args.vg, nvgRGB((rgb >> 16) & 0xFF,
                                         (rgb >> 8) & 0xFF, rgb & 0xFF));
            nvgTextAlign(args.vg, (anchor == 1 ? NVG_ALIGN_LEFT :
                                   anchor == 2 ? NVG_ALIGN_RIGHT :
                                   NVG_ALIGN_CENTER) | NVG_ALIGN_BASELINE);
            Vec p = mm2px(Vec(x, y));
            nvgText(args.vg, p.x, p.y, s, nullptr);
        };
        for (const auto& c : spkyhw::kParamCtls)
            if (c.label[0])
                text(c.lbl.x, c.lbl.y, c.lblSize, 0.f, c.lblRgb, c.anchor, c.label);
        for (const auto& c : spkyhw::kInputCtls)
            text(c.lbl.x, c.lbl.y, c.lblSize, 0.f, c.lblRgb, c.anchor, c.label);
        for (const auto& c : spkyhw::kOutputCtls)
            text(c.lbl.x, c.lbl.y, c.lblSize, 0.f, c.lblRgb, c.anchor, c.label);
        for (const auto& c : spkyhw::kHwOnlyCtls)
            if (c.label[0])
                text(c.lbl.x, c.lbl.y, c.lblSize, 0.f, c.lblRgb, c.anchor, c.label);
        for (const auto& t : spkyhw::kPanelTexts)
            text(t.mm.x, t.mm.y, t.size, t.spacing, t.rgb, t.anchor, t.str);
    }
};

struct FireflowHWWidget : ModuleWidget {
    FireflowHWWidget(Fireflow* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/FireflowHW.svg")));
        auto* labels = new HwPanelText();
        labels->box.size = box.size;
        addChild(labels);
        for (size_t i = 0; i < sizeof(spkyhw::kParamCtls) / sizeof(spkyhw::kParamCtls[0]); ++i) {
            const auto& c = spkyhw::kParamCtls[i];
            Vec pos = mm2px(Vec(c.mm.x, c.mm.y));
            // Knob size comes from the hardware class, never from c.kind:
            // KNOBC is bipolar and KNOBI is detented, and a centre-detent pot
            // ships in every size (spec 2026-08-10 §1). A rehearsal that
            // shows a big RATE while the plate prints a small one cannot be
            // used to judge grip.
            const bool big = spkyhw::kParamSize[i] != 0;
            switch (c.kind) {
                case WK_BIGKNOB: case WK_KNOBC: case WK_SMKNOB: case WK_KNOBI:
                    if (big) {
                        addParam(createParamCentered<RoundBlackKnob>(pos, module, c.id));
                        break;
                    }
                    if (c.id == ATTACK_A || c.id == ATTACK_B
                            || c.id == STAGES_A || c.id == STAGES_B) {
                        auto* knob = createParamCentered<SlotVisible<Trimpot>>(pos, module, c.id);
                        knob->fireflow = module;
                        knob->ctlId = c.id;
                        addParam(knob);
                    } else {
                        addParam(createParamCentered<Trimpot>(pos, module, c.id));
                    }
                    break;
                case WK_SW2:
                    addParam(createParamCentered<CKSS>(pos, module, c.id)); break;
                case WK_LATCH:
                    if (c.id == ENGINE_A || c.id == ENGINE_B) {
                        // Detent pot on the hardware draft (spec 2026-08-10 §5);
                        // the big module keeps the cycle latch.
                        if (big)
                            addParam(createParamCentered<EngineCycleLatch>(pos, module, c.id));
                        else
                            addParam(createParamCentered<Trimpot>(pos, module, c.id));
                    }
                    else if (c.id == REC_A || c.id == REC_B) {
                        auto* pad = createParamCentered<SlotVisible<VCVLatch>>(pos, module, c.id);
                        pad->fireflow = module;
                        pad->ctlId = c.id;
                        addParam(pad);
                    } else
                        addParam(createParamCentered<VCVLatch>(pos, module, c.id));
                    break;
                case WK_SMBTN:
                    addParam(createParamCentered<VCVButton>(pos, module, c.id)); break;
                default: break;
            }
        }
        for (const auto& c : spkyhw::kInputCtls)
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(c.mm.x, c.mm.y)), module, c.id));
        for (const auto& c : spkyhw::kOutputCtls)
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(c.mm.x, c.mm.y)), module, c.id));
        for (const auto& c : spkyhw::kLightCtls) {
            Vec pos = mm2px(Vec(c.mm.x, c.mm.y));
            if (c.id == REC_A_L || c.id == REC_B_L) {
                auto* led = createLightCentered<SamplerOnly<SmallLight<RedLight>>>(pos, module, c.id);
                led->fireflow = module;
                led->engineId = (c.id == REC_A_L) ? ENGINE_A : ENGINE_B;
                addChild(led);
            } else
                addChild(createLightCentered<MediumLight<YellowLight>>(pos, module, c.id));
        }
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    }
    void appendContextMenu(Menu* menu) override {
        appendFireflowMenu(menu, getModule<Fireflow>());
    }
};

Model* modelFireflowHW = createModel<Fireflow, FireflowHWWidget>("FireflowHW");
