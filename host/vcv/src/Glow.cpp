// host/vcv/src/Glow.cpp
//
// FireFlow Glow, on the Synthux Simple Touch 2 surface (spec 2026-08-11):
// twelve touch pads, six macro trim knobs, two assignable faders, two
// assignable centre-off switches and a stereo out. No CV inputs, no clock
// input, no NEW button -- the board has none, so neither does the module.
// The big Fireflow module is the full-control view of the same engine; this
// one is the flow machine.
//
// Everything that does not need a Rack type lives in glow_ui.hpp and
// touch_pads.hpp and is tested by the desktop suite. Every coordinate and
// label comes from generated_flow_panel.hpp. Nothing here is hand-placed.
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>
#include "plugin.hpp"
#include "generated_flow_panel.hpp"
#include "glow_ui.hpp"
#include "touch_pads.hpp"
#include "sampler_ui.hpp"

// The portable engine core -- the same headers the render host uses.
#include "instrument.h"
#include "flow/flow.h"
#include "flow/flow_rng.h"      // spky::Rng for drawTwelve()'s seeded sequence
#include "flow/taste.h"
#include "flow/terrain_code.h"
#include "center/center.h"

using namespace spkyvcv::glow;

// The module indexes params[MOTION + m] with m a spky::flow::Macro, so the
// panel's first six params must BE the macro enum. res/test_flow_panel.py
// guards the panel side; this guards the C++ side. static_cast<int> on both
// sides silences -Wenum-compare (ParamId and spky::flow::Macro are two
// distinct enum types) without weakening the check.
static_assert(static_cast<int>(MOTION) == static_cast<int>(spky::flow::M_MOTION),
              "panel macro order drifted");
static_assert(static_cast<int>(DENSITY) == static_cast<int>(spky::flow::M_DENSITY),
              "panel macro order drifted");
static_assert(static_cast<int>(BRIGHT) == static_cast<int>(spky::flow::M_BRIGHT),
              "panel macro order drifted");
static_assert(static_cast<int>(DIRT) == static_cast<int>(spky::flow::M_DIRT),
              "panel macro order drifted");
static_assert(static_cast<int>(WANDER) == static_cast<int>(spky::flow::M_WANDER),
              "panel macro order drifted");
static_assert(static_cast<int>(SPACE) == static_cast<int>(spky::flow::M_SPACE),
              "panel macro order drifted");

static_assert(static_cast<int>(spkyvcv::kPadCount) == PAD_12 - PAD_1 + 1,
              "the panel's pad count must match touch_pads.hpp");
static_assert(PAD_1 == SPACE + 1,
              "controlTick indexes params[PAD_1 + i]; the pads must be "
              "contiguous and follow the six macros");
static_assert(FADER_R == FADER_L + 1,
              "faderPos() indexes params[FADER_L + i]");
static_assert(SW_R == SW_L + 1,
              "switchPos() indexes params[SW_L + i]");

// How many places drawTwelve() draws per archetype. Named so the loop and the
// assert below cannot say different threes.
static constexpr int kPlacesPerArch = 3;

// The only assert in this file that guards a memory WRITE, not just an index
// convention. drawTwelve() fills ARCH_COUNT * kPlacesPerArch entries of
// places[kPadCount], and those two counts have separate owners that have never
// been told about each other: ARCH_COUNT is the engine's (engine/flow/
// flow_ids.h), kPadCount is the board's (touch_pads.hpp). 4 * 3 == 12 is a
// coincidence between an archetype list and an MPR121, not a relationship.
// Add a fifth archetype and the loop writes places[12..14] straight through the
// Module object, over std::string members, with no bounds check anywhere on the
// path. If this fires: change how many places each archetype gets in
// drawTwelve() -- do NOT enlarge places[], which is sized by the pads and by
// nothing else.
static_assert(spky::flow::ARCH_COUNT * kPlacesPerArch
                  == static_cast<int>(spkyvcv::kPadCount),
              "drawTwelve() writes ARCH_COUNT * kPlacesPerArch places into "
              "places[kPadCount]; give each archetype a different share of the "
              "pads, do not widen the array");

// The macro knobs keep c.label empty on the plate (spec 3.3: a printed caption
// would freeze an assignment the rehearsal is allowed to move), so their Rack
// names come from here rather than from the generated table.
static const char* kMacroNames[spky::flow::MACRO_COUNT] = {
    "MOTION", "DENSITY", "BRIGHT", "DIRT", "WANDER", "SPACE"
};

// A pad's NAME is runtime data (spec 6.3), so it cannot come from the
// generated header the way every other caption does -- configButton fixes its
// string at construction. This is the one deliberate carve-out from "the panel
// table is the only source": the tooltip label is computed live from the
// module's Place array, while the plate itself still prints only the number.
struct PadQuantity : SwitchQuantity {
    const spkyvcv::Place* place = nullptr;
    int pad = 0;
    std::string getLabel() override {
        if (place && !place->name.empty())
            return string::f("Pad %d  %s", pad + 1, place->name.c_str());
        return string::f("Pad %d", pad + 1);
    }
};

struct Glow : Module {
    spky::Instrument inst;
    spky::FxMem fxmem;
    spky::AmbientReverb reverb;

    // Engine-owned memory the host has to provide, exactly as the big module
    // does: the FX chain and the BBD engine index into these.
    std::vector<float> echoMem[spky::PART_COUNT][2];
    float bbd[spky::PART_COUNT][2][spky::BbdEngine::kCells];

