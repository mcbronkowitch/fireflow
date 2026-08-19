#include "parts/part.h"
#include "util/fast_tanh.h"
#include <cmath>

using namespace spky;

// The mod tick must ride the same raster the engine control tick uses --
// Part::_control_tick() reads texture lane outputs the sample they are
// produced (spec 2026-07-19 mod-plane-control-rate).
static_assert(ModLane::kTickInterval == SynthEngine::kCtrlInterval,
              "mod tick interval must equal the engine control raster");

void Part::init(float sample_rate, uint32_t seed_base,
                float* echo_l, float* echo_r,
                SampleBuffer::Frame* sampler_mem, size_t sampler_frames,
                float* bbd_l, float* bbd_r) {
    _sr = sample_rate;
    _mod.init(sample_rate, seed_base);
    _tone.init(sample_rate);
    _synth.set_seed(seed_base ^ 0x5eedC0DEu);   // per-part drift decorrelation
    _synth.init(sample_rate);
    _wave.set_seed(seed_base ^ 0x57415645u);    // "WAVE", distinct melodic drift
    _wave.init(sample_rate);
    _body.set_seed(seed_base ^ 0x424F4459u);    // "BODY", own drift + string noise
    _body.init(sample_rate);
    _bbd.init(sample_rate);
    _bbd.init_buffers(bbd_l, bbd_r, BbdEngine::kCells);
    _feed.set_seed(seed_base ^ 0x46454544u);    // "FEED", distinct individual
    _feed.init(sample_rate);
    _sampler.set_seed(seed_base ^ 0x5A11E20Du);
    _sampler.set_memory(sampler_mem, sampler_frames);
    _sampler.init(sample_rate);
    _last_gate = false;
    _engine_id = ENGINE_SYNTH;                  // boot default (M2 spec)
    // On a SAMPLER deck the PITCH lane is a read position, and on a BBD deck in
    // FLOW it is the continuous clock bend -- on both, the sweep IS the feature
    // and a slot sequencer would make it a staircase. Pushed at the two sites
    // where _engine_id is written, the same convention _engine_wants_in
    // follows, so the two cannot drift apart.
    //
    // This names the same two engines as the quantizer bypass in
    // _control_tick (SAMPLER || (BBD && !_step_on)) for the same underlying
    // reason -- on those decks the PITCH lane is not a note -- but it does NOT
    // derive from it. Do not move this to that per-tick site.
    _mod.set_flow_melody(_engine_id != ENGINE_SAMPLER && _engine_id != ENGINE_BBD);
    _pending_engine = _engine_id;
    _switching = false;
    _engine = _engine_for(_engine_id);
    _engine_wants_in = _engine->consumes_input();   // pairs with _engine
    _engine_fade.init(sample_rate);
    _engine_fade.set_on(true, true);            // boot: engine fully on
    _step_on = false;
    // init() means "no prior observation of the switch exists" -- that is
    // exactly what _step_seen = false encodes. The VCV host calls init()
    // again mid-session (audio-device / sample-rate change), and if
    // _step_seen survived a reinit while STEP stayed physically held, the
    // next set_step(true, ...) push -- the same switch state, not a gesture
    // -- would read as a rising edge and fake a snap (spec 2026-07-23
    // sampler-performance-fixes, review finding on a5751f3).
    _step_seen = false;
    _step_snap = false;
    _engine->set_flow(true);                    // lanes boot in FLOW -> drone
    _last_master_hz = -1.f;                     // force a cycle forward on
                                                // the first process()
    _fx.init(sample_rate, echo_l, echo_r);
    _gate_len = static_cast<int>(sample_rate * 0.005f);
    _gate_ctr = 0;
    _inhibit = false;
    _note_suppressed = false;
    // Excitation bus (spec §6, Task 10): _src_tape/_src_deck/_src_audio are
    // PATCH state, same footing as _active/_base/_tdepth above -- neither
    // group is touched here, so a reinit (VCV sample-rate change) does not
    // silently discard a live patch's source selection. _other_deck_tap and
    // _audio_in_tap are runtime-derived instead (same footing as _gate_ctr
    // just above), so they DO reset -- a reinit must not let stale
    // pre-reinit audio leak into the first post-reinit control tick.
    _other_deck_tap = 0.f;
    _audio_in_tap = 0.f;
    // _bus_dc.Process() only ever runs from _control_tick(), i.e. once per
    // SynthEngine::kCtrlInterval samples (500 Hz at 48 kHz) -- NOT once per
    // sample. daisysp::DcBlock::Init(rate) sizes its pole from the rate it is
    // told Process() will be called at (dcblock.cpp: gain_ = 1 - 10/rate), so
    // it must be told the CALL rate, sample_rate / kCtrlInterval, not the
    // audio sample rate itself (task-10-review.md finding 1: at sample_rate
    // the corner lands near 0.017 Hz, a ~9.6 s time constant, instead of the
    // intended ~1.6 Hz). Contrast with PartFx::_tap_dc (part_fx.cpp), which
    // IS Process()ed every sample and is correctly Init(sample_rate) there --
    // do not "fix" one to match the other, they run at genuinely different
    // rates.
    _bus_dc.Init(sample_rate / static_cast<float>(SynthEngine::kCtrlInterval));
    _ctrl_ctr = 0;                              // first process() runs a tick
    _quant.init(sample_rate, SynthEngine::kCtrlInterval);   // slew in ticks
    _pitch_q = _quant.process(pitch_pre_quant());
    _chord.init();
}

