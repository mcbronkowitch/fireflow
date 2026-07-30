#pragma once
#include <array>
#include <cstdint>
#include "mod/super_modulator.h"
#include "pitch/quantizer.h"
#include "pitch/chord.h"
#include "parts/engine_iface.h"
#include "parts/test_tone_engine.h"
#include "synth/synth_engine.h"
#include "sampler/sampler_engine.h"
#include "fx/fx_util.h"
#include "fx/part_fx.h"
#include "util/math.h"
#include "Utility/dcblock.h"

namespace spky {

// A part = SuperModulator + selectable engine + 5 targets. Combines each lane's
// bipolar output with a stored per-target base + depth, gated by the target's
// active flag and the master DEPTH.
class Part {
public:
    void init(float sample_rate, uint32_t seed_base,
              float* echo = nullptr,
              SampleBuffer::Frame* sampler_mem = nullptr, size_t sampler_frames = 0);

    SuperModulator& mod() { return _mod; }
    const SuperModulator& mod() const { return _mod; }
    Quantizer& quant() { return _quant; }
    PartFx& fx() { return _fx; }
    const PartFx& fx() const { return _fx; }

    // Excitation bus source selection (spec 2026-07-26 body-resonator §6,
    // Task 10): patch state, not a performance control -- three checkboxes
    // in a context menu, never a panel knob or parameter id. Default: own
    // FLUX tape on, the sibling deck's dry tap off, audio input off, so an
    // unmodified BODY deck behaves exactly like Task 9 left it and two BODY
    // decks never couple unasked (spec: "SUB = 0 ... including the
    // cross-deck tap, so two BODY decks never couple unasked" -- true a
    // fortiori when the cross-deck source itself is off).
    void set_excitation_sources(bool tape, bool other_deck, bool audio_in) {
        _src_tape = tape;
        _src_deck = other_deck;
        _src_audio = audio_in;
    }
    // Pushed once per control block by Instrument (the only scope where both
    // parts are visible -- see instrument.cpp), holding the SIBLING part's
    // dry mono output from the PREVIOUS control block. Not for panel/host use.
    void set_other_deck_tap(float x) { _other_deck_tap = x; }
    // Observer only, for tests (Task 10 review round 2): the excitation bus
    // value actually pushed to _engine->set_excitation() at the last control
    // tick -- i.e. the enabled sources summed, THEN DC-blocked and
    // fast_tanh-clipped. Same idiom as overlap_eff()/color_eff() above: a
    // cache filled by _control_tick(), read back by a test, never written
    // from outside. Lets a test watch the post-clip signal directly instead
    // of inferring it from BodyVoice's render, which is what a DC-corner
    // claim needs (a probe frequency the render is sensitive to is a
    // different thing from a probe frequency the RAW bus decay is sensitive
    // to once resonator dynamics are in between).
    float excitation_eff() const { return _excitation_eff; }

    void set_depth(float d) { _depth = clampf(d, 0.f, 1.f); }
    void set_tune(float t)  { _tune = clampf(t, 0.f, 1.f); }
    // COLOR (spec 2026-07-17 chord-layer): 0 = single note (bit-identical),
    // sweeps to a 4-note diatonic chord. Live on the FLOW surface.
    // The knob is stored, not pushed: process() combines it with the MOTION
    // lane and hands the ChordBuilder the effective color (spec 2026-07-18
    // color-motion-target).
    void set_color(float c) { _color = clampf(c, 0.f, 1.f); }
    int  chord_size() const { return _chord.size(); }
    // The color actually handed to the ChordBuilder: the knob plus MOTION's
    // swing (spec 2026-07-18 color-motion-target). Equals the knob when
    // MOD = 0 or the MOTION target is inactive.
    float color_eff() const { return _color_eff; }
    // DENS in the sampler: the grain-overlap knob. Stored, not pushed --
    // _control_tick combines it with the MOTION lane and hands the sampler
    // the effective value, exactly as COLOR does.
    void set_sampler_overlap(float n) { _overlap = clampf(n, 0.f, 1.f); }
    float overlap_eff() const { return _overlap_eff; }
    void set_detune_cents(float c) { _detune_cents = c; }   // DRIFT tune tap; engine pitch only
    void set_target_active(int slot, bool on) { _active[slot] = on; }
    void set_target_base(int slot, float b)   { _base[slot] = clampf(b, 0.f, 1.f); }
    void set_target_depth(int slot, float d)  { _tdepth[slot] = clampf(d, 0.f, 1.f); }