    // A terrain may put SAMPLER on the texture deck (engine/flow/taste.h,
    // kTextureEngine), and a Sampler deck with an empty buffer is a silent
    // deck. Glow has no REC button and never records, so the buffer only
    // ever holds res/factory.wav -- 16 s is comfortably more than the file
    // needs, against 42 s on the big module which does record.
    static constexpr double kSamplerBufferSeconds = 16.0;
    std::vector<spky::SampleBuffer::Frame> samplerMem[spky::PART_COUNT];
    spky::WavData factoryNative;
    bool factoryNativeTried = false;
    std::vector<float> factoryL, factoryR;

    spky::flow::Flow flow;
    spkyvcv::PadGesture pads;
    spkyvcv::Place places[spkyvcv::kPadCount];
    spkyvcv::RefuseFlash refuse;

    // Which target each assignable control drives (spec 4.3). Module state,
    // saved in dataToJson. Atomic because appendContextMenu writes them on the
    // UI thread while controlTick reads them on the audio thread -- the same
    // standing-value shape rootOverride already uses, not a UiOp (UiOp is a
    // one-shot exchange for an operation).
    std::atomic<int> faderTarget[2] {
        int(spkyvcv::FaderTarget::TEMPO), int(spkyvcv::FaderTarget::MASTER) };
    std::atomic<int> switchTarget[2] {
        int(spkyvcv::SwitchTarget::LOCK), int(spkyvcv::SwitchTarget::SCALE) };

    // The values the SCALE switch GATES. The switch never selects one.
    std::atomic<int> menuScale { spky::SCALE_AEOLIAN };
    std::atomic<int> menuRoot  { 0 };

    // GENRE is a draw constraint, and it moved from a knob to a menu item --
    // so it is now written on the UI thread. Atomic and pushed from
    // controlTick, exactly like rootOverride: this file's standing rule is
    // that every Flow mutation happens on the audio thread, and a menu item
    // calling flow.set_genre() directly would be the first exception.
    std::atomic<int> menuGenre { spky::flow::ARCH_ANY };

    // Every fresh module draws the same twelve places, so "pad 7" means the
    // same thing in a note, a video and somebody else's rack. A drawn place is
    // NOT curated -- parent spec 2 calls the draw a slot machine -- so these
    // twelve make the module playable, and nothing more. Pin curated terrain
    // onto a pad before treating a session as evidence.
    static constexpr uint32_t kPoolSeed = 0xF10Cu;

    float masterGain = 1.f;

    float curSr = 0.f;
    // The flow layer's control rate rides the same raster the rest of the
    // engine already ticks on -- spky::Center::kCtrlInterval, not an
    // independently-chosen number (see host/render/main.cpp's Task 9
    // comment, which made this same choice for the same reason). Tying it
    // to the constant rather than a bare literal means the two hosts can
    // never drift apart again. A finer rate would just be discarded: Center
    // only reads its setters at the 96-sample raster, so any push faster
    // than that is wasted work the audio thread does for nothing -- Center
    // overwrites it before it is ever read.
    static constexpr int kCtrlDiv = spky::Center::kCtrlInterval;
    dsp::ClockDivider ctrlDiv;
    bool woken = false;

    // dataFromJson() can run before onAdd() (patch load), when flow has no
    // Instrument yet -- stash the payload here and apply it from onAdd()
    // instead of pushing into a null Instrument.
    spkyvcv::GlowSave pending;
    bool havePending = false;

    // --- UI-thread -> audio-thread staging (fix round 3) ------------------
    // appendContextMenu's "Paste terrain code" and TerrainCodeField's Enter
    // commit run on Rack's UI thread, but Flow is otherwise only ever touched
    // from controlTick() on the audio thread. Flow::wake() is not a small
    // write (see flow.cpp): it rewrites _terrain, _prev_terrain, re-seeds the
    // sequencer and force-pushes every parameter, so a direct write here could
    // hand the audio thread a half-written terrain mid-tick. dataFromJson()'s
    // LIVE-module branch (curSr > 0, reached via right-click preset load /
    // module paste, which hold no engine lock) has the same problem and is
    // staged the same way.
    //
    // Follow Fireflow.cpp's resyncReq shape: the UI thread fills a payload
    // and sets an atomic flag LAST; controlTick() takes the flag FIRST (via
    // exchange(), so at most one op survives to be applied) and only then
    // reads the payload. Both sides use the atomic's default sequential
    // consistency -- no relaxed/acquire-release hand-rolling -- so a flag
    // the audio thread observes as non-NONE is guaranteed to happen-after
    // the UI thread's payload write.
    enum class UiOp { NONE, SET_TERRAIN, SET_LOCK, RESTORE,
                      NEW_FULL, NEW_PARTIAL, UNDO };
    std::atomic<UiOp> uiOp { UiOp::NONE };
    spky::flow::TerrainState uiState;   // SET_TERRAIN, RESTORE
    spky::flow::TerrainState uiUndo;    // RESTORE
    bool uiHaveUndo = false;            // RESTORE
    bool uiLock = false;                // SET_LOCK, RESTORE
    uint8_t uiMask = 0x3F;              // NEW_PARTIAL

    // The ROOT override (spec 2026-08-07 §3.1), set from appendContextMenu on
    // the UI thread and read every controlTick on the audio thread. Atomic
    // rather than a plain int, and NOT a UiOp: UiOp is a one-shot exchange for
    // an operation, this is a standing value. Default sequential consistency,
    // matching uiOp -- no relaxed/acquire-release hand-rolling.
    std::atomic<int> rootOverride { -1 };     // -1 = AUTO