// The modulation term alone: what the lanes add to the knob, before the base
// and before any clamping. target_raw() below adds the base to exactly this,
// and the LED law displays exactly this (spec 2026-08-16 §3.1) -- one
// expression, so the display and the audio path cannot drift apart.
float Part::_mod_term(int slot) const {
    float d = (slot == LANE_PITCH) ? 1.f : _depth;
    if (slot == LANE_SOURCE && _engine_id == ENGINE_SAMPLER)
        d = std::pow(d, sampler_cfg::kSourceModExp);
    return _active[slot] ? _mod.lane_output(slot) * d * _tdepth[slot] : 0.f;
}

float Part::target_raw(int slot) const {
    // Master MOD (ex-DEPTH) shapes the texture only; the PITCH lane is the
    // anchor and keeps its per-slot depth alone (spec 2026-07-17 mod-tide).
    // Die SOURCE-Lane ist auf einem Sampler-Deck die LESEPOSITION und greift
    // ueber die gesamte Aufnahme -- linear warf schon eine Prise MOD die
    // Position durchs Material (spec 2026-07-23 sampler-performance-fixes).
    // Bei MOD 1 unveraendert (1^n == 1); gebogen wird nur der untere Weg.
    //
    // Gilt in BEIDEN Modi: _targets[LANE_SOURCE] speist ueber _base_pos()
    // auch die Slice-Basisposition in STEP. Denselben Regler je nach Modus
    // verschieden tief wirken zu lassen waere genau die versteckte Kopplung,
    // die die FEEL-Spec abgeschafft hat.
    // Reads _engine_id here, NOT _pending_engine during a switch -- the
    // opposite of snap_sampler_cursor's rule (part.h), which counts the
    // target engine mid-switch. Deliberate, not an oversight: during the
    // fade the OLD engine is what is actually sounding, so shaping SOURCE's
    // MOD curve by the currently-sounding engine is defensible here even
    // though the two rules diverge.
    float mod = _mod_term(slot);
    float v = clampf(_base[slot] + mod, 0.f, 1.f);
    // LEVEL floor (play-test rev 2026-07-17): modulation may duck the part to
    // at most 40% of its set level, never into silence. Relative to the base,
    // so a hand-muted part (base 0) stays silent; FILT's deliberate fade and
    // the other slots are untouched.
    const float floor_v = kLevelFloor * _base[slot];
    if (slot == LANE_LEVEL && v < floor_v) v = floor_v;
    return v;
}

// PITCH target + TUNE offset (a bipolar +/-18-semi transpose, 0.5 = neutral).
// On a SYNTH part the sum is quantized afterwards, so the final audible pitch
// lands on the scale grid and both parts share one grid. On a SAMPLER part it
// is used as-is -- see the quantizer comment in _control_tick.
float Part::pitch_pre_quant() const {
    return clampf(target_raw(LANE_PITCH) + (_tune - 0.5f), 0.f, 1.f);
}

float Part::target_value(int slot) const {
    return slot == LANE_PITCH ? _pitch_q : target_raw(slot);
}

