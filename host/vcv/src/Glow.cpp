// host/vcv/src/Glow.cpp
//
// FireFlow Glow (spec 6): six macro knobs, one NEW button and eight jacks
// over the portable engine/flow/ layer. The big Fireflow module is the
// full-control view of the same engine; this one is the flow machine.
//
// Everything that does not need a Rack type lives in glow_ui.hpp and is
// tested by the desktop suite. Every coordinate and label comes from
// generated_flow_panel.hpp. Nothing here is hand-placed.
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

using namespace spkyvcv::glow;

// The module indexes params[MOTION + m] with m a spky::flow::Macro, so the
// panel's first six params must BE the macro enum. res/test_flow_panel.py
// guards the panel side; this guards the C++ side.
static_assert(MOTION == spky::flow::M_MOTION, "panel macro order drifted");
static_assert(DENSITY == spky::flow::M_DENSITY, "panel macro order drifted");
static_assert(BRIGHT == spky::flow::M_BRIGHT, "panel macro order drifted");
static_assert(DIRT == spky::flow::M_DIRT, "panel macro order drifted");
static_assert(WANDER == spky::flow::M_WANDER, "panel macro order drifted");
static_assert(SPACE == spky::flow::M_SPACE, "panel macro order drifted");
static_assert(CV_DEN == CV_MOT + 1 && CV_DRT == CV_MOT + 3,
              "CV jacks must stay contiguous -- controlTick indexes CV_MOT + i");

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
    static constexpr int kCtrlDiv = 16;          // flow ticks at sr / 16
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

    Glow() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        for (const auto& c : kParamCtls) {
            if (c.kind == WK_MACRO)
                configParam(c.id, 0.f, 1.f, 0.5f, c.label);
            else
                configButton(c.id, c.label);
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
        return root;
    }

    void dataFromJson(json_t* root) override {
        if (!root) return;
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
        if (spkyvcv::glow_restore(flow, s)) { woken = true; knobs.primed = false; }
    }

    // Enter a terrain code by hand (context menu). Returns false and changes
    // nothing if the string is not a valid code.
    bool setTerrainCode(const std::string& text) {
        spky::flow::TerrainState st;
        if (!spky::flow::decode_code(text.c_str(), st)) return false;
        flow.wake(st);
        woken = true;
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
            spky::read_wav(asset::plugin(pluginInstance, "res/factory.wav"),
                           factoryNative, err);
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
    }

    void controlTick(float sr) {
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
        // terrain's own tempo by itself once the cable is pulled.
        float bpm = flow.param_now(spky::flow::P_TEMPO_BPM);
        if (clkPeriod > 1.f && sr > 0.f && clkSamples < sr * kClockTimeoutS) {
            const float measured = 60.f * sr / clkPeriod;
            if (measured >= 20.f && measured <= 400.f) bpm = measured;
        }
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
            if (c.kind == WK_MACRO)
                // Rack's stock knobs come in fixed sizes; RoundLargeBlackKnob
                // is 46 px ~ 15.6 mm, the nearest to the panel's 16 mm.
                // RoundBigBlackKnob (54 px ~ 18.3 mm) would overhang the
                // printed footprint by more than a millimetre a side.
                addParam(createParamCentered<RoundLargeBlackKnob>(pos, module, c.id));
            else
                addParam(createLightParamCentered<VCVLightBezel<GreenLight>>(
                    pos, module, c.id, NEW_L));
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

        // Share a terrain: the code is the whole state (spec 4), so copying
        // it out and pasting it in is the entire sharing story.
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
            [m](bool on) { m->flow.set_lock(on); }));
    }
};

Model* modelGlow = createModel<Glow, GlowWidget>("Glow");