    Glow() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        for (const auto& c : kParamCtls) {
            switch (c.kind) {
                case WK_MACRO:
                    configParam(c.id, 0.f, 1.f, 0.5f,
                                kMacroNames[c.id - MOTION]);
                    break;
                case WK_PAD: {
                    // configButton clears randomizeEnabled for us (Module.hpp:169),
                    // which is what we want: a Randomize that pokes twelve
                    // momentary pads is a fault, not a dice roll.
                    const int i = c.id - PAD_1;
                    auto* pq = configButton<PadQuantity>(c.id, c.label);
                    pq->place = &places[i];
                    pq->pad = i;
                    pq->description = c.tip;
                    break;
                }
                case WK_FADER:
                    // TEMPO's default sits mid-travel (95 BPM); MASTER's sits
                    // at unity, because a module that boots at half gain is a
                    // bug report.
                    configParam(c.id, 0.f, 1.f,
                                c.id == FADER_R ? 1.f : 0.5f, c.tip);
                    if (auto* pq = paramQuantities[c.id])
                        pq->randomizeEnabled = false;
                    break;
                case WK_SWITCH:
                    // The three position names are RUNTIME TOOLTIP strings, not
                    // panel captions: Rack shows them in the hover readout and
                    // the context menu, and nothing here reaches the plate --
                    // which prints no switch caption at all, as
                    // test_only_the_pads_carry_printed_captions enforces. Same
                    // carve-out from "the panel table is the only source" that
                    // PadQuantity documents above, and for the same reason: a
                    // caption a generator could print would have to be printed.
                    configSwitch(c.id, 0.f, 2.f, 0.f, c.tip,
                                 { "Down", "Centre", "Up" });
                    if (auto* pq = paramQuantities[c.id])
                        pq->randomizeEnabled = false;
                    break;
                // Named rather than defaulted so a future kind is a compile
                // error here instead of a param that silently gets no config.
                case WK_OUT: break;
            }
        }
        // No configInput loop: the board has no inputs, and the generator
        // emits no kInputCtls table for an empty list.
        for (const auto& c : kOutputCtls) configOutput(c.id, c.tip);
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            fxmem.bbd[p][0] = bbd[p][0];
            fxmem.bbd[p][1] = bbd[p][1];
        }
        fxmem.reverb = &reverb;
        ctrlDiv.setDivision(kCtrlDiv);
    }

    // --- the twelve places (spec 6) --------------------------------------
    // Twelve places, three per archetype, from a fixed seed. draw_new's
    // genre branch can in principle exhaust kGenreDrawCap and return a
    // default TerrainState whose archetype does not match -- terrain.cpp
    // calls that edge ~1e-18 and leaves it uncorrected. Verifying with
    // arch_of costs one stream seed, so verify rather than inherit the
    // assumption twelve times over.
    void drawTwelve(uint32_t seed = kPoolSeed) {
        spky::Rng seq;
        seq.seed(seed);
        spky::flow::TerrainState cur = {};
        int i = 0;
        for (int arch = 0; arch < spky::flow::ARCH_COUNT; ++arch) {
            for (int k = 0; k < kPlacesPerArch; ++k) {
                spky::flow::TerrainState st = cur;
                for (int tries = 0; tries < 8; ++tries) {
                    st = spky::flow::draw_new(cur, seq, arch);
                    if (spky::flow::arch_of(st.master) == arch) break;
                }
                cur = st;
                spky::flow::encode_code(st, places[i].code,
                                        int(sizeof places[i].code));
                places[i].name.clear();
                places[i].note.clear();
                ++i;
            }
        }
        pads.reset();
    }

    void pinCurrent(int pad) {
        if (pad < 0 || pad >= spkyvcv::kPadCount) return;
        spky::flow::encode_code(flow.state(), places[pad].code,
                                int(sizeof places[pad].code));
    }

    bool wakePad(int pad) {
        spky::flow::TerrainState st;
        if (pad < 0 || pad >= spkyvcv::kPadCount) return false;
        if (!spky::flow::decode_code(places[pad].code, st)) return false;
        flow.wake(st);
        woken = true;
        return true;
    }

    // --- persistence (spec 5) --------------------------------------------
    // A live set built around a locked terrain has to survive a restart, so
    // a patch carries the whole state: the terrain code, the lock, the one
    // undo slot, the assignments and the twelve places. Preset *systems* --
    // banks, slots, favourites -- are out of scope (spec 8).
    json_t* dataToJson() override {
        const spkyvcv::GlowSave s = spkyvcv::glow_capture(flow);
        json_t* root = json_object();
        json_object_set_new(root, "terrain", json_string(s.code));
        json_object_set_new(root, "lock", json_boolean(s.lock));
        if (s.have_undo) json_object_set_new(root, "undo", json_string(s.undo));
        json_object_set_new(root, "root", json_integer(rootOverride.load()));
        json_object_set_new(root, "menuScale", json_integer(menuScale.load()));
        json_object_set_new(root, "menuRoot", json_integer(menuRoot.load()));

        json_t* fa = json_array();
        json_t* sw = json_array();
        for (int i = 0; i < 2; ++i) {
            json_array_append_new(fa, json_integer(faderTarget[i].load()));
            json_array_append_new(sw, json_integer(switchTarget[i].load()));
        }
        json_object_set_new(root, "faders", fa);
        json_object_set_new(root, "switches", sw);

        json_t* pl = json_array();
        for (const auto& p : places) {
            json_t* o = json_object();
            json_object_set_new(o, "code", json_string(p.code));
            json_object_set_new(o, "name", json_string(p.name.c_str()));
            json_object_set_new(o, "note", json_string(p.note.c_str()));
            json_array_append_new(pl, o);
        }
        json_object_set_new(root, "places", pl);
        return root;
    }

    void dataFromJson(json_t* root) override {
        if (!root) return;
        // The same momentary-pad remedy onAdd runs, because onAdd is not on
        // every path that gets here. Module::fromJson calls paramsFromJson and
        // then dataFromJson, and that pair is the WHOLE of a right-click preset
        // load or a module paste -- the module is already live, so onAdd never
        // runs again and its version of this never fires. A preset stored with
        // a pad held therefore comes back with that param at 1.0, and the next
        // controlTick reads a rising edge, wakes, and rerolls 400 ms later on
        // top of the terrain this call is in the middle of restoring.
        //
        // Unconditional rather than guarded on `curSr > 0.f`: on the patch-load
        // path onAdd will do it again a moment later, and running it twice
        // costs twelve stores, while a missed path costs a silent reroll.
        // Placed before every early return below for the same reason -- the
        // pads are not part of the terrain payload and must not depend on it
        // parsing.
        for (int i = 0; i < spkyvcv::kPadCount; ++i)
            params[PAD_1 + i].setValue(0.f);
        bool padsQuiet[spkyvcv::kPadCount] = {};
        pads.prime(padsQuiet);
        // Read BEFORE the terrain's early return below: the override is
        // independent of the terrain, and a patch whose code is missing or
        // malformed must not lose it as collateral.
        // The type check matters as much as the range one: json_integer_value
        // on a non-integer returns 0, so a corrupt `"root": "F#"` would
        // otherwise transpose the patch to C instead of falling back to AUTO.
        // The sibling `undo` read below checks its type for the same reason.
        // glow_ui.hpp owns the range rule so the desktop suite can test it.
        if (json_t* r = json_object_get(root, "root")) {
            rootOverride = json_is_integer(r)
                ? spkyvcv::clamp_root_override(int(json_integer_value(r)))
                : -1;
        }
        if (json_t* v = json_object_get(root, "menuScale"))
            if (json_is_integer(v)) {
                const int s = int(json_integer_value(v));
                menuScale = (s >= 0 && s < spky::SCALE_LIST_COUNT)
                                ? s : spky::SCALE_AEOLIAN;
            }
        if (json_t* v = json_object_get(root, "menuRoot"))
            if (json_is_integer(v))
                menuRoot = spkyvcv::clamp_root_override(
                    int(json_integer_value(v))) < 0
                        ? 0 : int(json_integer_value(v));
        auto readTargets = [&](const char* key, std::atomic<int>* dst,
                               int lo, int hi) {
            json_t* a = json_object_get(root, key);
            if (!json_is_array(a)) return;
            for (int i = 0; i < 2 && i < int(json_array_size(a)); ++i) {
                json_t* e = json_array_get(a, i);
                if (!json_is_integer(e)) continue;
                const int v = int(json_integer_value(e));
                if (v >= lo && v <= hi) dst[i] = v;
            }
        };
        readTargets("faders", faderTarget, 0, int(spkyvcv::FaderTarget::MASTER));
        readTargets("switches", switchTarget, 0, int(spkyvcv::SwitchTarget::SCALE));

        if (json_t* a = json_object_get(root, "places")) {
            if (json_is_array(a)) {
                for (int i = 0; i < spkyvcv::kPadCount
                                && i < int(json_array_size(a)); ++i) {
                    json_t* o = json_array_get(a, i);
                    if (!json_is_object(o)) continue;
                    json_t* c = json_object_get(o, "code");
                    if (json_is_string(c))
                        std::snprintf(places[i].code, sizeof places[i].code,
                                      "%s", json_string_value(c));
                    json_t* n = json_object_get(o, "name");
                    if (json_is_string(n))
                        places[i].name = spkyvcv::sanitize_label(
                            json_string_value(n), spkyvcv::kNameCap);
                    json_t* t = json_object_get(o, "note");
                    if (json_is_string(t))
                        places[i].note = spkyvcv::sanitize_label(
                            json_string_value(t), spkyvcv::kNoteCap);
                }
            }
        }

        spkyvcv::GlowSave s;
        json_t* code = json_object_get(root, "terrain");
        if (!json_is_string(code)) return;              // nothing to restore
        std::snprintf(s.code, sizeof s.code, "%s", json_string_value(code));
        if (json_t* u = json_object_get(root, "undo")) {
            if (json_is_string(u)) {
                std::snprintf(s.undo, sizeof s.undo, "%s", json_string_value(u));
                s.have_undo = true;
            }
        }
        if (json_t* l = json_object_get(root, "lock"))
            s.lock = json_boolean_value(l);
        // A malformed code leaves everything alone -- a corrupt patch must
        // not silently move the player to some other instrument.
        if (curSr <= 0.f) {          // dataFromJson ran before onAdd
            // Do NOT set `woken` here. reinit() reads `woken` as `hadFlow` to
            // decide whether flow.init(&inst, ...) still needs to run; init()
            // is the only place that sets Flow::_inst, and apply_param() is a
            // no-op while _inst is null. Marking `woken` true before onAdd's
            // first reinit() would make that reinit() skip flow.init()
            // forever, so glow_restore()'s pushed values would never reach
            // the Instrument -- a loaded patch would show the right terrain
            // code and stay permanently silent. `havePending` alone already
            // stops onAdd's `else if (!woken) wakeHouse();` from running (see
            // onAdd below), so `woken` doesn't need to move yet.
            pending = s;
            havePending = true;
            return;
        }
        // LIVE-module branch: this call can run on the UI thread (right-click
        // preset load / module paste go through ModuleWidget::loadAction /
        // pasteJsonAction, not addModule, so no engine lock is held), while
        // process()/controlTick() may be reading Flow on the audio thread
        // right now. Validate here (pure, touches nothing) but stage the
        // apply for controlTick() -- see the UiOp block above.
        spkyvcv::GlowRestorePlan plan;
        if (spkyvcv::glow_restore_plan(s, plan)) {
            uiState = plan.state;
            uiUndo = plan.undo;
            uiHaveUndo = plan.have_undo;
            uiLock = plan.lock;
            uiOp = UiOp::RESTORE;      // payload written above, flag written last
        }
    }

    // Enter a terrain code by hand (context menu). Returns false and changes
    // nothing if the string is not a valid code. Stages the write for
    // controlTick() to apply on the audio thread -- see the UiOp block above.
    bool setTerrainCode(const std::string& text) {
        spky::flow::TerrainState st;
        if (!spky::flow::decode_code(text.c_str(), st)) return false;
        uiState = st;
        uiOp = UiOp::SET_TERRAIN;      // payload written above, flag written last
        return true;
    }

    std::string terrainCode() {
        char buf[spky::flow::kTerrainCodeLen + 1] = {};
        spky::flow::encode_code(flow.state(), buf, int(sizeof buf));
        return std::string(buf);
    }

    // Read res/factory.wav off disk once per module instance, on the main
    // thread, before process() can run. Same split the big module uses: the
    // audio thread only ever memcpys the already-decoded, already-resampled
    // copy in factoryL/factoryR.
    void onAdd(const AddEvent& e) override {
        Module::onAdd(e);
        if (!factoryNativeTried) {
            factoryNativeTried = true;
            std::string err;
            if (!spky::read_wav(asset::plugin(pluginInstance, "res/factory.wav"),
                                factoryNative, err)) {
                WARN("Glow: factory sample unavailable: %s", err.c_str());
                // wav_reader.h resizes both channel vectors to the full frame
                // count BEFORE the fread that can fail, so a truncated file
                // leaves them non-empty and full of zeros -- the
                // !factoryNative.l.empty() guard below would pass and load
                // silence into both decks with no diagnostic. Clear them so
                // that guard means what it says.
                factoryNative.l.clear();
                factoryNative.r.clear();
            }
        }
        reinit(curSr > 0.f ? curSr : 48000.f);
        if (havePending) {
            havePending = false;
            // reinit() just ran with `woken` still false (dataFromJson left
            // it that way on purpose -- see the comment there), so
            // flow.init(&inst, ...) really did run above and Flow::_inst is
            // live. Mark `woken` true only now that the restore has actually
            // reached the Instrument, so a later onSampleRateChange()'s
            // reinit() takes the incremental branch instead of re-running
            // init() and wiping this restore out.
            if (spkyvcv::glow_restore(flow, pending)) {
                woken = true;
            } else {
                wakeHouse();          // malformed payload: fall back, same as a fresh insert
            }
        } else if (!woken) {
            wakeHouse();
        }
        // Rack saves momentary params, so a patch stored mid-hold comes back
        // with a pad pressed. Zero them and prime the gesture with what it
        // sees, or the first controlTick reads a rising edge, wakes, and
        // rerolls 400 ms later on top of the terrain just restored.
        for (int i = 0; i < spkyvcv::kPadCount; ++i)
            params[PAD_1 + i].setValue(0.f);
        bool downNow[spkyvcv::kPadCount] = {};
        pads.prime(downNow);
    }

    void wakeHouse() {
        if (places[0].code[0] == '\0') drawTwelve();
        if (!wakePad(0)) {
            spky::flow::TerrainState st;
            if (!spky::flow::decode_code(spky::flow::kHouseCode, st)) st = {};
            flow.wake(st);
            woken = true;
        }
        // wake() itself never touches the lock, so without this a locked
        // module would land on pad 1 still locked.
        flow.set_lock(false);
        pads.live = 0;
        pads.excursion = false;
    }

    void reinit(float sr) {
        // Capture whatever the flow layer is holding: inst.init() below wipes
        // every setter, and Flow only re-pushes on wake(), which is the last
        // thing this function does.
        const bool hadFlow = woken;
        const spky::flow::TerrainState st = flow.state();
        const spky::flow::TerrainState un = flow.undo_state();
        const bool lock = flow.locked(), haveUndo = flow.can_undo();

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

        // Rebuild the factory audio for this rate, then load it into BOTH
        // decks: a terrain can hand either deck to the SAMPLER engine at any
        // reroll, and there is no player gesture that could load it later.
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
        if (!factoryL.empty()) {
            size_t n = factoryL.size();
            if (n > frames) n = frames;
            for (int p = 0; p < spky::PART_COUNT; ++p)
                inst.load_sample(p, factoryL.data(), factoryR.data(), n);
        }

        // Flow::init() resets the terrain, the lock and the undo slot, so it
        // may run at most once per module instance. Every later rate change
        // goes through set_ctrl_hz(), which touches only the tick period and
        // the SPACE slew coefficient.
        const float ctrlHz = sr / float(kCtrlDiv);
        if (!hadFlow) {
            flow.init(&inst, ctrlHz);
        } else {
            flow.set_ctrl_hz(ctrlHz);
            flow.wake(st);            // force-pushes every setter into the
            flow.set_lock(lock);      // freshly initialised Instrument
            flow.restore_undo(un, haveUndo);
        }
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        reinit(e.sampleRate);
    }

    void onReset() override {
        // Tonality FIRST, before reinit/wakeHouse -- reinit() and wakeHouse()
        // force-push every parameter, so clearing afterwards would push the
        // PREVIOUS tonality once and self-correct a control period later.
        rootOverride = -1;
        menuScale = spky::SCALE_AEOLIAN;
        menuRoot = 0;
        // menuGenre too, and not only flow.set_genre(): controlTick pushes
        // menuGenre into Flow every tick, so clearing Flow alone would be
        // undone a control period later and Initialize would silently keep
        // the previous genre constraint.
        menuGenre = spky::flow::ARCH_ANY;
        faderTarget[0] = int(spkyvcv::FaderTarget::TEMPO);
        faderTarget[1] = int(spkyvcv::FaderTarget::MASTER);
        switchTarget[0] = int(spkyvcv::SwitchTarget::LOCK);
        switchTarget[1] = int(spkyvcv::SwitchTarget::SCALE);
        flow.set_genre(spky::flow::ARCH_ANY);
        flow.set_scale_override(-1);
        flow.set_root_override(-1);
        drawTwelve();                 // the SAME twelve: the seed is fixed
        reinit(curSr > 0.f ? curSr : 48000.f);
        wakeHouse();
    }

    void controlTick(float sr) {
        // Host-owned settings first: wake() below force-pushes every
        // parameter, so an override applied after it would miss that push.
        const int swScalePos = switchPos(spkyvcv::SwitchTarget::SCALE);
        const spkyvcv::TonalityGate ton =
            swScalePos < 0 ? spkyvcv::TonalityGate{}
                           : spkyvcv::scale_switch(swScalePos,
                                                   menuScale.load(),
                                                   menuRoot.load());
        flow.set_scale_override(ton.scale_ovr);
        flow.set_root_override(ton.root_ovr >= 0 ? ton.root_ovr
                                                 : rootOverride.load());

        const int swLockPos = switchPos(spkyvcv::SwitchTarget::LOCK);
        if (swLockPos >= 0) flow.set_lock(spkyvcv::lock_switch(swLockPos));

        flow.set_genre(menuGenre.load());

        // Apply whatever the UI thread staged (fix round 3): flag read FIRST
        // via exchange() -- so at most one op survives even if two menu
        // actions landed in the same UI frame -- payload read only after.
        switch (uiOp.exchange(UiOp::NONE)) {
            case UiOp::SET_TERRAIN: flow.wake(uiState); woken = true; break;
            case UiOp::SET_LOCK:    flow.set_lock(uiLock); break;
            case UiOp::NEW_FULL:    if (!flow.new_full()) refuse.mark(flow.now_s()); break;
            case UiOp::NEW_PARTIAL: if (!flow.new_partial(uiMask)) refuse.mark(flow.now_s()); break;
            case UiOp::UNDO:        if (!flow.undo()) refuse.mark(flow.now_s()); break;
            case UiOp::RESTORE:
                flow.wake(uiState);
                flow.set_lock(uiLock);
                flow.restore_undo(uiUndo, uiHaveUndo);
                woken = true;
                break;
            case UiOp::NONE: default: break;
        }

        for (int m = 0; m < spky::flow::MACRO_COUNT; ++m)
            flow.set_macro(m, params[MOTION + m].getValue());

        // --- the pads (spec 5.3) -----------------------------------------
        // The gesture's clock is Flow's own, which advances one dt per
        // tick(); reading it before the tick means every event this pass
        // carries the same timestamp, which is exactly what a control tick is.
        const double t = flow.now_s();
        bool down[spkyvcv::kPadCount];
        for (int i = 0; i < spkyvcv::kPadCount; ++i)
            down[i] = params[PAD_1 + i].getValue() > 0.5f;
        const spkyvcv::PadEvent ev = pads.update(down, t);
        if (ev.action == spkyvcv::PadAction::WAKE) {
            if (!wakePad(ev.pad)) refuse.mark(t);
        } else if (ev.action == spkyvcv::PadAction::REROLL) {
            // Under LOCK, wake() is not a gesture and is not refused, but
            // new_partial IS (flow.h). So pads still change place while holds
            // do nothing -- LOCK guards the generator, not the recall.
            const bool ok = flow.new_partial(0x3F);
            pads.excursion = ok;
            if (!ok) refuse.mark(t);
        }

        flow.tick();

        // Tempo: the terrain owns it and Flow re-pushes it on EVERY terrain
        // change, so a host-side fader has to be re-applied every tick or one
        // wake would silently hand the place its own tempo back. `off` is how
        // you ask for exactly that.
        const int fTempo = faderPos(spkyvcv::FaderTarget::TEMPO);
        if (fTempo >= 0)
            inst.set_tempo_bpm(spkyvcv::fader_tempo_bpm(
                params[FADER_L + fTempo].getValue()));

        const int fMaster = faderPos(spkyvcv::FaderTarget::MASTER);
        masterGain = fMaster >= 0
            ? spkyvcv::fader_master_gain(params[FADER_L + fMaster].getValue())
            : 1.f;
    }

    // Which physical control (0 = left, 1 = right) is assigned to `want`,
    // or -1 if neither is. Assigning both to the same target is allowed and
    // the left one wins -- a rehearsal rig should not refuse a knob setting.
    int faderPos(spkyvcv::FaderTarget want) const {
        for (int i = 0; i < 2; ++i)
            if (faderTarget[i].load() == int(want)) return i;
        return -1;
    }
    // NOT const, unlike faderPos: engine::Param::getValue() is a non-const
    // member (Param.hpp), so reading a param position out of a const Module
    // does not compile.
    int switchPos(spkyvcv::SwitchTarget want) {
        for (int i = 0; i < 2; ++i)
            if (switchTarget[i].load() == int(want))
                return int(params[SW_L + i].getValue() + 0.5f);
        return -1;
    }

    void process(const ProcessArgs& args) override {
        if (args.sampleRate != curSr) reinit(args.sampleRate);
        if (ctrlDiv.process()) controlTick(args.sampleRate);

        float outl = 0.f, outr = 0.f;
        inst.process(nullptr, nullptr, &outl, &outr, 1);
        outputs[OUT_L].setVoltage(clamp(outl * masterGain, -1.f, 1.f) * 5.f);
        outputs[OUT_R].setVoltage(clamp(outr * masterGain, -1.f, 1.f) * 5.f);
    }
};