    void set_fx_target_active(int slot, bool on) { _fx_active[slot] = on; }
    void set_fx_target_base(int slot, float b)   { _fx_base[slot] = clampf(b, 0.f, 1.f); }
    void set_fx_target_depth(int slot, float d)  { _fx_depth[slot] = clampf(d, 0.f, 1.f); }
    float fx_target_value(int slot) const;

    // --- engine selection (M2). Boot default: ENGINE_SYNTH. ---
    // Click-free: 4 ms SoftSwitch fade-out -> swap -> 4 ms fade-in; the swap
    // and state re-forwarding happen inside process() at the idle point.
    void set_engine(EngineId e);
    EngineId engine_id() const { return _engine_id; }

    // STEP/FLOW reaches both the lanes and the engine (drone rule)
    void set_step(bool on, int steps);
    bool flow() const { return !_step_on; }   // CHOKE: a FLOW drone is always "on"

    // Liefert true GENAU EINMAL nach einer FLOW->STEP-Flanke und loescht das
    // Flag dabei. Center konsumiert es im Control-Tick; Part selbst weiss
    // nichts vom Transport (dieselbe Regel wie beim COLOR-Push: Part schiebt
    // Werte, keine Politik).
    bool take_step_snap() { const bool w = _step_snap; _step_snap = false; return w; }
    // Reicht den Slot an die Sampler-Engine weiter, wenn dieses Deck auf
    // Sampler steht. Waehrend eines Engine-Wechsels zaehlt die ZIEL-Engine --
    // sonst verloere ein Wechsel "auf Sampler und auf STEP zugleich" die
    // Ausrichtung.
    void snap_sampler_cursor(int slot) {
        const EngineId e = _switching ? _pending_engine : _engine_id;
        if (e == ENGINE_SAMPLER) _sampler.snap_phrase_cursor(slot);
    }

    // PLAY tap (M6 wires the gesture; the engine sees an ordinary trigger)
    void trigger_manual();

    // CHOKE (spec 2026-07-16 choke-priority): while inhibited, a lane fire
    // produces no engine trigger and no gate pulse; the suppressed note's
    // STEP sustain is latched out of gate() too. trigger_manual() is a user
    // gesture and is deliberately NOT inhibited.
    void set_inhibit(bool on) {
        if (on == _inhibit) return;
        _inhibit = on;
        _engine->set_hold(on);   // FLOW drone ducks out / fades back in
    }
    float max_voice_env() const;   // 0 when idle or on the test-tone engine

    // VOICE edit layer - forwarded to every melodic engine directly, so edits
    // stick whichever engine is active. The sampler reinterprets each knob as
    // its cloud analogue (spec: "no dead knobs"), so one panel row serves all.
    // BODY reinterprets them again, resonator-side, through the SAME
    // SynthEngineT setters: ATTACK is the exciter's length, DECAY is damping
    // (ring time), RESONANCE is the exciter's character, SUB is the
    // excitation bus level, Detune is string spread plus mode stretch and
    // FILT is brightness (spec 2026-07-26 body-resonator, section 5, and the
    // setter comments on BodyVoice). Without these forwards those knobs would
    // simply be dead on a BODY deck.
    void set_voice_attack(float n)    { _synth.set_attack(n);    _wave.set_attack(n);    _body.set_attack(n);    _sampler.set_window_attack(n); }
    void set_voice_decay(float n)     { _synth.set_decay(n);     _wave.set_decay(n);     _body.set_decay(n);     _sampler.set_window_decay(n); }
    void set_voice_resonance(float n) { _synth.set_resonance(n); _wave.set_resonance(n); _body.set_resonance(n); _sampler.set_resonance(n); }
    // The visible SUB control is routed separately as GENE SIZE on a sampler,
    // while visible SOURCE becomes ORG through LANE_SOURCE. The independent,
    // widgetless Detune parameter has no sampler meaning. Keep melodic SUB and
    // Detune on Synth/Wave only; SamplerEngine::_sub_n and _detune_n default
    // to 0 and stay there until sampler cloud dispersion sets them.
    void set_voice_sub(float n)       { _synth.set_sub(n);       _wave.set_sub(n);       _body.set_sub(n); }
    void set_voice_detune(float n)    { _synth.set_detune(n);    _wave.set_detune(n);    _body.set_detune(n); }
    void set_voice_filt(float t)      { _synth.set_filt(t);      _wave.set_filt(t);      _body.set_filt(t);      _sampler.set_filt(t); }