// Same combine rule as target_raw, tapped from the SAME lanes — the FX breathe
// in the part's own character. Never quantized (that is a PITCH-lane concern).
float Part::fx_target_value(int slot) const {
    float mod = _fx_active[slot]
        ? _mod.lane_output(slot) * _depth * _fx_depth[slot] : 0.f;
    return clampf(_fx_base[slot] + mod, 0.f, 1.f);
}

void Part::set_engine(EngineId e) {
    if (_switching ? e == _pending_engine : e == _engine_id) return;
    _pending_engine = e;
    _switching = true;
    _engine_fade.set_on(false);   // fade out; process() swaps at the idle point
}

void Part::set_step(bool on, int steps) {
    // Die steigende Flanke ist die Geste, nicht der Zustand: der Host pusht
    // hier jeden Control-Tick denselben Wert.
    if (_step_seen && on && !_step_on) _step_snap = true;
    _step_seen = true;
    _step_on = on;
    _mod.set_step(on, steps);
    _engine->set_flow(!on);
}

void Part::trigger_manual() {
    _gate_ctr = _gate_len;
    float chord[ChordBuilder::kMaxNotes];
    const int n = _chord.build(target_value(LANE_PITCH), _chord_mask(),
                               _quant.root_semis(), chord);
    // A manual strike is an anchor by definition, not a groove note -- it
    // gets accent 0 (full strength) regardless of whatever accent the
    // sequencer's last STEP fire left in the engine. Same precedent as CHOKE:
    // trigger_manual() is a user gesture and is deliberately NOT subject to
    // the sequencer's policy (see the CHOKE comment on this method in
    // part.h). Without this push, a TRIG press landing while the engine held
    // a high accent struck at a fraction of full velocity and a shorter
    // decay -- measured: a press at _accent == 0.857143 differed from the
    // same press with the accent cleared in 9806/24000 rendered samples, max
    // |d| 0.0369 (spec 2026-08-15-step-accent-design.md section 4).
    _engine->set_accent(0.f);
    // Durch _flatten_for_sampler, genau wie der Fire-Pfad in
    // _fire_trigger(). Ohne das landeten bei COLOR > 0 bis zu vier Toene in
    // der SamplerEngine, bis der naechste _control_tick (<= 96 Samples) ueber
    // set_chord korrigiert -- weit genug fuer rund ein Dutzend Spawns mit
    // Oktavspruengen beim TRIG-Druck, auf einem Deck, das ausdruecklich EINE
    // Tonhoehe halten soll. Auf einer Synth-Part gibt der Helper nch
    // unveraendert zurueck, dort aendert sich also nichts.
    _engine->trigger_chord(chord, _flatten_for_sampler(chord, n));
}

float Part::max_voice_env() const {
    float m = 0.f;
    for (int v = 0; v < SynthEngine::kVoices; ++v) {
        const float e = voice_env(v);   // engine-qualified: 0 on test tone
        if (e > m) m = e;
    }
    return m;
}