// The SVG's <text> is invisible to NanoSVG, so the lettering is redrawn here
// from the generated tables -- the same reason the big module has PanelText.
struct GlowText : Widget {
    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font =
            APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        auto put = [&](float xmm, float ymm, float szmm, unsigned c,
                       unsigned char anchor, const char* s) {
            nvgFontSize(args.vg, mm2px(szmm));
            nvgFillColor(args.vg, nvgRGB((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff));
            nvgTextAlign(args.vg,
                         (anchor == 1 ? NVG_ALIGN_LEFT
                                      : anchor == 2 ? NVG_ALIGN_RIGHT
                                                    : NVG_ALIGN_CENTER) |
                         NVG_ALIGN_BASELINE);
            const Vec p = mm2px(Vec(xmm, ymm));
            nvgText(args.vg, p.x, p.y, s, nullptr);
        };
        for (const auto& t : kTexts)
            put(t.mm.x, t.mm.y, t.size, t.rgb, t.anchor, t.str);
        for (const auto& c : kParamCtls)
            put(c.lbl.x, c.lbl.y, c.lblSize, c.lblRgb, c.anchor, c.label);
        for (const auto& c : kOutputCtls)
            put(c.lbl.x, c.lbl.y, c.lblSize, c.lblRgb, c.anchor, c.label);
    }
};