    SamplerEngine& sampler() { return _sampler; }
    const SamplerEngine& sampler() const { return _sampler; }
    // Observer twin of sampler() above -- lets a test reach the Synth leg of
    // the melodic SUB/widgetless-Detune split directly (final-fixes pass,
    // Befund B) the same way sampler() already lets it reach the sampler leg.
    SynthEngine& synth() { return _synth; }
    const SynthEngine& synth() const { return _synth; }
    WaveEngine& wave() { return _wave; }
    const WaveEngine& wave() const { return _wave; }
    BodyEngine& body() { return _body; }
    const BodyEngine& body() const { return _body; }

    int active_voices() const {
        if (_engine_id == ENGINE_SYNTH) return _synth.active_voices();
        if (_engine_id == ENGINE_WAVE) return _wave.active_voices();
        if (_engine_id == ENGINE_BODY) return _body.active_voices();
        return 0;
    }
    float voice_env(int v) const {
        if (_engine_id == ENGINE_SYNTH) return _synth.voice_env(v);
        if (_engine_id == ENGINE_WAVE) return _wave.voice_env(v);
        // BodyEngine::voice_env is an ENERGY FOLLOWER, not an envelope (see
        // BodyVoice::env_value). max_voice_env() below therefore reads "how
        // much is this resonator ringing", which is the same thing the
        // envelope reading meant for the meter that consumes it, but do not
        // expect an AD shape from it.
        if (_engine_id == ENGINE_BODY) return _body.voice_env(v);
        return 0.f;
    }

    float target_value(int slot) const;
    float target_raw(int slot) const;          // base + mod*depth, unquantized
    float pitch_pre_quant() const;             // PITCH target + TUNE, pre-quantize
    float lane_output(int slot) const { return _mod.lane_output(slot); }
    bool  lane_fired(int slot) const  { return _mod.lane_fired(slot); }
    // GATE jack: the ~5 ms retrigger pulse, OR'd with the composed melodic
    // STEP note sustain (spec: rhythm-groove-design.md section 3). pitch_sustain()
    // is step-mode-qualified (false in FLOW), so FLOW stays pulse-only.
    bool  gate() const {
        return _gate_ctr > 0 || (!_note_suppressed && _mod.pitch_sustain());
    }
    float pitch_cv() const { return target_value(LANE_PITCH); }