// Everything the engine and FX read at their own control rate: the five lane
// targets, the quantized pitch, the chord surface, the set_targets push, and
// the five FX target values. Runs on the SynthEngine::kCtrlInterval-sample
// raster, phase-aligned with SynthEngine's own control tick (see the
// _ctrl_ctr comment in part.h). Not idempotent -- it advances
// Quantizer::process's slew and re-evaluates ChordBuilder::set_color's zone
// hysteresis -- so process()'s raster-tick and fire-refresh branches must
// stay mutually exclusive (else if, not a second if); calling this twice on
// the same sample double-steps the glide.
void Part::_control_tick() {
    // LANE_PITCH is skipped on purpose: the assignment further down (after the
    // quantizer) writes _tg[LANE_PITCH] unconditionally, so target_raw's value
    // for that slot was only ever discarded. Nothing between the two points
    // reads _tg[LANE_PITCH] -- the only statements in between are
    // pitch_pre_quant(), which recomputes target_raw(LANE_PITCH) from members
    // instead of reading the cache, _quant.process(), whose Quantizer holds no
    // reference to this Part and touches only its own state, and the _pitch_q
    // assignment.
    //
    // Bit-exact for the four remaining slots: target_raw is const, and reads
    // only _base, _active, _tdepth, _depth, _engine_id and the lane outputs, so
    // it neither writes _tg nor depends on which slots were visited before it.
    for (int i = 0; i < LANE_COUNT; ++i)
        if (i != LANE_PITCH) _tg[i] = target_raw(i);

    const float pitch_raw = pitch_pre_quant();
    // Called unconditionally, even when the sampler discards the result: the
    // quantizer carries a slew counter and a hysteresis note across calls, and
    // skipping it while a part is on the sampler would leave that state frozen
    // and make the first synth tick after an engine switch depend on how long
    // the part spent as a sampler. Cheap, and it keeps the synth's behaviour
    // exactly what it was.
    const float pitch_quantized = _quant.process(pitch_raw);

    // The SAMPLER does not quantize. The quantizer is a melody device: it snaps
    // a lane's pitch onto the scale so composed notes land in key. The sampler
    // has no melody -- the morphagene-controls work switched its PITCH lane off
    // -- and TUNE there means one thing only: transpose this recording as a
    // whole, to match material that may be tonal, atonal, or plainly out of
    // tune. Snapping that to the instrument's scale is meaningless, and at the
    // knob's centre it was actively wrong: 0.5 of a 36-semitone span is exactly
    // 18 semitones, a tritone above the root, which most scales do not contain.
    // Measured at TUNE 0.5 with the PITCH lane off: three of the eight scales
    // snapped the "neutral" detent a semitone flat, one a semitone sharp, and
    // only four left it at unity -- so recorded material played back off-pitch
    // against its own source, and which way depended on the SCALE knob.
    // Unquantized, the centre is exactly 1.0 for every scale and root, and the
    // knob transposes continuously (the author's call: out-of-tune material has
    // to be tunable to the key, which a semitone grid cannot do).
    // The SAMPLER does not quantize (see the comment above). The BBD does not
    // either, but only in FLOW: STEP puts the clock on scale steps so the bend
    // is in the key, and FLOW leaves it continuous, which is the gesture FLOW
    // exists for.
    _pitch_q = (_engine_id == ENGINE_SAMPLER ||
                (_engine_id == ENGINE_BBD && !_step_on)) ? pitch_raw
                                                         : pitch_quantized;
    _tg[LANE_PITCH] = clampf(_pitch_q + _detune_cents * (1.f / 3600.f), 0.f, 1.f);

    // MOTION's Scatter startet auf einem Sampler-Deck bei null, nicht bei der
    // Lane-Basis 0.5. Dieselbe Schicht und dieselbe Begruendung wie
    // _flatten_for_sampler und die abgeschaltete PITCH-Lane: die INSTRUMENT-
    // Schicht entscheidet, was ein Sampler-Deck nicht tut.
    //
    // Der Grund ist messbar, nicht aesthetisch. Die Basis 0.5 schreibt
    // niemand -- weder Host noch Instrument -- und SuperModulator::set_range
    // trifft nur LANE_PITCH, die Texturlanes behalten also _range = 1. Bei
    // MOD = 0 stand _targets[LANE_MOTION] damit unabaenderlich auf 0.5, und
    // in SamplerEngine::_spawn_one ist der Positions-Jitter dann
    // gleichverteilt ueber ein Intervall der Breite GENAU content. Damit ist
    // (SOURCE*span + _scan_pos + jitter) mod content exakt gleichverteilt,
    // unabhaengig von beiden Summanden: ORGANIZE und SCAN hatten auf die
    // Spawn-Position nachweislich null Effekt (gemessen: Mittelwert 12036 /
    // 11896 / 11951 bei SOURCE 0 / 0.25 / 0.9 ueber content 24000).
    //
    // Nur der Basisanteil faellt weg, die Lane-Modulation bleibt: bei MOD > 0
    // schiebt sie von 0 nach oben, MOD wird also zum MOTION-Regler des Decks
    // und der Nebel bleibt erreichbar.
    //
    // Bewusst an _tg und nicht an _base: COLOR (cmod) und DENS (omod) unten
    // lesen _mod.lane_output(LANE_MOTION) direkt und bleiben davon unberuehrt.
    //
    // Fuer Szenario-Autoren (review 2026-07-22, F-04-Nachtrag): auf einem
    // Sampler-Deck ist set_target_base(part, LANE_MOTION, …) damit absichtlich
    // wirkungslos, egal welchen Wert man ihm gibt -- der Regler, der MOTION
    // hier tatsaechlich bewegt, ist MOD (set_depth), nicht die Lane-Basis. Ein
    // Szenario, das stattdessen die Basis walkt, rendert an allen Punkten
    // dieselbe Audio (traf host/render/scenarios/sampler_extremes.json genau
    // so, bevor es korrigiert wurde).
    //
    // Die Lane selbst bleibt bipolar: _mod.lane_output(LANE_MOTION) liefert
    // [-1, 1] (_range bleibt 1, siehe oben), und clampf(mmod, 0.f, 1.f) unten
    // kappt die untere Haelfte komplett weg, statt sie -- wie die alte
    // 0.5+mod-Formel -- symmetrisch um einen Mittelpunkt zu falten. Hoerbar
    // heisst das: der Scatter PULSIERT (steht die halbe Modulationsperiode
    // lang exakt bei 0 und schiesst dann in einen positiven Ausschlag),
    // statt gleichmaessig zu ATMEN. Zwei Alternativen, falls das nicht die
    // gewuenschte Form ist: fabsf(mmod) fuer eine kontinuierliche
    // Vollratenversion (beide Halbwellen tragen bei, nie ein Stillstand),
    // oder eine reskalierte bipolare Abbildung (0.5f + 0.5f*mmod), die wieder
    // atmet statt zu pulsen. Der harte Clamp hier ist die aktuell gehoerte
    // und vorlaeufig akzeptierte Fassung (Variante a) -- welche der drei am
    // Ende bleibt, ist eine Hoerentscheidung, keine, die dieser Kommentar
    // trifft.
    if (_engine_id == ENGINE_SAMPLER) {
        const float mmod = _active[LANE_MOTION]
            ? _mod.lane_output(LANE_MOTION) * _depth * _tdepth[LANE_MOTION]
            : 0.f;
        _tg[LANE_MOTION] = clampf(mmod, 0.f, 1.f);
    }

    // chord layer: refresh the surface every tick (cheap interval apply);
    // full voice-leading build only on a fire
    // COLOR is MOTION's third destination, alongside pan fan and drift (spec
    // 2026-07-18 color-motion-target). Bipolar additive: the knob is the
    // centre, MOTION swings +/-kColorMod around it at MOD = 1. The gate makes
    // COLOR = 0 exactly silent by construction.
    const float cgate = clampf(_color / kColorGate, 0.f, 1.f);
    const float cmod  = _active[LANE_MOTION]
        ? _mod.lane_output(LANE_MOTION) * _depth * kColorMod * cgate
        : 0.f;
    _color_eff = clampf(_color + cmod, 0.f, 1.f);
    _chord.set_color(_color_eff);
    // Stereo width (spec 5.7, Task 7): the SAME effective COLOR the chord
    // layer just received, so a BBD deck's width breathes with MOTION exactly
    // the way the chord surface does. Default no-op (engine_iface.h), so
    // every other engine is untouched.
    _engine->set_width(_color_eff);

    // DENS -> grain overlap, with MOTION's swing on top (spec 2026-07-21
    // morphagene-controls). Pushed straight at _sampler rather than through
    // _engine: it is a sampler-only parameter, and _sampler is a concrete
    // member here just as it is for the voice row (part.h). On a synth part
    // this is one float store into an engine nobody is listening to.
    const float omod = _active[LANE_MOTION]
        ? _mod.lane_output(LANE_MOTION) * _depth * kOverlapMod
        : 0.f;
    _overlap_eff = clampf(_overlap + omod, 0.f, 1.f);
    _sampler.set_overlap(_overlap_eff);

    // Slice-groove side channel (spec 2026-07-22): the step clock rides the
    // same raster as every other engine push. Same idiom as set_overlap --
    // sampler-only, pushed at _sampler directly.
    // FEEL and dispersion (spec 2026-07-23) ride the same push, and between
    // them read BOTH values COLOR produced above: two consumers, two
    // different values of the same knob. Which gets which, and why, is
    // explained at the two calls below.
    if (_engine_id == ENGINE_SAMPLER) {
        _sampler.set_step_clock(_mod.pitch_step_samples());
        // Die beiden COLOR-Pushes lesen ABSICHTLICH verschiedene Werte, und
        // die Regel dahinter ist allgemeiner als dieser Regler:
        // DISKRETE EREIGNISSE BEKOMMEN KEINEN VERSTECKTEN SWING,
        // KONTINUIERLICHE TEXTUREN SCHON.
        //
        // FEEL ist Akzenttiefe auf einzelnen komponierten Noten -- eine
        // atmende Akzenttiefe waere genau die versteckte Kopplung, die die
        // FEEL-Spec abgeschafft hat. Also der rohe Knopf.
        //
        // Die Streuung ist eine Eigenschaft der WOLKE, und MOTION besitzt
        // dort bereits jede andere Streuachse: Position, Pan, Spawn-Timing.
        // Die Tonhoehe davon auszunehmen hiesse, eine Achse still stehen zu
        // lassen, waehrend die anderen drei atmen. Also _color_eff.
        //
        // Gepusht wird in BEIDEN Modi. Die Felder sind in STEP schlicht
        // wirkungslos (_spawn_slice liest sie nicht); ein Modus-Gate waere
        // Zustand ohne Sicherheitsgewinn.
        _sampler.set_feel(_color);
        _sampler.set_dispersion(_color_eff);
    }

    float chord[ChordBuilder::kMaxNotes];
    // apply() runs unconditionally even when the sampler discards its result:
    // ChordBuilder carries zone hysteresis and voice-leading state across
    // calls, and skipping it on a sampler part would freeze that state and
    // make the first synth tick after an engine switch depend on how long the
    // part had been a sampler. Same reasoning as the quantizer call above.
    int nch = _chord.apply(_tg[LANE_PITCH], _chord_mask(),
                           _quant.root_semis(), chord);
    nch = _flatten_for_sampler(chord, nch);
    _engine->set_chord(chord, nch);

    // Raster-rate push is safe because both receivers smooth on their own
    // side and neither ever sees the 96-sample staircase raw: SynthEngine::
    // process runs _targets[LANE_LEVEL] through a ~10 ms smoother, and
    // PartFx::process runs each of the five FX values through a 2 ms
    // smoother. Do not "optimize away" that smoothing -- it is what makes
    // this raster hold inaudible.
    _engine->set_targets(_tg, _tune);
    for (int i = 0; i < FXT_COUNT; ++i) _fxv[i] = fx_target_value(i);

    // Excitation bus (spec §6, Tasks 9 + 10). Three enabled-by-flag sources,
    // each one control block late for its own reason:
    //   - _fx.tape_tap(): order within one sample is _control_tick() ->
    //     _engine->process() -> _fx.process() (Part::process, part.h), so
    //     the value read here is whatever the LAST sample of the PREVIOUS
    //     block left cached in PartFx -- no delay line of our own needed
    //     (Task 9's mechanism, unchanged).
    //   - _other_deck_tap: pushed by Instrument once per control block, from
    //     the SIBLING part's dry output latched at the end of the block
    //     before this one (instrument.cpp) -- symmetric and independent of
    //     which part CHOKE processes first (task-10-brief-addendum.md
    //     section D).
    //   - _audio_in_tap: this part's own doing, latched the same way
    //     Instrument latches its cross-deck floats -- see the capture point
    //     in Part::process (part.h).
    // Sum first, DC-block and soft-clip the SUM second: tape_tap() is
    // already DC-blocked and fast_tanh-clipped inside PartFx (that bound is
    // what makes tape_tap() a safe getter for ANY caller), but the cross-deck
    // and audio-in taps arrive un-blocked, and three sources summed can
    // reach toward 3.0 even when each is individually bounded. Both clips
    // stay -- fast_tanh(fast_tanh(x)) ~= x well below unity, so the double
    // compression is inaudible at ordinary levels and only bites in
    // self-oscillation territory, exactly where spec §6 wants a bound
    // (task-10-brief-addendum.md section E; a Task 12 listening item, not a
    // defect here). IPartEngine::set_excitation defaults to a no-op
    // (engine_iface.h), so this changes nothing for TestToneEngine,
    // SamplerEngine, SYNTH or WAVE; only SynthEngineT<BodyVoice> forwards it
    // anywhere, and BodyVoice hard-gates on SUB > 0 regardless of what this
    // sum contains (part.h set_voice_sub comment; BodyVoice::process).
    float bus = 0.f;
    if (_src_tape)  bus += _fx.tape_tap();
    if (_src_deck)  bus += _other_deck_tap;
    if (_src_audio) bus += _audio_in_tap;
    bus = fast_tanh(_bus_dc.Process(bus));
    _excitation_eff = bus;   // observer only (excitation_eff(), part.h)
    _engine->set_excitation(bus);
}