// A menu text field that hands its contents to the module on Enter and
// closes the menu. ui::TextField itself has no commit behaviour.
struct TerrainCodeField : ui::TextField {
    Glow* module = nullptr;
    void onSelectKey(const SelectKeyEvent& e) override {
        if (module && e.action == GLFW_PRESS
            && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
            module->setTerrainCode(text);
            e.consume(this);
            if (MenuOverlay* overlay = getAncestorOfType<MenuOverlay>())
                overlay->requestDelete();
            return;
        }
        ui::TextField::onSelectKey(e);
    }
};

// The one control Rack does not ship. app::Switch is ALREADY "a ParamWidget
// which, in momentary mode, sets the value to maxValue when the mouse is held
// and minValue when released" (app/Switch.hpp) -- deriving from ParamWidget
// instead would mean re-implementing onDragStart/onDragEnd by hand.
// app::SvgSwitch is the subclass that adds frames; Switch itself has no
// artwork, which is exactly the custom-drawn case.
//
// The widget stays stupid: tap-versus-hold is decided by the module in
// controlTick, never here. One place owns the gesture.
struct TouchPlate : app::Switch {
    Glow* mod = nullptr;
    int pad = 0;

    TouchPlate() {
        momentary = true;
        box.size = mm2px(Vec(kPadW, kPadH));
    }