    // advance mod one sample + engine + part FX; sends = post-FX x REVERB SEND
    // The input-carrying form is the real one; the two legacy overloads feed
    // silence, which keeps every existing caller and test bit-identical.
    //
    // Defined here rather than in part.cpp so that EVERY call site can inline
    // it, not just engine/instrument.cpp -- bench/workloads_instr.cpp's
    // deck_shell row calls part.process(...) directly, without an Instrument.
    // The out-of-line form paid a nine-register stmdb/ldmia pair, a vpush/vpop
    // of d8-d9 and a 28-byte frame once per sample (design
    // docs/superpowers/specs/2026-07-30-part-per-sample-design.md section 3.2,
    // read off the linked ELF).
    //
    // Bit-exact by construction: only the compilation site changed. The
    // statements below, their order, their branch structure and their
    // arithmetic are the ones part.cpp carried. What moved out of this body is
    // four COLD blocks -- the engine swap, the master-cycle push, the fire
    // path and the gate edge -- whose GUARDS are still here and whose bodies
    // are now out-of-line private methods in part.cpp. _control_tick() also
    // stays out of line there: it runs once per SynthEngine::kCtrlInterval
    // samples, so inlining it would add its whole body at every call site
    // without removing anything from the per-sample path.
    //
    // always_inline, not merely `inline`, because moving the definition into
    // this header was measured NOT to be enough on its own: with the four cold
    // blocks already lifted out, arm-none-eabi-g++ 10.2.1 at -O2 still emitted
    // one out-of-line copy (a 0x360-byte weak symbol) and left all ten call
    // sites in bench.elf calling it with `bl`, prologue and epilogue intact.
    // -Winline reported nothing, so the threshold it exceeded is not named
    // here. The attribute also turns a future growth of this body into a
    // compile error rather than a silent return to the out-of-line form.
    __attribute__((always_inline)) inline
    void process(float inL, float inR, float& outL, float& outR,
                 float& sendL, float& sendR) {
        _mod.process();

        // click-free engine switch: fade out (4 ms) -> swap -> fade in (4 ms).
        // At hold the multiplier is exactly 1.0, so unswitched runs stay
        // bit-identical (M1.6 bypass invariant).
        const float fade = _engine_fade.process();
        if (_switching && _engine_fade.is_idle()) _engine_swap();

        // forward the master-lane cycle length on change, not per sample
        const float hz = _mod.master_hz();
        if (hz != _last_master_hz && hz > 0.f) _push_master_cycle(hz);

        const bool fired = _mod.lane_fired(LANE_PITCH);
        if (fired) {
            _note_suppressed = _inhibit;
            if (!_inhibit) _gate_ctr = _gate_len;
        }
        if (_gate_ctr > 0) --_gate_ctr;

        // Raster, plus an event refresh: a PITCH fire samples the lane at that
        // exact sample, so a tick-stale pitch is not "late", it is the wrong
        // note. The refresh deliberately does not re-phase _ctrl_ctr -- the
        // alignment with the engine's own tick is the point. The two branches
        // are mutually exclusive (else if, not a second if) because
        // _control_tick() is not idempotent -- it advances Quantizer::process's
        // slew and re-evaluates ChordBuilder::set_color's zone hysteresis -- so
        // a sample that is both a raster tick and a fire must call it once, not
        // twice, or the glide double-steps.
        //
        // Two consequences worth knowing about, neither a bug:
        // - A fire refresh is an extra Quantizer::process call one sample after
        //   a raster tick. Quantizer::process's slew counts *calls*, and each
        //   call now spans SynthEngine::kCtrlInterval samples, so that refresh
        //   advances the glide by a full tick's worth. Bounded at one extra step
        //   per note and probably desirable, but not something the next reader
        //   should have to rediscover.
        // - The fire refresh only covers lane_fired(LANE_PITCH). SynthEngine's
        //   _auto_pending drone promise (synth_engine.cpp:243-245) also reads
        //   the chord surface, and a set_flow/set_hold transition landing
        //   mid-interval triggers against a surface up to 95 samples (~2 ms)
        //   stale. Musically negligible for rare knob transitions -- this is an
        //   accepted asymmetry, not something to fix here.
        if (_ctrl_ctr == 0) {
            _ctrl_ctr = SynthEngine::kCtrlInterval;
            _control_tick();
        } else if (fired) {
            _control_tick();
        }
        --_ctrl_ctr;

        if (fired && !_note_suppressed) _fire_trigger();

        // Composed gate, forwarded on edges only (see engine_iface.h). Computed
        // after _gate_ctr has been advanced, so it reflects THIS sample.
        const bool g = gate();
        if (g != _last_gate) _gate_edge(g);

        // Excitation bus, audio-in capture (spec §6, Task 10): write the mono
        // input into _audio_in_tap only on this control block's last sample
        // (_ctrl_ctr, already decremented above, has reached 0), mirroring
        // Instrument's _dry_tap capture (instrument.cpp) -- same micro-
        // optimisation, same non-claim: _control_tick() only ever READS this
        // once per block too (at the TOP of the next process() call), so an
        // unconditional write every sample would leave the identical value
        // sitting there at read time, just after 96 redundant writes (see
        // task-10-review.md finding 6). The one-control-block lag comes from the
        // read cadence, not from this guard.
        if (_ctrl_ctr == 0) _audio_in_tap = 0.5f * (inL + inR);

        _engine->process_in(inL, inR);
        _engine->process(outL, outR);
        outL *= fade;
        outR *= fade;

        _fx.process(outL, outR, sendL, sendR, _fxv);
    }
    void process(float& outL, float& outR, float& sendL, float& sendR) {
        process(0.f, 0.f, outL, outR, sendL, sendR);
    }
    void process(float& outL, float& outR) {
        float sl, sr;
        process(0.f, 0.f, outL, outR, sl, sr);
    }

private:
    // --- the per-sample hot state, declared first on purpose ---
    //
    // Everything process() above touches on every one of the 96 samples in a
    // control block, and nothing else. The object is ~24 KB, so a member
    // declared after the engines sits far enough past the object base that
    // arm-none-eabi-g++ reaches it by re-deriving a scaled base (an
    // `add.w rN, r0, #20480` on entry) and then a 32-bit ldr.w/str.w per touch
    // -- 22 such accesses per sample, measured on the linked ELF (design
    // docs/superpowers/specs/2026-07-30-part-per-sample-design.md section 3.2).
    // At the front of the object the same members are within the 5-bit byte /
    // 7-bit word immediate ranges of the 16-bit Thumb load/store encodings, and
    // they occupy 36 contiguous bytes instead of being spread across the object.
    //
    // Bit-exact by construction: this is a declaration reordering only. No
    // statement, branch or arithmetic operation changed anywhere. It is safe to
    // reorder because nothing in the tree depends on Part's layout -- there is
    // no memcpy/memset over a member range, no offsetof, no static_assert on an
    // offset or on sizeof(Part), no serialised snapshot (the VCV host's JSON is
    // keyed by name), no aggregate or designated initialisation of Part or of
    // any struct containing one (bench's InstrPartGroup is default-initialised
    // through SerialArena::emplace, which sizes itself from sizeof), and no cast
    // that reinterprets a Part as anything else. Part has no user-declared
    // constructor, and none of its in-class initialisers reads another member,
    // so construction order carries no information either.
    //
    // Field order inside the block is chosen for the encodings: the three bools
    // land in the ldrb/strb 5-bit immediate window, the three words in the
    // ldr/str 7-bit word window, and only one padding byte is spent.
    //
    // Whether this costs or saves cycles is not claimed here. It changes
    // encodings and data-cache locality, not instruction counts, and this
    // project settles cost with the bench, not by reading.
    bool           _last_gate = false;
    bool           _switching = false;
    bool           _note_suppressed = false;   // last fire was swallowed
    int            _ctrl_ctr = 0;              // control raster; see below
    int            _gate_ctr = 0;
    float          _last_master_hz = -1.f;
    SoftSwitch     _engine_fade;

