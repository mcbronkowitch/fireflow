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
#include <functional>
#include <string>
#include <vector>
#include "plugin.hpp"
#include "generated_flow_panel.hpp"
#include "glow_ui.hpp"
#include "touch_pads.hpp"
#include "flow_patch_bridge.hpp"   // encode_base/decode_base: the one encoding
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
// The engine enumerator became M_PACE on 2026-08-12 (flow_ids.h); the panel
// enum, its caption and the "Reroll one macro" submenu entry followed in the
// same task (PACE plan, Glow task). Slot 3 is what this line asserts, and
// slot 3 has not moved.
static_assert(static_cast<int>(PACE) == static_cast<int>(spky::flow::M_PACE),
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

// How many places drawTwelve() draws per archetype, and the assert that stops
// the loop writing past places[kPadCount], both in touch_pads.hpp -- ctest
// compiles that header, and nothing in the desktop build compiles this file.
// The asserts that DO live here all guard index conventions, and every one of
// them is double-covered by PARAM_ORDER in res/test_flow_panel.py.
using spkyvcv::kPlacesPerArch;

// The macro knobs keep c.label empty on the plate (spec 3.3: a printed caption
// would freeze an assignment the rehearsal is allowed to move), so their Rack
// names come from here rather than from the generated table.
static const char* kMacroNames[spky::flow::MACRO_COUNT] = {
    "MOTION", "DENSITY", "BRIGHT", "PACE", "WANDER", "SPACE"
};

// A pad's NAME is runtime data (spec 6.3), so it cannot come from the
// generated header the way every other caption does -- configButton fixes its
// string at construction. This is the one deliberate carve-out from "the panel
// table is the only source": the tooltip label is computed live from the
// module's Place array, while the plate itself still prints only the number.
//
// The `[base]` marker is the other half of the same rule (spec 2026-08-11
// §6): a place with a hand-authored overlay is NOT fully described by its
// code, and a UI that prints only the code implies it is. Every place where a
// pad is named carries the marker -- this tooltip, the Places submenu and the
// paste submenu -- so the fact cannot be seen in one list and missed in
// another. It says only THAT there is a base, not what is in it; the base
// itself is a 47-row overlay and belongs in the clipboard, not in a title.
struct PadQuantity : SwitchQuantity {
    const spkyvcv::Place* place = nullptr;
    int pad = 0;
    std::string getLabel() override {
        std::string s = string::f("Pad %d", pad + 1);
        if (!place) return s;
        if (place->name[0] != '\0') { s += "  "; s += place->name; }
        if (place->has_base) s += "  [base]";
        return s;
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
    //
    // The twelve places are staged the same way, and for the same reason: the
    // workshop menu names them, notes them, pins terrain onto them and redraws
    // all twelve, while wakePad() reads places[pad].code from controlTick. So
    // places[] has ONE WRITER -- the audio thread -- and SEVERAL TOLERANT
    // READERS: the UI thread hands over a whole replacement array (SET_PLACES)
    // instead of reaching into it, but it also READS places[] unsynchronised in
    // several spots (PadQuantity::getLabel, both submenu titles, both
    // LabelField::setText calls, the copy loop in stagePlaces, and dataToJson,
    // which Rack's autosave can run at any moment). All of them are benign, and
    // here is why, so nobody has to re-derive it. Two kinds of field now, and
    // two arguments:
    //
    //   * THE STRINGS (code, name, note). A torn read can only ever show a
    //     stale or half-updated string, never run off the buffer, because
    //     set_label and encode_code are the only writers and both terminate
    //     within `cap` -- the buffers are sized cap + 1 (touch_pads.hpp), so a
    //     NUL is present at every byte offset a reader can reach. Worst case a
    //     menu opens showing the previous name.
    //
    //   * THE BASE (float[P_COUNT] + bool[P_COUNT], since the 2026-08-11
    //     transfer round). Fixed arrays of PODs, so there is no length for a
    //     torn read to get wrong the way a string has -- the bound is a
    //     compile-time constant and a reader cannot run off it. pinCurrent is
    //     the only writer that is not a whole-array handover, and it writes the
    //     overlay BEFORE the has_base flag that publishes it, so the SOURCE
    //     never asks for a place to claim a base it has not been given. That
    //     is a convention, not a guarantee: both fields are plain non-atomic
    //     stores, so nothing in the memory model forbids the flag being
    //     published first. A fence here would not close it either -- the
    //     readers are plain non-atomic reads with no acquire side, and
    //     std::atomic on the fields is ruled out because Place must stay
    //     trivially copyable (two static_asserts depend on it, and SET_PLACES
    //     is a memcpy). What a torn read CAN still show is a pad that already
    //     had a base being re-pinned: the
    //     reader may see a mixture of the old overlay and the new one under
    //     has_base = true. Bounded, never unsafe, and exactly the same shape as
    //     the torn `code` string this paragraph already accepts -- the place is
    //     one pin behind. Neither is worth a lock on the audio thread.
    //
    // Whole array rather than one field because a place is only meaningful as a
    // set: a partial write is a place whose name belongs to some other code.
    // Place is trivially copyable (touch_pads.hpp asserts it), so applying one
    // costs a memcpy and allocates nothing.
    //
    // PIN is the exception that proves it: pinning reads flow.state(), which
    // is audio-thread property, so the menu stages the PAD NUMBER and the
    // audio thread does both the read and the write.
    //
    // There is no SET_LOCK, and no staged lock value either. The switch owns
    // the lock now (spec §4.3, "one control, one truth") and controlTick
    // pushes it every tick, BEFORE this switch runs -- so a one-shot lock op,
    // or a `uiLock` carried by RESTORE, would be overwritten a control period
    // later. It had no producer left after Task 4 removed the menu's lock
    // toggle, and adding one back would just be that synchronisation bug with
    // extra steps. glow_ui.hpp's GlowSave note has the whole reasoning.
    enum class UiOp { NONE, SET_TERRAIN, RESTORE,
                      NEW_FULL, NEW_PARTIAL, UNDO, SET_PLACES, PIN };
    std::atomic<UiOp> uiOp { UiOp::NONE };
    spky::flow::TerrainState uiState;   // SET_TERRAIN, RESTORE
    spky::flow::TerrainState uiUndo;    // RESTORE
    bool uiHaveUndo = false;            // RESTORE
    // RESTORE's two overlays. They ride the staged payload rather than being
    // read off `pending`, because the live-module branch of dataFromJson never
    // touches Flow -- see the LIVE-module note there.
    spky::flow::BaseOverlay uiBase;     // RESTORE: the live place's base
    bool uiHaveBase = false;
    spky::flow::BaseOverlay uiUndoBase; // RESTORE: the undo slot's
    bool uiHaveUndoBase = false;
    uint8_t uiMask = 0x3F;              // NEW_PARTIAL
    spkyvcv::Place uiPlaces[spkyvcv::kPadCount];   // SET_PLACES, RESTORE
    bool uiForget = false;              // SET_PLACES: the live pad is gone too
    int uiPin = -1;                     // PIN

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
    //
    // Static and writing through a pointer, so the UI thread can draw into the
    // staging array without touching module state: the menu's "Redraw all
    // twelve" runs here, on Rack's UI thread, and hands the result over as a
    // SET_PLACES. Nothing in this function reads or writes the module.
    static void drawTwelveInto(spkyvcv::Place* dst, uint32_t seed) {
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
                spky::flow::encode_code(st, dst[i].code,
                                        int(sizeof dst[i].code));
                dst[i].name[0] = '\0';
                dst[i].note[0] = '\0';
                // ...and the base, which is the one field a redraw could
                // otherwise carry over. This function draws INTO an array that
                // already holds twelve places (the menu's reset stages a copy
                // of the live ones), so leaving it alone would give a
                // brand-new terrain the previous place's hand-authored patch
                // -- and the item promises to discard exactly that.
                dst[i].base = {};
                dst[i].has_base = false;
                ++i;
            }
        }
    }

    // The engine-stopped version: onAdd() and onReset() both run with the
    // engine held, so they write places[] outright and reset the gesture with
    // it. The menu must NOT call this -- see stagePlaces() below.
    void drawTwelve(uint32_t seed = kPoolSeed) {
        drawTwelveInto(places, seed);
        pads.reset();
    }

    // AUDIO THREAD ONLY (UiOp::PIN). It reads flow.state() and writes
    // places[], and both of those belong to this thread.
    void pinCurrent(int pad) {
        if (pad < 0 || pad >= spkyvcv::kPadCount) return;
        spky::flow::encode_code(flow.state(), places[pad].code,
                                int(sizeof places[pad].code));
        // The code stopped being the whole of a place when places grew a base.
        // A terrain woken from a hand-authored base is still playing that base,
        // and the code cannot hold it -- so pinning without this would quietly
        // degrade a pasted patch to the bare seed underneath it, and the loss
        // would only surface the next time that pad was pressed.
        //
        // PAYLOAD FIRST, FLAG LAST, like every other handover in this file.
        // places[] has unsynchronised UI-thread readers -- stagePlaces' copy
        // loop, and dataToJson, which Rack's autosave can run at any moment --
        // and this is the audio thread writing underneath them. Setting
        // has_base first would publish "there is a base here" over the base
        // this pad had a moment ago, and stagePlaces would hand that pair
        // straight back as authoritative. This order is what the source ASKS
        // for; it is a convention rather than a guarantee, because both stores
        // are plain and non-atomic. See the tearing note above applyPlaces()
        // for why it is not promoted to one, and for what a torn read of the
        // overlay itself can cost.
        const spky::flow::BaseOverlay* ov = flow.overlay();
        if (ov) places[pad].base = *ov;
        places[pad].has_base = ov != nullptr;
    }

    // Audio thread, or a stopped engine (onAdd's patch-load path). A memcpy
    // per place -- Place is trivially copyable, so nothing here allocates.
    void applyPlaces() {
        for (int i = 0; i < spkyvcv::kPadCount; ++i) places[i] = uiPlaces[i];
        if (uiForget) {                 // a fresh draw: no pad holds a place
            pads.live = -1;
            pads.excursion = false;
        }
    }

    // UI THREAD. Copy the twelve the menu is showing, let `edit` change them,
    // hand the whole array over. Reading places[] here is a plain read of
    // trivially copyable bytes the audio thread only rewrites on an op this
    // same thread staged, so the worst it can show is a place from a moment
    // ago -- the same window every other UiOp in this file has, and the reason
    // the payload is written before the flag.
    template <class Edit>
    void stagePlaces(Edit&& edit, bool forget = false) {
        for (int i = 0; i < spkyvcv::kPadCount; ++i) uiPlaces[i] = places[i];
        edit(uiPlaces);
        uiForget = forget;
        uiOp = UiOp::SET_PLACES;       // payload written above, flag written last
    }

    // Hand over whatever is already sitting in uiPlaces. Direct while the
    // module is not running yet (dataFromJson before onAdd: nothing can be
    // reading places[]), staged once it is.
    //
    // uiForget is set HERE rather than left to the caller: it is the other half
    // of the SET_PLACES payload, and applyPlaces() reads it. Every caller of
    // this function is a restore, and a restore does not disown the live pad --
    // a redraw does, and that path goes through stagePlaces(edit, true) with
    // its own flag. Written before uiOp for the same reason the array is:
    // payload first, flag last.
    void commitPlaces() {
        uiForget = false;
        if (curSr <= 0.f) applyPlaces();
        else uiOp = UiOp::SET_PLACES;
    }

    bool wakePad(int pad) {
        spky::flow::TerrainState st;
        if (pad < 0 || pad >= spkyvcv::kPadCount) return false;
        if (!spky::flow::decode_code(places[pad].code, st)) return false;
        // The place's hand-authored base, if it has one. nullptr and NOT an
        // empty overlay when it has not: wake(s, nullptr) plays the terrain as
        // drawn, and it is also what clears a base the PREVIOUS pad set -- so
        // pressing a bare pad after a hand-authored one really does leave the
        // hand-authored patch behind (flow.h, wake()).
        flow.wake(st, places[pad].has_base ? &places[pad].base : nullptr);
        woken = true;
        return true;
    }

    // --- persistence (spec 5) --------------------------------------------
    // A live set built around one terrain has to survive a restart, so a patch
    // carries the whole state: the terrain code, the one undo slot, the
    // standing menu values, the assignments and the twelve places. Preset
    // *systems* -- banks, slots, favourites -- are out of scope (spec 8).
    //
    // There is no "lock" key. It was written until this round, but the LOCK
    // switch has owned that state since Task 5, and controlTick pushes the
    // switch's answer every tick -- so a restored lock lived about two
    // milliseconds and nothing could observe it. Dropping the key rather than
    // keeping unobservable state (dev alpha: no migration, no fallback).
    json_t* dataToJson() override {
        const spkyvcv::GlowSave s = spkyvcv::glow_capture(flow);
        json_t* root = json_object();
        json_object_set_new(root, "terrain", json_string(s.code));
        if (s.have_undo) json_object_set_new(root, "undo", json_string(s.undo));
        // The LIVE place's hand-authored base, and the undo slot's. Written
        // only when there is one: a patch with no base must come back with no
        // base, and an absent key is how that is said. Same encoder as the
        // places below and as the clipboard -- one format, one round trip.
        if (s.have_base)
            json_object_set_new(root, "base",
                                json_string(spkyvcv::encode_base(s.base).c_str()));
        if (s.have_undo_base)
            json_object_set_new(root, "undoBase",
                                json_string(spkyvcv::encode_base(s.undo_base).c_str()));
        json_object_set_new(root, "root", json_integer(rootOverride.load()));
        json_object_set_new(root, "menuScale", json_integer(menuScale.load()));
        json_object_set_new(root, "menuRoot", json_integer(menuRoot.load()));
        // GENRE travels with the other two standing values. It was left out
        // while it was a knob (Rack saves params for you) and stayed out one
        // task too long after it became a menu item: a rehearsal that
        // auditions one archetype for an evening should not lose the
        // constraint on reload while ROOT and SCALE survive.
        json_object_set_new(root, "genre", json_integer(menuGenre.load()));

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
            json_object_set_new(o, "name", json_string(p.name));
            json_object_set_new(o, "note", json_string(p.note));
            if (p.has_base)
                json_object_set_new(o, "base",
                                    json_string(spkyvcv::encode_base(p.base).c_str()));
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
        // The other two standing values take the same route, and their range
        // rules live in glow_ui.hpp for the same reason the root's does -- so
        // the desktop suite can test them without Rack.
        if (json_t* v = json_object_get(root, "menuScale"))
            if (json_is_integer(v))
                menuScale = spkyvcv::clamp_menu_scale(int(json_integer_value(v)));
        if (json_t* v = json_object_get(root, "menuRoot")) {
            // menuRoot is the SCALE switch's up-position root, which has no
            // AUTO: an out-of-range save falls back to C, not to -1.
            if (json_is_integer(v)) {
                const int r = spkyvcv::clamp_root_override(
                    int(json_integer_value(v)));
                menuRoot = r < 0 ? 0 : r;
            }
        }
        if (json_t* v = json_object_get(root, "genre"))
            if (json_is_integer(v))
                menuGenre = spkyvcv::clamp_genre(int(json_integer_value(v)));
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

        // The places are parsed into the STAGING array, never into places[]:
        // this call can run on the UI thread (see the LIVE-module note below)
        // while controlTick reads places[pad].code through wakePad(). Seeded
        // from the live twelve first, so a patch carrying fewer than twelve --
        // or no "places" key at all, which is every patch written before this
        // key existed -- leaves the rest of them alone rather than blanking
        // them.
        for (int i = 0; i < spkyvcv::kPadCount; ++i) uiPlaces[i] = places[i];
        // A restore does not disown the live pad. commitPlaces() sets this too,
        // but the RESTORE path below applies the staged places without going
        // through it, and applyPlaces() reads the flag on both routes.
        uiForget = false;
        if (json_t* a = json_object_get(root, "places")) {
            if (json_is_array(a)) {
                for (int i = 0; i < spkyvcv::kPadCount
                                && i < int(json_array_size(a)); ++i) {
                    json_t* o = json_array_get(a, i);
                    if (!json_is_object(o)) continue;
                    json_t* c = json_object_get(o, "code");
                    if (json_is_string(c))
                        std::snprintf(uiPlaces[i].code, sizeof uiPlaces[i].code,
                                      "%s", json_string_value(c));
                    json_t* n = json_object_get(o, "name");
                    if (json_is_string(n))
                        spkyvcv::set_label(uiPlaces[i].name, spkyvcv::kNameCap,
                                           json_string_value(n));
                    json_t* t = json_object_get(o, "note");
                    if (json_is_string(t))
                        spkyvcv::set_label(uiPlaces[i].note, spkyvcv::kNoteCap,
                                           json_string_value(t));
                    // The base is assigned UNCONDITIONALLY for a place the
                    // patch actually holds, unlike the three fields above.
                    // Those keep the live value when the key is missing so a
                    // short "places" array leaves the rest alone; a base kept
                    // that way would be the PREVIOUS place's base wearing the
                    // loaded place's code and name. A patch written before
                    // this key existed therefore comes back with no base at
                    // all, which is exactly what it had.
                    json_t* b = json_object_get(o, "base");
                    spky::flow::BaseOverlay ov;
                    uiPlaces[i].has_base =
                        json_is_string(b) &&
                        spkyvcv::decode_base(json_string_value(b), ov);
                    uiPlaces[i].base = ov;   // empty when there is none
                }
            }
        }

        spkyvcv::GlowSave s;
        json_t* code = json_object_get(root, "terrain");
        if (!json_is_string(code)) {                    // nothing to restore
            commitPlaces();             // ...but the twelve still land
            return;
        }
        std::snprintf(s.code, sizeof s.code, "%s", json_string_value(code));
        if (json_t* u = json_object_get(root, "undo")) {
            if (json_is_string(u)) {
                std::snprintf(s.undo, sizeof s.undo, "%s", json_string_value(u));
                s.have_undo = true;
            }
        }
        // The live place's base and the undo slot's. A string that does not
        // decode leaves have_base false -- the terrain then plays as drawn,
        // which is the same answer a place with no base gets, and not a zeroed
        // patch. No key at all (every patch written before this round) is the
        // same answer again.
        if (json_t* b = json_object_get(root, "base")) {
            if (json_is_string(b))
                s.have_base = spkyvcv::decode_base(json_string_value(b), s.base);
        }
        if (json_t* b = json_object_get(root, "undoBase")) {
            if (json_is_string(b))
                s.have_undo_base =
                    spkyvcv::decode_base(json_string_value(b), s.undo_base);
        }
        // A patch written before this round may still carry a "lock" key. It
        // is ignored on purpose: the LOCK switch is the only thing that can
        // set that state, and Rack restores the switch param itself.
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
            //
            // The places go in outright here: nothing is running, and
            // onAdd()'s wakeHouse() fallback reads places[0].code a moment
            // later -- a staged handover would still be pending then and the
            // module would wake the house terrain instead of pad 1.
            commitPlaces();
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
            uiBase = plan.base;
            uiHaveBase = plan.have_base;
            uiUndoBase = plan.undo_base;
            uiHaveUndoBase = plan.have_undo_base;
            // RESTORE applies the staged places too, rather than raising a
            // second op: uiOp is one slot, so a SET_PLACES here would be
            // overwritten by this line and the loaded twelve would vanish.
            uiOp = UiOp::RESTORE;      // payload written above, flag written last
        } else {
            commitPlaces();            // malformed terrain: the twelve still land
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
        // wake() itself never touches the lock, and a house wake is a fresh
        // start. Belt and braces only: controlTick re-decides the lock from the
        // assigned switch on the next tick regardless (glow_ui.hpp, GlowSave).
        flow.set_lock(false);
        pads.live = 0;
        pads.excursion = false;
    }

    void reinit(float sr) {
        // Capture whatever the flow layer is holding: inst.init() below wipes
        // every setter, and Flow only re-pushes on wake(), which is the last
        // thing this function does.
        //
        // That includes both OVERLAYS. This is the one other path besides
        // dataFromJson that reconstructs a live Flow, and the live place's
        // hand-authored base is in neither places[] nor the terrain code -- so
        // nothing else could put it back. Waking without it (wake(st) defaults
        // ov to nullptr, flow.cpp:87) strips a loaded patch back to the drawn
        // terrain the first time somebody changes Rack's sample rate, in
        // silence. The undo slot's goes the same way twice over: wake() clears
        // it too (flow.cpp:96-97) and restore_undo re-clears it (flow.cpp:245).
        //
        // TWO captures, not one duplicated -- the same shape as `st` and `un`
        // above, which are two values for the same reason. The pair is one
        // value across wake/begin_blend/undo, but restore_undo assigns the
        // slot's on its own, and a loaded patch reaches this function having
        // gone through exactly that. Handing one captured overlay to both
        // calls below would overwrite the slot's with the live one: the same
        // silent loss this block exists to stop, moved one field over.
        //
        // By value, and copied BEFORE inst.init(): flow.overlay() hands back a
        // pointer into `flow`, which wake() is about to rewrite.
        const bool hadFlow = woken;
        const spky::flow::TerrainState st = flow.state();
        const spky::flow::TerrainState un = flow.undo_state();
        const bool haveUndo = flow.can_undo();
        spky::flow::BaseOverlay ov, unOv;
        bool haveOv = false, haveUnOv = false;
        if (const spky::flow::BaseOverlay* p = flow.overlay()) {
            ov = *p;
            haveOv = true;
        }
        if (const spky::flow::BaseOverlay* p = flow.undo_overlay()) {
            unOv = *p;
            haveUnOv = true;
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
        //
        // Nothing re-applies the lock here. On this branch Flow::init() did not
        // run, and it is the only thing besides set_lock() that writes
        // Flow::_locked (flow.cpp) -- so the lock was never lost to restore.
        // controlTick pushes the switch's answer a control period later in any
        // case; see glow_ui.hpp's GlowSave note.
        const float ctrlHz = sr / float(kCtrlDiv);
        if (!hadFlow) {
            flow.init(&inst, ctrlHz);
        } else {
            flow.set_ctrl_hz(ctrlHz);
            // force-pushes every setter into the freshly initialised
            // Instrument, base and all. Each overlay goes back to the verb it
            // came from, in flow.h's documented order.
            flow.wake(st, haveOv ? &ov : nullptr);
            flow.restore_undo(un, haveUndo, haveUnOv ? &unOv : nullptr);
        }
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        reinit(e.sampleRate);
    }

    void onReset() override {
        // Anything the UI thread staged and the audio thread has not applied
        // yet dies here: Initialize throws the state away, and a redraw or a
        // pin left in the slot would land on top of the fresh module a control
        // period later. Safe to write directly -- Initialize runs with the
        // engine held, so no controlTick is in flight.
        uiOp = UiOp::NONE;
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

        // The lock is exactly what the assigned switch says, and NOBODY owns
        // it when no switch is assigned -- so unassigned means unlocked, not
        // "keep whatever it was".
        //
        // Not a tidiness rule. The workshop menu can move both switches off
        // LOCK, and the menu's own lock toggle is deliberately gone (spec
        // §4.3: one control, one truth). Leaving the lock standing would leave
        // a locked generator with no control left that can clear it -- every
        // draw, every hold and every reroll refused, with Initialize as the
        // only way out. The cost is that a patch saved locked with no LOCK
        // switch assigned comes back unlocked; that state is the trap itself,
        // and restoring somebody into it is not a feature.
        const int swLockPos = switchPos(spkyvcv::SwitchTarget::LOCK);
        flow.set_lock(swLockPos >= 0 && spkyvcv::lock_switch(swLockPos));

        flow.set_genre(menuGenre.load());

        // Apply whatever the UI thread staged (fix round 3): flag read FIRST
        // via exchange() -- so at most one op survives even if two menu
        // actions landed in the same UI frame -- payload read only after.
        switch (uiOp.exchange(UiOp::NONE)) {
            case UiOp::SET_TERRAIN: flow.wake(uiState); woken = true; break;
            case UiOp::NEW_FULL:    if (!flow.new_full()) refuse.mark(flow.now_s()); break;
            case UiOp::NEW_PARTIAL: if (!flow.new_partial(uiMask)) refuse.mark(flow.now_s()); break;
            case UiOp::UNDO:        if (!flow.undo()) refuse.mark(flow.now_s()); break;
            case UiOp::SET_PLACES:  applyPlaces(); break;
            case UiOp::PIN:         pinCurrent(uiPin); break;
            case UiOp::RESTORE:
                applyPlaces();
                // Each overlay to the verb that owns its state, in flow.h's
                // documented order -- wake() clears the slot, so the slot's
                // own base goes in after it and not before. nullptr, never an
                // empty overlay: a place with no base plays the terrain as
                // drawn (flow.h, wake()).
                flow.wake(uiState, uiHaveBase ? &uiBase : nullptr);
                // No set_lock here: the push above already ran this tick, and
                // it is the switch's answer. See glow_ui.hpp's GlowSave note.
                flow.restore_undo(uiUndo, uiHaveUndo,
                                  uiHaveUndoBase ? &uiUndoBase : nullptr);
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
        // pads.update() moves `live` onto the pressed pad before we can know
        // whether that pad's code decodes, so a REFUSED wake has to put it
        // back. Rolled back, not cleared: live = -1 would be the honest reading
        // of "no pad's place is playing", but the red refusal collar is painted
        // on the live pad and on no other, so clearing it would refuse the
        // player in silence -- and nothing stopped, so the previously live pad
        // is still exactly the pad the module is on. Without this, the collar
        // went teal on a pad holding nothing the moment the flash expired,
        // while the old terrain kept sounding. wakeHouse() answers the same
        // question from the other end: when ITS wake fails it falls back to the
        // house terrain and leaves pad 0 live, because something is playing
        // there too.
        const int  prevLive = pads.live;
        const bool prevExcursion = pads.excursion;
        const spkyvcv::PadEvent ev = pads.update(down, t);
        if (ev.action == spkyvcv::PadAction::WAKE) {
            if (!wakePad(ev.pad)) {
                refuse.mark(t);
                pads.live = prevLive;
                pads.excursion = prevExcursion;
            }
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

// The generated tables pack every colour as 0xRRGGBB -- lettering (lblRgb,
// kTexts) and the pad collar (kCollar*) alike. One unpacker, so a widget can
// never disagree with the header about what a channel is.
static NVGcolor panelRGB(unsigned c) {
    return nvgRGB((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
}

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
            nvgFillColor(args.vg, panelRGB(c));
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

// A menu text field that hands its contents to a callback on Enter and closes
// the menu -- TerrainCodeField's sibling, for the two strings a place carries.
// ui::TextField itself has no commit behaviour.
//
// It does NOT sanitize or truncate. spkyvcv::set_label does both, it is the
// only way to write those fixed buffers, and it has to hold that contract for
// dataFromJson too -- a hand-edited patch reaches the same fields with no text
// field anywhere in sight. Doing it here as well was idempotent but left this
// widget carrying a `cap` whose only job was to redo the buffer owner's work,
// which is the kind of second copy that drifts.
struct LabelField : ui::TextField {
    std::function<void(const std::string&)> commit;
    void onSelectKey(const SelectKeyEvent& e) override {
        if (commit && e.action == GLFW_PRESS
            && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
            commit(text);
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

        // WHICH colour a state gets is decided here and only here, because
        // that is runtime state -- which is also why there are no LightIds for
        // the pads. WHAT the four colours are belongs to the panel: copper (the
        // part-B accent) is reserved for "that worked", a hold that actually
        // rerolled, and a refusal gets a colour of its own, because telling the
        // player "I refused you" is the only thing RefuseFlash exists for, and
        // both ways a PAD can earn one -- a hold that LOCK turned down, and a
        // pad whose stored code will not decode -- are exactly the moments
        // where a copper collar would read as the reroll that just did not
        // happen. The refusal red sits with MUTED in gen_panel's palette
        // (base.BRICK), not above COPPER; the other three ARE palette entries.
        // All four, and both stroke widths, come from the generated header, so
        // a palette retune moves the printed tiles and these collars together.
        NVGcolor collar;
        if (flash && live)    collar = panelRGB(kCollarRefused);
        else if (exc)         collar = panelRGB(kCollarExcursed);
        else if (live)        collar = panelRGB(kCollarLive);
        else                  collar = panelRGB(kCollarIdle);

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f,
                       box.size.y - 1.f, mm2px(kPadR));
        nvgStrokeColor(args.vg, collar);
        nvgStrokeWidth(args.vg, mm2px(live ? kCollarWLive : kCollarWIdle));
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
        // The line below indexes this table with arch_of(), and the Genre
        // submenu further down builds its list from it. Neither knows how many
        // archetypes the engine has.
        static_assert(sizeof kArchNames / sizeof *kArchNames
                          == spky::flow::ARCH_COUNT,
                      "kArchNames is indexed by arch_of(); give the new "
                      "archetype a name here");
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
        // bug waiting to be filed. controlTick pushes the switch's answer
        // every tick, including "unlocked" when no switch is assigned, so
        // there is no state here for a menu item to get out of step with.

        // --- the workshop (spec §6) --------------------------------------
        // Everything the board cannot do lives below this line. The board is
        // the stage; Rack is the workshop, and none of this ships to the
        // Touch. Every entry stages an op -- no handler here touches Flow or
        // places[] directly.
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Workshop"));

        menu->addChild(createMenuItem("Draw a new terrain", "", [m]() {
            m->uiOp = Glow::UiOp::NEW_FULL;
        }));

        // Kept rather than dropped with the NEW button: the pads only ever
        // call new_partial(0x3F), so without a caller for a real partial mask
        // that half of the Flow API would have no user left and would rot.
        menu->addChild(createSubmenuItem("Reroll one macro", "",
            [m](Menu* sub) {
                for (int i = 0; i < spky::flow::MACRO_COUNT; ++i) {
                    // PACE owns no story -- rerolling it cannot change PACE,
                    // and would only redraw every other macro's weather (the
                    // weather counter is the sum of all six). A menu entry
                    // that does something other than its label says is worse
                    // than a missing one. Spec 2026-08-12 §4.4.
                    if (i == spky::flow::M_PACE) continue;
                    sub->addChild(createMenuItem(kMacroNames[i], "", [m, i]() {
                        m->uiMask = uint8_t(1u << i);
                        m->uiOp = Glow::UiOp::NEW_PARTIAL;
                    }));
                }
            }));

        menu->addChild(createMenuItem("Undo terrain", "", [m]() {
            m->uiOp = Glow::UiOp::UNDO;
        }));

        // Index 0 is ARCH_ANY, hence the +1/-1: the constraint is "no
        // constraint" plus one entry per archetype, taken from the same table
        // the label above reads so the two cannot come to disagree.
        std::vector<std::string> genres = { "Any" };
        for (const char* n : kArchNames) genres.push_back(n);
        menu->addChild(createIndexSubmenuItem(
            "Genre (what a draw may pick)", genres,
            [m]() { return m->menuGenre.load() + 1; },
            [m](int i) { m->menuGenre = i - 1; }));

        // --- assignments (spec §4.3) --------------------------------------
        // The S-numbers are the board's own designators and match the tooltips
        // in the generated panel table; "left" and "right" are what the player
        // actually reaches for.
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Assignments"));
        static const std::vector<std::string> kFaderNames =
            { "Off", "Tempo", "Master" };
        static const std::vector<std::string> kSwitchNames =
            { "Off", "Lock", "Scale" };
        for (int i = 0; i < 2; ++i) {
            menu->addChild(createIndexSubmenuItem(
                i == 0 ? "Left fader (S36)" : "Right fader (S37)",
                kFaderNames,
                [m, i]() { return m->faderTarget[i].load(); },
                [m, i](int v) { m->faderTarget[i] = v; }));
        }
        for (int i = 0; i < 2; ++i) {
            menu->addChild(createIndexSubmenuItem(
                i == 0 ? "Left switch (S09/S10)" : "Right switch (S07/S08)",
                kSwitchNames,
                [m, i]() { return m->switchTarget[i].load(); },
                [m, i](int v) { m->switchTarget[i] = v; }));
        }

        // The scale list is the SWITCH's value, not a selector: the menu picks
        // what gets frozen, the switch decides whether anything is. Ordered by
        // kScaleKnobOrder so the list still runs calm to sharp.
        std::vector<std::string> scales;
        for (int i = 0; i < spky::SCALE_LIST_COUNT; ++i)
            scales.push_back(spky::SCALE_NAMES[spkyvcv::kScaleKnobOrder[i]]);
        menu->addChild(createIndexSubmenuItem(
            "Scale (what the SCALE switch fixes)", scales,
            [m]() {
                for (int i = 0; i < spky::SCALE_LIST_COUNT; ++i)
                    if (spkyvcv::kScaleKnobOrder[i] == m->menuScale.load())
                        return i;
                return 0;
            },
            [m](int i) { m->menuScale = spkyvcv::kScaleKnobOrder[i]; }));

        // --- the twelve places (spec §6.3) --------------------------------
        menu->addChild(new MenuSeparator);
        menu->addChild(createSubmenuItem("Places", "", [m](Menu* sub) {
            for (int i = 0; i < spkyvcv::kPadCount; ++i) {
                // "  [base]" for a pad carrying a hand-authored overlay -- see
                // PadQuantity, which prints the same marker for the same
                // reason. places[] is read unsynchronised here exactly as the
                // name is; the worst a torn read can do is show the previous
                // pin, and the tearing note above applyPlaces() says why that
                // is the accepted cost.
                const std::string title =
                    string::f("Pad %d", i + 1) +
                    (m->places[i].name[0] == '\0'
                         ? std::string() : "  " + std::string(m->places[i].name)) +
                    (m->places[i].has_base ? "  [base]" : "");
                sub->addChild(createSubmenuItem(title, "", [m, i](Menu* pm) {
                    // The pad number, not flow.state(): the encode happens on
                    // the audio thread, which owns both the terrain it reads
                    // and the place it writes.
                    pm->addChild(createMenuItem("Pin current terrain here", "",
                        [m, i]() {
                            m->uiPin = i;
                            m->uiOp = Glow::UiOp::PIN;
                        }));
                    pm->addChild(createMenuLabel("Name"));
                    auto* nf = new LabelField;
                    nf->box.size.x = 180.f;
                    nf->setText(m->places[i].name);
                    nf->commit = [m, i](const std::string& s) {
                        m->stagePlaces([&](spkyvcv::Place* p) {
                            spkyvcv::set_label(p[i].name, spkyvcv::kNameCap, s);
                        });
                    };
                    pm->addChild(nf);
                    pm->addChild(createMenuLabel("Note -- why it was kept"));
                    auto* tf = new LabelField;
                    tf->box.size.x = 180.f;
                    tf->setText(m->places[i].note);
                    tf->commit = [m, i](const std::string& s) {
                        m->stagePlaces([&](spkyvcv::Place* p) {
                            spkyvcv::set_label(p[i].note, spkyvcv::kNoteCap, s);
                        });
                    };
                    pm->addChild(tf);
                }));
            }
        }));

        // The other end of Fireflow's "Copy patch as flow base" (spec
        // 2026-08-11 §6). The clipboard holds an encode_base string -- the same
        // format as pool.tsv's base column and this module's JSON -- and the
        // pad it lands on recalls that patch's skeleton while the terrain keeps
        // supplying the story layer the six macros move.
        //
        // Staged through stagePlaces like every other UI-thread edit. Writing
        // places[] from here would race the audio thread, which owns that
        // array; the staged copy is a memcpy of trivially copyable bytes and
        // allocates nothing.
        //
        // The DECISION -- what a pad should become given whatever text is on
        // the clipboard -- is base_for_pad(), in flow_patch_bridge.hpp, where
        // the desktop suite gates all four of its answers. What is left here is
        // the Rack half: read the clipboard, say what is on it, stage the
        // result.
        //
        // Read ONCE, at menu-open time, and captured by value. It is what the
        // label below reports, so it had better be what the items paste; and it
        // keeps a GLFW call out of twelve handlers.
        {
            spky::flow::BaseOverlay clipBase{};
            bool clipHas = false;
            const bool usable = spkyvcv::base_for_pad(
                glfwGetClipboardString(APP->window->win), clipBase, clipHas);

            // A refused paste used to be silent, which in a modular rack is
            // indistinguishable from a broken one. The clipboard's state is
            // named here, above the pad list, and when there is nothing usable
            // on it the submenu is disabled rather than offering twelve pads
            // that would each do nothing.
            //
            // The middle case earns its warning: an empty base is a REAL answer
            // -- it is what a rejected transfer copies -- and pasting it CLEARS
            // whatever base the pad was carrying. That is the right behaviour
            // ("paste this patch here" should leave the pad holding what the
            // clipboard holds), but it is destructive, so it is announced
            // before it is clicked rather than discovered afterwards.
            int carried = 0;
            for (int p = 0; p < spky::flow::P_COUNT; ++p)
                if (clipBase.has[p]) ++carried;
            menu->addChild(createMenuLabel(
                !usable ? std::string("Clipboard: no flow base -- copy one from "
                                      "Fireflow's menu")
                : clipHas ? string::f("Clipboard: a flow base, %d parameters",
                                      carried)
                          : std::string("Clipboard: an EMPTY flow base -- "
                                        "pasting CLEARS the pad")));

            menu->addChild(createSubmenuItem("Paste patch onto pad", "",
                [m, clipBase, clipHas](Menu* sub) {
                    for (int i = 0; i < spkyvcv::kPadCount; ++i) {
                        // The marker earns the most here: this list is where a
                        // base gets overwritten, and "[base]" is the only
                        // warning that the pad about to be clicked is already
                        // carrying one.
                        const std::string title =
                            string::f("Pad %d", i + 1) +
                            (m->places[i].name[0] == '\0'
                                 ? std::string()
                                 : "  " + std::string(m->places[i].name)) +
                            (m->places[i].has_base ? "  [base]" : "");
                        sub->addChild(createMenuItem(title, "",
                            [m, i, clipBase, clipHas]() {
                                m->stagePlaces([&](spkyvcv::Place* p) {
                                    p[i].base     = clipBase;
                                    p[i].has_base = clipHas;
                                });
                            }));
                    }
                },
                !usable));
        }

        // The note is the perishable one: parent spec §4.3 defines it as "one
        // sentence: why it was kept", and that sentence exists only in the
        // seconds after the judgement. A later pass over a TSV does not write
        // it, which is why it is stored at all -- and exporting it here is the
        // whole point of storing it.
        menu->addChild(createMenuItem("Copy all twelve as pool.tsv", "", [m]() {
            const std::string tsv =
                spkyvcv::export_pool_tsv(m->places, spkyvcv::kPadCount);
            glfwSetClipboardString(APP->window->win, tsv.c_str());
        }));

        // "Reset", not "Redraw": the seed is the fixed kPoolSeed, so this draws
        // the SAME twelve every time. Its only observable effect is that every
        // pinned code, name and note is discarded and the factory twelve come
        // back -- one click, no confirmation, right under the export. The label
        // has to say that; a "redraw" promises something new.
        //
        // Draws into the staging array on THIS thread and hands the result over
        // -- Glow::drawTwelve() would write places[] and reset the gesture from
        // under the audio thread. `true` forgets the live pad: afterwards the
        // sounding terrain is no longer any of the twelve.
        menu->addChild(createMenuItem("Reset all twelve places (discards names "
                                      "and notes)", "", [m]() {
            m->stagePlaces([](spkyvcv::Place* p) {
                Glow::drawTwelveInto(p, Glow::kPoolSeed);
            }, true);
        }));
    }
};

Model* modelGlow = createModel<Glow, GlowWidget>("Glow");