    void draw(const DrawArgs& args) override {
        // Runtime state, not panel state -- which is why there are no
        // LightIds for the pads. Twelve lights in the generated header would
        // put runtime state into a panel table.
        const bool live = mod && mod->pads.live == pad;
        const bool exc = live && mod->pads.excursion;
        const bool flash = mod && mod->refuse.active(mod->flow.now_s());

        NVGcolor collar;
        // Copper (#b96532, the panel's part-B accent) is reserved for "that
        // worked" -- a hold that actually rerolled. A refusal gets its own
        // colour, because telling the player "I refused you" is the only thing
        // RefuseFlash exists for, and both ways a PAD can earn one -- a hold
        // that LOCK turned down, and a pad whose stored code will not decode --
        // are exactly the moments where a copper collar would read as the
        // reroll that just did not happen.
        //
        // #8f4a45 is a muted, greyish red: same value range as COPPER (L ~42 %
        // against ~46 %) so it does not shout louder than the accent it sits
        // beside, but a third of its saturation and 20 degrees round the wheel,
        // which is what makes it read as dull brick rather than as warm metal.
        // It belongs with MUTED (#656056) in gen_panel's palette, not above
        // COPPER. Both live here rather than in the generated table for the
        // reason the whole widget does: this is runtime state, not panel state.
        if (flash && live)    collar = nvgRGB(0x8f, 0x4a, 0x45);
        else if (exc)         collar = nvgRGB(0xb9, 0x65, 0x32);
        else if (live)        collar = nvgRGB(0x1d, 0x6f, 0x5f);
        else                  collar = nvgRGBA(0xd7, 0xcd, 0xbb, 0xff);

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f,
                       box.size.y - 1.f, mm2px(kPadR));
        nvgStrokeColor(args.vg, collar);
        nvgStrokeWidth(args.vg, live ? mm2px(0.55f) : mm2px(0.28f));
        nvgStroke(args.vg);
        app::Switch::draw(args);
    }
};