    SuperModulator _mod;
    TestToneEngine _tone;
    IPartEngine*   _engine = nullptr;
    SynthEngine    _synth;
    WaveEngine     _wave;
    BodyEngine     _body;
    SamplerEngine  _sampler;
    EngineId       _engine_id = ENGINE_SYNTH;
    EngineId       _pending_engine = ENGINE_SYNTH;
    bool           _step_on = false;
    // STEP-Einstiegs-Snap (spec 2026-07-23 sampler-performance-fixes).
    // _step_seen unterscheidet die erste Beobachtung des Schalters von einer
    // echten Geste: Hosts pushen set_step jeden Tick, und _step_on bootet auf
    // false -- ein mit STEP an geladenes Patch erzeugte sonst beim ersten
    // Push eine Flanke und schnappte, ohne dass jemand etwas geschaltet haette.
    bool           _step_seen = false;
    bool           _step_snap = false;
    bool           _inhibit = false;

    IPartEngine* _engine_for(EngineId e) {
        switch (e) {
            case ENGINE_SYNTH:   return static_cast<IPartEngine*>(&_synth);
            case ENGINE_SAMPLER: return static_cast<IPartEngine*>(&_sampler);
            case ENGINE_WAVE:    return static_cast<IPartEngine*>(&_wave);
            case ENGINE_BODY:    return static_cast<IPartEngine*>(&_body);
            default:             return static_cast<IPartEngine*>(&_tone);
        }
    }