// --- the cold blocks of Part::process ---------------------------------------
//
// Part::process itself lives in part.h, inline, so that every call site can
// inline the per-sample path (see the comment on its definition there). The
// four blocks below are the ones that do NOT run every sample; each keeps its
// guard at the call site in part.h and its body here, statement for statement
// as process() carried it.

// Guard in part.h: `if (_switching && _engine_fade.is_idle())`. The fade value
// is read there BEFORE that test, and that order is load-bearing -- it decides
// which call of the ramp the swap lands on, and therefore how far apart
// engine_id()'s flip and the fade's end are. Do not reorder the two.
void Part::_engine_swap() {
    _engine_id = _pending_engine;
    _engine = _engine_for(_engine_id);
    _engine_wants_in = _engine->consumes_input();   // pairs with _engine
    // On a SAMPLER deck the PITCH lane is a read position, and on a BBD deck in
    // FLOW it is the continuous clock bend -- on both, the sweep IS the feature
    // and a slot sequencer would make it a staircase. Pushed at the two sites
    // where _engine_id is written, the same convention _engine_wants_in
    // follows, so the two cannot drift apart.
    //
    // This names the same two engines as the quantizer bypass in
    // _control_tick (SAMPLER || (BBD && !_step_on)) for the same underlying
    // reason -- on those decks the PITCH lane is not a note -- but it does NOT
    // derive from it. Do not move this to that per-tick site.
    _mod.set_flow_melody(_engine_id != ENGINE_SAMPLER && _engine_id != ENGINE_BBD);
    // The BBD holds charge, and IPartEngine has no swap-away notification --
    // Part only ever pushes state INTO the engine being swapped in. Without
    // this a deck switched away from and back to returns the previous take.
    if (_engine_id == ENGINE_BBD) _bbd.reset();
    _engine->set_flow(!_step_on);                          // re-sync state
    _engine->set_hold(_inhibit);
    _engine->set_gate(_last_gate);   // the freshly swapped-in engine
    if (_last_master_hz > 0.f) _engine->set_cycle(1.f / _last_master_hz);
    _switching = false;
    _engine_fade.set_on(true);
    // A freshly swapped-in engine holds none of the previous engine's
    // pushed state -- set_targets()/set_chord() never reached it while
    // it was inactive -- so re-arm the raster to run _control_tick()
    // later in THIS SAME process() call, rather than up to
    // SynthEngine::kCtrlInterval - 1 samples from now on its power-on
    // defaults (e.g. TestToneEngine's 220 Hz _freq, test_tone_engine.h).
    _ctrl_ctr = 0;
}