struct GlowWidget : ModuleWidget {
    GlowWidget(Glow* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Glow.svg")));

        auto* labels = new GlowText();
        labels->box.size = box.size;
        addChild(labels);

        for (const auto& c : kParamCtls) {
            const Vec pos = mm2px(Vec(c.mm.x, c.mm.y));
            switch (c.kind) {
                case WK_MACRO:
                    // The plate prints the cap at the board's own measured
                    // silkscreen collar (touch2_geometry.KNOB_COLLAR_R = 3.885,
                    // so 7.77 mm across), and the widget is picked against
                    // that, not against a round number. Measured off
                    // res/ComponentLibrary at Rack's 75 DPI: RoundSmallBlackKnob
                    // 7.68 mm, Trimpot 6.05 mm, RoundBlackKnob 9.60 mm. The
                    // small black knob misses the print by 0.09 mm; Trimpot
                    // would leave a 0.9 mm ring of printed cap showing all
                    // round, which is what a knob that has fallen off looks
                    // like. (Trimpot is also not "Rack's only small knob", as
                    // this comment used to claim.)
                    addParam(createParamCentered<RoundSmallBlackKnob>(pos, module,
                                                                      c.id));
                    break;
                case WK_PAD: {
                    auto* p = createParamCentered<TouchPlate>(pos, module, c.id);
                    p->mod = module;
                    p->pad = c.id - PAD_1;
                    addParam(p);
                    break;
                }
                case WK_FADER:
                    // VCVSlider is 19.843 x 76.535 px at 75 DPI = 6.72 x 25.92
                    // mm, handle 3.98 mm. The board's fader measures about
                    // 24 mm, so the stock widget fits unmodified -- a lucky
                    // fit, recorded so nobody re-derives it.
                    addParam(createParamCentered<VCVSlider>(pos, module, c.id));
                    break;
                case WK_SWITCH:
                    // CKSSThree, not NKK. Both have three positions, so "the
                    // board's switches are centre-off, two digital pins each"
                    // -- true, and the reason this is a three-position widget
                    // at all -- does not choose between them. Size does.
                    // Measured off res/ComponentLibrary at Rack's 75 DPI:
                    // NKK_0.svg is 10.84 x 14.86 mm against the 5.00 x 9.00 mm
                    // this plate prints, i.e. 2.2x the printed footprint, while
                    // CKSSThree_0.svg is 4.56 x 9.60 and fits it.
                    //
                    // The overlap is not cosmetic. At NKK's size SW_L covers
                    // 1.90 x 8.16 mm of PAD_2, and SW_R reaches into PAD_3 and
                    // PAD_8. Switches come after pads in kParamCtls and Rack
                    // hit-tests children in reverse order, so the switch takes
                    // the press and those three pads lose 10-23 % of their
                    // clickable area. Enlarging the printed footprint to match
                    // NKK is not the alternative: both switches are mounted
                    // THROUGH the pad field on the real board and there is no
                    // room there (see "the pad field" in gen_flow_panel.py).
                    addParam(createParamCentered<CKSSThree>(pos, module, c.id));
                    break;
                case WK_OUT: break;
            }
        }
        for (const auto& c : kOutputCtls)
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(c.mm.x, c.mm.y)),
                                                       module, c.id));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(
            Vec(box.size.x - 2 * RACK_GRID_WIDTH,
                RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    }

    void appendContextMenu(Menu* menu) override {
        auto* m = getModule<Glow>();
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Terrain " + m->terrainCode()));

        // Which genre the CURRENT terrain is. Free now that arch_of exists,
        // and the point of the GENRE constraint is auditioning one archetype
        // at a time -- without this the player cannot see which one they
        // are in.
        static const char* kArchNames[] = { "Drone", "Pulse", "Arp", "Fragment" };
        menu->addChild(createMenuLabel(
            std::string("Genre ") +
            kArchNames[spky::flow::arch_of(m->flow.state().master)]));

        menu->addChild(createIndexSubmenuItem(
            "Root",
            { "Auto", "C", "C#", "D", "D#", "E", "F",
              "F#", "G", "G#", "A", "A#", "B" },
            [m]() { return m->rootOverride.load() + 1; },
            [m](int i) { m->rootOverride = i - 1; }));

        // Share a terrain: the code is the terrain's whole identity, so
        // copying it out and pasting it in carries the place itself. It is no
        // longer the instrument's whole STATE -- an explicit SCALE or ROOT
        // override (spec 2026-08-07 §3) rides on top of it and travels in the
        // patch, not in the code -- so a pasted code reproduces the sharer's
        // terrain, not necessarily their tonality.
        menu->addChild(createMenuItem("Copy terrain code", "", [m]() {
            glfwSetClipboardString(APP->window->win, m->terrainCode().c_str());
        }));
        menu->addChild(createMenuItem("Paste terrain code", "", [m]() {
            const char* s = glfwGetClipboardString(APP->window->win);
            if (s) m->setTerrainCode(s);
        }));

        auto* field = new TerrainCodeField;
        field->module = m;
        field->box.size.x = 180.f;
        field->placeholder = "F1-XXXXXXXX-000000000000";
        field->setText(m->terrainCode());
        menu->addChild(field);
        // No "Terrain lock" item: the LOCK switch owns that state now, and a
        // physical switch plus a menu item for one state is a synchronisation
        // bug waiting to be filed.
    }
};

Model* modelGlow = createModel<Glow, GlowWidget>("Glow");