    // The sampler deck plays ONE pitch: the PITCH target, which with the lane
    // switched off is TUNE alone. Collapse any composed chord to that single
    // note before it reaches the engine.
    //
    // The sampler's grain cloud spreads a chord round-robin across grains --
    // one grain per note, cycling. On a synth that is a chord; on a granulated
    // recording it is the same material replayed at several transpositions at
    // once, which is a harmonizer, not a texture. With COLOR at its factory
    // 0.647 a freshly-flipped deck A granulated at four ratios spanning nearly
    // two octaves (+1.80, -3.20, +4.80, +21.13 semitones, measured), heard as
    // grains jumping octaves. This is what the morphagene-controls spec
    // found, and it still holds for the CHORD path described here: COLOR was
    // not part of the sampler's control surface THERE -- it was simply never
    // considered, and it reached pitch only through the chord surface the
    // way MOTION reached it through the octave scatter. The flatten below is
    // that spec's fix and is unchanged.
    //
    // COLOR is no longer inert on a sampler deck overall, though -- since
    // spec 2026-07-23 (cloud-dispersion) it reaches the grain cloud by a
    // different route, SamplerEngine::set_dispersion, pushed from
    // Part::_control_tick alongside this flatten. That is a second, separate
    // mechanism and does not reopen the chord path collapsed here.
    //
    // Done HERE rather than in SamplerEngine on purpose: the engine stays a
    // general granular engine that can spread a chord (its own tests still
    // cover that), and the INSTRUMENT decides a sampler deck has no melody.
    // Same layer, and the same reasoning, as switching LANE_PITCH off and
    // keeping melodic SUB and widgetless Detune off the sampler.
    int _flatten_for_sampler(float* chord, int nch) const {
        if (_engine_id != ENGINE_SAMPLER) return nch;
        chord[0] = _tg[LANE_PITCH];
        return 1;
    }

    // Everything the engine and FX consume at their own control rate -- see
    // the doc comment on the definition in part.cpp for the full contract
    // (what it does, why it is not idempotent, and how it stays
    // phase-aligned with SynthEngine's own control tick).
    void _control_tick();

    // The four cold blocks lifted out of process() above. Each one keeps its
    // GUARD at the call site in process() and its BODY out of line in
    // part.cpp -- the statements inside are unchanged and still run in the same
    // order relative to everything around them. Their cadences, which is why
    // they are the ones lifted: _engine_swap only on the sample a set_engine()
    // fade reaches idle, _push_master_cycle only when the master lane's rate
    // changes, _fire_trigger only on an unsuppressed PITCH fire, _gate_edge
    // only on a composed-gate edge.
    void _engine_swap();
    void _push_master_cycle(float hz);
    void _fire_trigger();
    void _gate_edge(bool g);

