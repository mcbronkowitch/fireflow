// host/vcv/src/Glow.cpp
//
// FireFlow Glow (spec 6): six macro knobs, one NEW button and eight jacks
// over the portable engine/flow/ layer. The big Fireflow module is the
// full-control view of the same engine; this one is the flow machine.
//
// Everything that does not need a Rack type lives in glow_ui.hpp and is
// tested by the desktop suite. Every coordinate and label comes from
// generated_flow_panel.hpp. Nothing here is hand-placed.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "plugin.hpp"
#include "generated_flow_panel.hpp"
#include "glow_ui.hpp"
#include "sampler_ui.hpp"

// The portable engine core -- the same headers the render host uses.
#include "instrument.h"
#include "flow/flow.h"
#include "flow/gesture.h"
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
static_assert(CV_DEN == CV_MOT + 1 && CV_BRT == CV_MOT + 2 &&
              CV_DRT == CV_MOT + 3 && CV_SPC == CV_MOT + 4,
              "CV jacks must stay contiguous -- controlTick indexes CV_MOT + i");
// GENRE position 0 is ANY and 1..4 are the archetypes in enum order, so
// controlTick's `pos - 1` is only correct while ARCH_DRONE is 0. The six
// macro asserts above set the precedent for pinning arithmetic like this.
static_assert(spky::flow::ARCH_DRONE == 0,
              "GENRE's knob position -> archetype mapping assumes ARCH_DRONE == 0");

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
    spky::flow::Gesture gest;
    spkyvcv::KnobTracker knobs;
    spkyvcv::GestureBridge newBtn;
    spkyvcv::RefuseFlash refuse;

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
    dsp::SchmittTrigger clockTrig;
    float clkSamples = 0.f;                      // samples since the last edge
    float clkPeriod = 0.f;                       // samples between the last two
    static constexpr float kClockTimeoutS = 2.f; // spec 4: fall back to the
                                                 // terrain's own tempo
    bool woken = false;

    // dataFromJson() can run before onAdd() (patch load), when flow has no
    // Instrument yet -- stash the payload here and apply it from onAdd()
    // instead of pushing into a null Instrument.
    spkyvcv::GlowSave pending;
    bool havePending = false;

    // --- UI-thread -> audio-thread staging (fix round 3) ------------------
    // appendContextMenu's "Paste terrain code", TerrainCodeField's Enter
    // commit and the "Terrain lock" toggle all run on Rack's UI thread, but
    // Flow is otherwise only ever touched from controlTick() on the audio
    // thread. Flow::wake() is not a small write (see flow.cpp): it rewrites
    // _terrain, _prev_terrain, re-seeds the sequencer and force-pushes every
    // parameter, so a direct write here could hand the audio thread a
    // half-written terrain mid-tick. dataFromJson()'s LIVE-module branch
    // (curSr > 0, reached via right-click preset load / module paste, which
    // hold no engine lock) has the same problem and is staged the same way.
    //
    // Follow Fireflow.cpp's resyncReq shape: the UI thread fills a payload
    // and sets an atomic flag LAST; controlTick() takes the flag FIRST (via
    // exchange(), so at most one op survives to be applied) and only then
    // reads the payload. Both sides use the atomic's default sequential
    // consistency -- no relaxed/acquire-release hand-rolling -- so a flag
    // the audio thread observes as non-NONE is guaranteed to happen-after
    // the UI thread's payload write.
    enum class UiOp { NONE, SET_TERRAIN, SET_LOCK, RESTORE };
    std::atomic<UiOp> uiOp { UiOp::NONE };
    spky::flow::TerrainState uiState;   // SET_TERRAIN, RESTORE
    spky::flow::TerrainState uiUndo;    // RESTORE
    bool uiHaveUndo = false;            // RESTORE
    bool uiLock = false;                // SET_LOCK, RESTORE

    // The ROOT override (spec 2026-08-07 §3.1), set from appendContextMenu on
    // the UI thread and read every controlTick on the audio thread. Atomic
    // rather than a plain int, and NOT a UiOp: UiOp is a one-shot exchange for
    // an operation, this is a standing value. Default sequential consistency,
    // matching uiOp -- no relaxed/acquire-release hand-rolling.
    std::atomic<int> rootOverride { -1 };     // -1 = AUTO

    // GENRE / SCALE. Both are snapped switches whose FIRST position is the
    // random one -- ANY draws the archetype at random, AUTO takes whatever
    // the terrain drew -- so the player selects randomness at the control.
    // That is exactly why neither joins Rack's Randomize: configSwitch leaves
    // randomizeEnabled at its default true (configButton is the one that
    // clears it), and letting Randomize pin the instrument to Whole tone
    // would remove a choice rather than add one.
    void configSel(const PanelCtl& c) {
        std::vector<std::string> labels;
        if (c.id == GENRE) {
            labels = { "Any", "Drone", "Pulse", "Arp", "Fragment" };
        } else {
            labels.push_back("Auto");
            for (int i = 0; i < spky::SCALE_LIST_COUNT; ++i)
                labels.push_back(spky::SCALE_NAMES[spkyvcv::kScaleKnobOrder[i]]);
        }
        configSwitch(c.id, 0.f, float(labels.size() - 1), 0.f, c.tip, labels);
        if (auto* pq = paramQuantities[c.id]) pq->randomizeEnabled = false;
    }

    Glow() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        for (const auto& c : kParamCtls) {
            switch (c.kind) {
                case WK_MACRO:
                    // The six macro knobs stay on c.label, matching
                    // Fireflow.cpp -- deliberate, not an oversight.
                    configParam(c.id, 0.f, 1.f, 0.5f, c.label);
                    break;
                case WK_BTN:
                    // The NEW button's whole interaction model lives in one
                    // control, so its Rack tooltip should be the panel's full
                    // gesture-table string (c.tip), not just "NEW" (c.label).
                    configButton(c.id, c.tip);
                    break;
                case WK_SEL:
                    configSel(c);
                    break;
                default: break;
            }
        }
        for (const auto& c : kInputCtls)  configInput(c.id, c.tip);
        for (const auto& c : kOutputCtls) configOutput(c.id, c.tip);
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            fxmem.bbd[p][0] = bbd[p][0];
            fxmem.bbd[p][1] = bbd[p][1];
        }
        fxmem.reverb = &reverb;
        ctrlDiv.setDivision(kCtrlDiv);
    }

    // --- persistence (spec 5) --------------------------------------------
    // A live set built around a locked terrain has to survive a restart, so
    // a patch carries the whole state: the terrain code, the lock and the
    // one undo slot. Preset *systems* -- banks, slots, favourites -- are out
    // of scope (spec 8); this is the baseline.
    json_t* dataToJson() override {
        const spkyvcv::GlowSave s = spkyvcv::glow_capture(flow);
        json_t* root = json_object();
        json_object_set_new(root, "terrain", json_string(s.code));
        json_object_set_new(root, "lock", json_boolean(s.lock));
        if (s.have_undo) json_object_set_new(root, "undo", json_string(s.undo));
        json_object_set_new(root, "root", json_integer(rootOverride.load()));
        return root;
    }

    void dataFromJson(json_t* root) override {
        if (!root) return;
        // Read BEFORE the terrain's early return below: the override is
        // independent of the terrain, and a patch whose code is missing or
        // malformed must not lose it as collateral.
        if (json_t* r = json_object_get(root, "root")) {
            const int v = int(json_integer_value(r));
            rootOverride = (v >= 0 && v <= 11) ? v : -1;
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
            knobs.primed = false;
        } else if (!woken) {
            wakeHouse();
        }
    }

    void wakeHouse() {
        spky::flow::TerrainState st;
        if (!spky::flow::decode_code(spky::flow::kHouseCode, st)) st = {};
        flow.wake(st);
        // wake() itself never touches the lock (it only clears the undo
        // slot), so without this a locked module would land on the house
        // terrain still locked at all three call sites: a fresh onAdd (a
        // no-op there, already unlocked), onReset() (spec 5's own
        // requirement -- Initialize must return unlocked), and the
        // pending-restore fallback in onAdd when a saved code is malformed
        // (a corrupt patch must not strand the player locked on top of it).
        flow.set_lock(false);
        woken = true;
        knobs.primed = false;
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
        // NEW press, and there is no player gesture that could load it later.
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
        reinit(curSr > 0.f ? curSr : 48000.f);
        wakeHouse();
        knobs.primed = false;
        // Params are reset for us by Rack's default ResetEvent handler before
        // this runs, which covers GENRE and SCALE. rootOverride is not a
        // param, so Initialize would otherwise leave it stale.
        rootOverride = -1;
    }

    void controlTick(float sr) {
        // Host-owned settings first: wake() below force-pushes every
        // parameter, so an override applied after it would miss that push and
        // let one tick out on the terrain's own tonality.
        const int gpos = int(params[GENRE].getValue() + 0.5f);
        flow.set_genre(gpos <= 0 ? spky::flow::ARCH_ANY : gpos - 1);
        flow.set_scale_override(
            spkyvcv::scale_of_knob(int(params[SCALE].getValue() + 0.5f)));
        flow.set_root_override(rootOverride.load());

        // Apply whatever the UI thread staged (fix round 3): flag read FIRST
        // via exchange() -- so at most one op survives even if two menu
        // actions landed in the same UI frame -- payload read only after.
        switch (uiOp.exchange(UiOp::NONE)) {
            case UiOp::SET_TERRAIN:
                flow.wake(uiState);
                woken = true;
                break;
            case UiOp::SET_LOCK:
                flow.set_lock(uiLock);
                break;
            case UiOp::RESTORE:
                flow.wake(uiState);
                flow.set_lock(uiLock);
                flow.restore_undo(uiUndo, uiHaveUndo);
                woken = true;
                knobs.primed = false;
                break;
            case UiOp::NONE: default: break;
        }

        float k[spky::flow::MACRO_COUNT];
        for (int m = 0; m < spky::flow::MACRO_COUNT; ++m) {
            k[m] = params[MOTION + m].getValue();
            flow.set_macro(m, k[m]);
        }
        for (int i = 0; i < 5; ++i) {
            Input& in = inputs[CV_MOT + i];
            flow.set_cv(spkyvcv::kCvMacro[i],
                        in.isConnected() ? spkyvcv::cv_to_macro(in.getVoltage())
                                         : 0.f);
        }

        // --- the NEW gesture family (spec 5) ----------------------------
        // The decoder's clock is Flow's own, which advances one dt per
        // tick(); reading it before the tick means every event this pass
        // carries the same timestamp, which is exactly what a control tick
        // is.
        const double t = flow.now_s();
        float d[spky::flow::MACRO_COUNT];
        if (knobs.deltas(k, d))
            for (int m = 0; m < spky::flow::MACRO_COUNT; ++m)
                if (d[m] > 0.f) gest.knob_delta(m, d[m], t);

        const bool down = params[NEW_BTN].getValue() > 0.5f;
        if (newBtn.edge(down)) gest.button(down, t, flow.locked());
        gest.tick(t, flow.can_undo());

        bool refused = false;
        const spky::flow::GestureOut op = gest.poll();
        switch (op.op) {
            case spky::flow::GestureOut::NEW_FULL:
                refused = !flow.new_full(); break;
            case spky::flow::GestureOut::NEW_PARTIAL:
                refused = !flow.new_partial(op.mask); break;
            case spky::flow::GestureOut::UNDO:
                refused = !flow.undo(); break;
            case spky::flow::GestureOut::LOCK_TOGGLE:
                flow.set_lock(!flow.locked()); break;
            case spky::flow::GestureOut::REFUSED:
                refused = true; break;
            default: break;
        }
        // A press the decoder let through but Flow turned down (nothing to
        // undo, an empty macro mask) must still light the refusal, or the
        // panel would silently swallow a gesture. gesture.h's own REFUSED
        // only covers the locked case, which is why Flow's verbs return
        // bool. gesture.h's own _refuse_t is only reachable from a real
        // release edge, so it can never be set from here (fix round 1) --
        // this flash is the module's own, since only the module knows Flow
        // declined.
        if (refused) refuse.mark(t);

        flow.tick();

        // Tempo: the terrain owns it and Flow pushes it, but an external
        // clock overrides (spec 4). Set it unconditionally every tick --
        // Flow caches what it last pushed, so it would NOT restore the
        // terrain's own tempo by itself once the cable is pulled. The
        // override rule itself is pure logic (fix round 4) and lives in
        // glow_ui.hpp, covered by tests/test_glow_ui.cpp -- this is just the
        // call site.
        const float bpm = spkyvcv::clock_bpm(flow.param_now(spky::flow::P_TEMPO_BPM),
                                              clkPeriod, clkSamples, sr,
                                              kClockTimeoutS);
        inst.set_tempo_bpm(bpm);

        // The module's own refusal flash takes precedence over the
        // decoder's own LED state -- gest.led()'s own precedence (REFUSE >
        // UNDO_ARMED > MARKED > BLEND > LOCKED > IDLE) stays intact for
        // every other case; this only substitutes the top of it.
        const int led = refuse.active(flow.now_s())
                             ? spky::flow::Gesture::LED_REFUSE
                             : gest.led(flow.blend_phase(), flow.locked());
        lights[NEW_L].setBrightness(
            spkyvcv::led_level(led, flow.blend_phase(), flow.now_s()));
    }

    void process(const ProcessArgs& args) override {
        if (args.sampleRate != curSr) reinit(args.sampleRate);

        if (inputs[CLK].isConnected()) {
            clkSamples += 1.f;
            if (clockTrig.process(inputs[CLK].getVoltage(), 0.1f, 1.f)) {
                if (clkSamples > 1.f) clkPeriod = clkSamples;
                clkSamples = 0.f;
                inst.clock_pulse();
            }
        } else {
            clkPeriod = 0.f;
        }

        if (ctrlDiv.process()) controlTick(args.sampleRate);

        float outl = 0.f, outr = 0.f;
        inst.process(nullptr, nullptr, &outl, &outr, 1);
        outputs[OUT_L].setVoltage(clamp(outl, -1.f, 1.f) * 5.f);
        outputs[OUT_R].setVoltage(clamp(outr, -1.f, 1.f) * 5.f);
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
        for (const auto& c : kInputCtls)
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
                    // Rack's stock knobs come in fixed sizes;
                    // RoundLargeBlackKnob is 46 px ~ 15.6 mm, the nearest to
                    // the panel's 16 mm. RoundBigBlackKnob (54 px ~ 18.3 mm)
                    // would overhang the printed footprint by more than a
                    // millimetre a side.
                    addParam(createParamCentered<RoundLargeBlackKnob>(
                        pos, module, c.id));
                    break;
                case WK_BTN:
                    addParam(createLightParamCentered<VCVLightBezel<GreenLight>>(
                        pos, module, c.id, NEW_L));
                    break;
                case WK_SEL:
                    // 11 mm printed footprint; RoundBlackKnob is 38 px ~ 12.9
                    // mm, RoundSmallBlackKnob 28 px ~ 9.5 mm -- the smaller
                    // one stays inside the print.
                    addParam(createParamCentered<RoundSmallBlackKnob>(
                        pos, module, c.id));
                    break;
                default: break;
            }
        }
        for (const auto& c : kInputCtls)
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(c.mm.x, c.mm.y)),
                                                     module, c.id));
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
        // and the point of the GENRE control is auditioning one archetype at
        // a time -- without this the player cannot see which one they are in.
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

        menu->addChild(createBoolMenuItem(
            "Terrain lock", "",
            [m]() { return m->flow.locked(); },
            [m](bool on) {
                // Stage for controlTick() to apply -- set_lock() itself is a
                // single bool write with no crash risk, but going through
                // the same staging path as the other two writers (fix round
                // 3) keeps every Flow mutation on the audio thread, with one
                // ordering rule instead of a special case for this one.
                m->uiLock = on;
                m->uiOp = Glow::UiOp::SET_LOCK;
            }));
    }
};

Model* modelGlow = createModel<Glow, GlowWidget>("Glow");