// Guard in part.h: `if (hz != _last_master_hz && hz > 0.f)`, i.e. this runs
// only when the master lane's rate actually moved.
void Part::_push_master_cycle(float hz) {
    _last_master_hz = hz;
    _engine->set_cycle(1.f / hz);
}

// Guard in part.h: `if (fired && !_note_suppressed)`, called after the
// raster/fire branches. `fired` is true whenever this runs, so one of those two
// branches has already run _control_tick() exactly once for this sample.
void Part::_fire_trigger() {
    if (_engine_id == ENGINE_SAMPLER) {
        const int slot = _mod.pitch_cur_step();
        _sampler.set_phrase_pos(slot, _mod.pitch_steps(),
                                pg_metric_weight(slot));
    }
    if (_engine_id == ENGINE_BBD) _bbd.latch_clock();
    float chord[ChordBuilder::kMaxNotes];
    // build() unconditionally, for the same state reason as apply() in
    // _control_tick.
    int nch = _chord.build(_tg[LANE_PITCH], _chord_mask(),
                           _quant.root_semis(), chord);
    nch = _flatten_for_sampler(chord, nch);
    // Before the strike, like set_material_character inside the engine: the
    // note being triggered must be struck with ITS OWN accent, not with the
    // one the previous fire left behind.
    _engine->set_accent(_mod.pitch_note_accent());
    _engine->trigger_chord(chord, nch);
}

// Guard in part.h: `if (g != _last_gate)`, with g == gate() computed there
// after _gate_ctr has been advanced.
void Part::_gate_edge(bool g) {
    _last_gate = g;
    _engine->set_gate(g);
}