    // Control raster (_ctrl_ctr itself is declared with the per-sample hot
    // block at the top of this section). Both this counter and
    // SynthEngine::_ctrl_ctr init to 0
    // and advance once per call to their respective process(), so while both
    // run continuously they fire on the same samples (0, kCtrlInterval,
    // 2*kCtrlInterval, ...), and _control_tick() runs before _engine->
    // process() within that sample -- the order the per-sample code
    // delivered.
    //
    // set_engine() re-arms this counter to 0 on the sample its swap
    // completes (see _engine_swap(), part.cpp), so that sample is always
    // a raster tick: the freshly active engine gets set_targets()/
    // set_chord() then and there, not up to kCtrlInterval - 1 samples later
    // on its power-on defaults. A side effect worth knowing: if the swap
    // lands off the sample-0-based grid (0, kCtrlInterval, 2*kCtrlInterval,
    // ...), this counter's own future ticks permanently rebase to (swap
    // sample, swap sample + kCtrlInterval, ...) instead -- harmless for the
    // engine (nothing downstream cares which absolute grid the raster is
    // on), but it means code or tests that assume ticks land on the
    // original sample-0 grid need the swap to land on it too, or to compute
    // "on raster" relative to the swap sample instead.
    //
    // That re-arm does NOT re-align SynthEngine::_ctrl_ctr. That counter
    // only advances inside SynthEngine::process() (synth_engine.cpp), so it
    // freezes for as long as SynthEngine is the inactive engine and resumes
    // counting from the frozen value once swapped back in -- a *permanent*
    // phase offset against this counter, (samples SynthEngine was inactive)
    // mod kCtrlInterval, not a one-interval blip, and it does not self-heal.
    // set_targets()/set_chord() still land immediately (they write
    // _targets[]/_chord[] directly), but anything SynthEngine only
    // recomputes inside its own _update_control() -- cutoff, resonance,
    // pan/drift width, detune, attack/decay times, the live chord-surface
    // bloom/collapse -- keeps updating on that offset schedule, not on this
    // raster, until the next swap changes the offset again. The SoftSwitch
    // fade does not mask this: it is a 192-sample (4 ms) rise, so across one
    // kCtrlInterval window it reaches only ~0.5 gain, not zero.
    //
    // This alignment claim is about SynthEngine specifically -- TestToneEngine
    // has no control tick at all and reads t[LANE_PITCH] every sample, so
    // under this raster its pitch becomes a kCtrlInterval-sample staircase
    // too. That is acceptable (it is a diagnostic engine, not the audio
    // path), just not "aligned" in the sense the rest of this comment
    // describes.

    // Target cache: _control_tick() both fills it and pushes it to the
    // engine via set_targets() -- process() no longer pushes it itself, it
    // only reads _tg[LANE_PITCH] back out for the fire's chord build. Boot
    // values mirror SynthEngine::_targets so a push before the first tick
    // cannot hand the engine garbage.
    float _tg[LANE_COUNT] = { 0.f, 0.5f, 0.5f, 0.f, 0.8f };

    // FX target cache, filled at the control tick. PartFx smooths each value
    // over 2 ms, so the raster's steps never reach an FX parameter raw.
    // Boot values mirror _fx_base so the first block cannot push zeros.
    // Slot 1 (FXT_FLUX_TIME) is 0.5 because the BBD reads it as a geometric
    // multiplier on the clock with 0.5 == x1. The 0.4 it carried while the
    // target was retired and unread would put every deck 1.3x off its own
    // synced grid.
    float _fxv[FXT_COUNT] = { 0.3f, 0.5f, 1.f, 0.25f, 0.45f };

    PartFx         _fx;

    // Excitation bus (spec §6, Task 10). Source enables -- default matches
    // Task 9's shipped behaviour exactly (tape only). _other_deck_tap is
    // written by Instrument (set_other_deck_tap); _audio_in_tap is this
    // part's own doing, latched the same way Instrument latches _dry_tap --
    // see the capture point in process() above. _bus_dc is the POST-SUM DC
    // block spec §6 asks for, distinct from PartFx's own _tap_dc: Task 9's
    // clip makes tape_tap() safe for any caller, this one bounds the sum of
    // up to three such callers (task-10-brief-addendum.md section E). Both
    // stay; do not remove either.
    bool  _src_tape = true;
    bool  _src_deck = false;
    bool  _src_audio = false;
    float _other_deck_tap = 0.f;
    float _audio_in_tap = 0.f;
    daisysp::DcBlock _bus_dc;
    // Backing store for excitation_eff() (observer only) -- the exact value
    // handed to _engine->set_excitation() at the last control tick.
    float _excitation_eff = 0.f;

    // Modulation first is the shipped state (spec 2026-07-17 boot-targets):
    // all five targets boot active, with staggered depths — FILTER 0.55 (the
    // exponential cutoff dominates; the big sweep belongs to FILT), MOTION
    // 0.7 (width moves without pumping). Ear-tunable. M6 pads toggle _active.
    // Until then there is only one writer: watch for this the day M6 lands,
    // because host/vcv/src/Spotymod.cpp already calls
    // set_target_active(p, LANE_PITCH, ...) unconditionally every block (the
    // sampler pitch-hold gate, spec 2026-07-21 morphagene-controls) -- once a
    // pad can also toggle LANE_PITCH's _active, that per-block push will
    // silently overwrite whatever the pad just set. Harmless today only
    // because the pad doesn't exist yet.
    std::array<bool,  LANE_COUNT> _active { { true, true, true, true, true } };
    std::array<float, LANE_COUNT> _base   { { 0.5f, 0.5f, 0.5f, 0.5f, 0.8f } };
    std::array<float, LANE_COUNT> _tdepth { { 1.f, 0.55f, 1.f, 0.7f, 1.f } };

    // FX target row (boot: all modulation inactive, spec "Boot defaults").
    // Bases, by FxTargetId: GRIT_INT .3 | FLUX_TIME .5 | FX_MIX 1 | REV_SEND .25 | FLUX_FB .45
    // Slot 1 (FXT_FLUX_TIME) is 0.5 because the BBD reads it as a geometric
    // multiplier on the clock with 0.5 == x1. The 0.4 it carried while the
    // target was retired and unread would put every deck 1.3x off its own
    // synced grid.
    std::array<bool,  FXT_COUNT> _fx_active { { false, false, false, false, false } };
    std::array<float, FXT_COUNT> _fx_base   { { 0.3f, 0.5f, 1.f, 0.25f, 0.45f } };
    std::array<float, FXT_COUNT> _fx_depth  { { 1.f, 1.f, 1.f, 1.f, 1.f } };

    // Modulation may duck LEVEL to at most this fraction of its base — the
    // part breathes, it never vanishes (play-test rev 2026-07-17, ear-tunable).
    static constexpr float kLevelFloor = 0.4f;

    // COLOR is a third destination of the MOTION lane (spec 2026-07-18
    // color-motion-target): density pendles +/-1 zone around the knob, so a
    // phrase's stabs differ in size. Bipolar and ADDITIVE, so the reach stays
    // constant across the knob range and a barely-open knob can still rise
    // into chord territory. Both ear-tunable.
    //   kColorMod  swing amplitude at MOD = 1; the zones are 0.25 wide, so
    //              +/-0.2 crosses at most one edge in each direction.
    //   kColorGate knob travel over which the swing fades in. Below it the
    //              swing is scaled toward 0, so COLOR = 0 is structurally
    //              silent (multiplied by zero, not special-cased) and the
    //              chord layer's bit-identity guarantee survives untouched.
    static constexpr float kColorMod  = 0.2f;
    static constexpr float kColorGate = 0.01f;

    // DENS is a fourth destination of the MOTION lane (spec 2026-07-21
    // morphagene-controls), so the cloud breathes in density instead of
    // standing still. Bipolar and additive, same shape as kColorMod. No gate
    // twin to kColorGate: the sampler has no bit-identity guarantee to
    // protect at knob 0, and overlap 1 is a musical value, not an off state.
    // Ear-tunable.
    static constexpr float kOverlapMod = 0.2f;

    float _depth = 1.f;
    float _tune = 0.5f;
    float _detune_cents = 0.f;   // DRIFT detune, applied post-quantizer to the engine only
    float _color = 0.f;          // COLOR knob; effective color is computed in process()
    float _color_eff = 0.f;      // knob + MOTION swing, as last pushed to _chord
    float _overlap = 1.f;        // DENS knob; effective value computed in _control_tick
    float _overlap_eff = 1.f;    // knob + MOTION swing, as last pushed to _sampler
    // _gate_ctr, its per-sample twin, is declared with the hot block at the top
    // of this section; this one is read only on a fire.
    int   _gate_len = 240;   // ~5 ms @ 48k, recomputed in init()
    float _sr = 48000.f;

    Quantizer _quant;
    float     _pitch_q = 0.f;
    ChordBuilder _chord;
    uint16_t _chord_mask() const {
        return _quant.mode() == QuantMode::Chrom ? CHROM_MASK : _quant.scale_mask();
    }
};

} // namespace spky
