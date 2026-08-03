#pragma once
#include <array>
#include <cstddef>
#include "parts/part.h"
#include "mod/lane_id.h"
#include "fx/reverb.h"
#include "fx/limiter.h"
#include "center/center.h"
#include "util/onepole.h"
#include "sampler/sampler_engine.h"

namespace spky {

enum PartId { PART_A = 0, PART_B = 1, PART_COUNT = 2 };

// FX memory injected by the host (spec "No heap"): one echo buffer of
// Flux::kMaxSamples floats per part, and storage for the one shared reverb.
// Desktop: static arrays / static object. Daisy (M6): SDRAM.
struct FxMem {
    float* echo[PART_COUNT][2] = {};
    AmbientReverb* reverb = nullptr;
    // M5 texture deck: one stereo record buffer per part. Spec sizing is
    // 42 s at 48 kHz (~16 MB/part) -- hosts allocate on the heap (desktop,
    // Rack) or in SDRAM (M6). nullptr -> that part's sampler runs silent.
    SampleBuffer::Frame* sampler_buf[PART_COUNT] = {};
    size_t sampler_frames = 0;
    // The BBD part engine's two lines per deck (spec 2026-07-31 §5.7). Sized
    // BbdEngine::kCells floats each = 32 KB per line, 128 KB for the
    // instrument. SDRAM on the Seed, static or heap on the desktop.
    // nullptr -> that deck's BBD engine runs silent.
    //
    float* bbd[PART_COUNT][2] = {};
};

static_assert(BbdEngine::kCells == bbd_tuning::kMaxStages / 2,
              "BBD engine storage is one cell per two physical stages");

// The complete public API. No hardware type crosses this boundary; the same
// object is driven by the desktop render host and (later) the firmware shell.
class Instrument {
public:
    void init(float sample_rate);                    // engine only, no FX chain
    void init(float sample_rate, const FxMem& mem);  // full FX chain
    void set_tempo_bpm(float bpm);

    void set_rate(int p, float n)            { _parts[p].mod().set_rate(n); }
    void set_shape(int p, float n)           { _parts[p].mod().set_shape(n); }
    void set_density(int p, float d)         { _parts[p].mod().set_density(d); }
    void set_smooth(int p, float n)          { _parts[p].mod().set_smooth(n); }
    void set_range(int p, float n)           { _parts[p].mod().set_range(n); }
    void set_variation(int p, float n)       { _parts[p].mod().set_variation(n); }  // -1..+1
    void set_shuffle(float amount)           { for (auto& part : _parts) part.mod().set_shuffle(amount); }
    void set_form(int p, int form) {
        if (form < 0) form = 0;
        const int last = static_cast<int>(Principle::kCount) - 1;
        if (form > last) form = last;
        _parts[p].mod().set_form(static_cast<Principle>(form));
    }
    void set_song(int p, int song) {
        _parts[p].mod().set_song(clamp_song(song));
    }
    int form(int p) const {
        return static_cast<int>(_parts[p].mod().form());
    }
    int song(int p) const {
        return static_cast<int>(_parts[p].mod().song());
    }
    void set_principle(int p, int pr) {
        set_form(p, pr);
    }
    void set_step(int p, bool on, int steps) { _parts[p].set_step(on, steps); }
    void new_phrase(int p)                   { _parts[p].mod().new_phrase(); }
#ifdef SPKY_TESTING
    uint32_t song_position_for_test(int p) const {
        return _parts[p].mod().song_position_for_test();
    }
    uint8_t active_pattern_for_test(int p) const {
        return _parts[p].mod().active_pattern_for_test();
    }
#endif
    void set_fixed_slew(int p, bool on)      { _parts[p].mod().set_fixed_slew(on); }
    void set_depth(int p, float n)           { _parts[p].set_depth(n); }
    void set_tune(int p, float n)            { _parts[p].set_tune(n); }
    void set_color(int p, float n)           { _parts[p].set_color(n); }
    void set_target_active(int p, int s, bool on) { _parts[p].set_target_active(s, on); }
    void set_target_base(int p, int s, float n)   { _parts[p].set_target_base(s, n); }
    void set_target_depth(int p, int s, float n)  { _parts[p].set_target_depth(s, n); }
    void set_scale(int scale_idx) {
        if (scale_idx < 0) scale_idx = 0;
        if (scale_idx >= SCALE_LIST_COUNT) scale_idx = SCALE_LIST_COUNT - 1;
        for (auto& part : _parts) part.quant().set_scale(SCALE_MASKS[scale_idx]);
    }
    void set_quant_mode(int p, QuantMode m) { _parts[p].quant().set_mode(m); }
    void set_root(int p, int semis)         { _parts[p].quant().set_root(semis); }

    // Excitation bus source selection (spec 2026-07-26 body-resonator §6,
    // Task 10): patch state, not a performance control. Default (tape on,
    // other-deck off, audio-in off) is set inside Part.
    void set_excitation_sources(int p, bool tape, bool other_deck, bool audio_in) {
        _parts[p].set_excitation_sources(tape, other_deck, audio_in);
    }
    void set_fx_on(int p, FxBlock which, bool on)  { _parts[p].fx().set_fx_on(which, on); }
    void set_grit_mode(int p, GritMode m)          { _parts[p].fx().set_grit_mode(m); }
    void set_fx_target_active(int p, int i, bool on) { _parts[p].set_fx_target_active(i, on); }
    void set_fx_target_base(int p, int i, float n) { _parts[p].set_fx_target_base(i, n); }
    void set_fx_target_depth(int p, int i, float n){ _parts[p].set_fx_target_depth(i, n); }
    void set_flux_mix(int p, float n)              { _parts[p].fx().set_flux_mix(n); }
    void set_flux_rate(int p, int slice_idx) { _parts[p].fx().set_flux_rate(slice_idx); }
    void set_grit_mix(int p, float n)              { _parts[p].fx().set_grit_mix(n); }
    void set_link(int p, float n)   { _parts[p].fx().set_link(n); }
    void set_comp(int p, float n)                  { _parts[p].fx().set_comp(n); }
    void set_reverb_size(float n)  { if (_reverb) _reverb->set_size(n); }
    void set_reverb_decay(float n) {
        if (_reverb) _reverb->set_decay(n);
        // Same public curve the tooltip reads: one source, no drift. The
        // seconds-slow slew stands in for hysteresis at the unity point.
        _duck_armed = AmbientReverb::decay_loop_gain(n) > 1.f;
    }
    void set_reverb_tone(float n)  { if (_reverb) _reverb->set_tone(n); }
    void set_reverb_diffusion(float n) { if (_reverb) _reverb->set_diffusion(n); }
    void set_reverb_smear(float n) { if (_reverb) _reverb->set_diffuser_mod_depth(n); }
    void set_reverb_mod(float n)   { if (_reverb) _reverb->set_mod_depth(n); }
    // Per-deck equal-power reverb mix (spec 2026-07-23 per-deck-reverb-mix):
    // one shared room, each deck's dry rides cos, its SEND rides sin.
    void set_reverb_mix(int part, float n);   // 0..1, exact endpoints
    void set_reverb_mix(float n);             // convenience: both decks
    void set_master_drive(float n) { _limiter.set_drive(n); }
    float fx_target_value(int p, int i) const { return _parts[p].fx_target_value(i); }
    // Observer only, for tests (Task 10): the part's own FLUX tape tap, so a
    // test claiming "the excitation bus's tape source is hot" can assert it
    // instead of merely hoping FLUX ever engaged (see task-10-brief-addendum.md
    // section B -- init(sample_rate) alone builds no FX chain, so this would
    // read a permanent 0.f without a real FxMem and FLUX actually switched on).
    float tape_tap(int p) const { return _parts[p].fx().tape_tap(); }
    // Observer only, for tests (Task 10 review round 2): the excitation bus
    // value actually pushed to the engine at the last control tick -- see
    // Part::excitation_eff().
    float excitation_bus(int p) const { return _parts[p].excitation_eff(); }
    // Observer only, for tests: the bloom duck's gain on the dry bus. From
    // outside a duck is indistinguishable from quieter playing (the same
    // argument as limiter_gain()), so this is the only honest probe.
    float duck_gain() const { return _duck_gain; }
    // Observer only, for tests: deck p's post-FX output from the sample just
    // processed. ch 0 = L, 1 = R. Latency cannot be measured from the summed
    // output, which cannot distinguish 0 samples from 1.
    float deck_tap(int p, int ch) const { return _deck_tap[p][ch]; }

    // --- M2 synth voice API (spec "Instrument API") ---
    void set_engine(int p, EngineId e)       { _parts[p].set_engine(e); }
    void set_voice_attack(int p, float n)    { _parts[p].set_voice_attack(n); }
    void set_voice_decay(int p, float n)     { _parts[p].set_voice_decay(n); }
    void set_voice_resonance(int p, float n) { _parts[p].set_voice_resonance(n); }
    void set_voice_sub(int p, float n)       { _parts[p].set_voice_sub(n); }
    void set_voice_detune(int p, float n)    { _parts[p].set_voice_detune(n); }
    void set_voice_filt(int p, float t)      { _parts[p].set_voice_filt(t); }
    void trigger_manual(int p)               { _parts[p].trigger_manual(); }
    int  active_voices(int p) const          { return _parts[p].active_voices(); }
    float voice_env(int p, int v) const      { return _parts[p].voice_env(v); }
    EngineId engine_id(int p) const          { return _parts[p].engine_id(); }

    // BBD observers (spec 2026-07-31 9). A BBD deck writes 0 into a_voices and
    // a_v0..3 and would otherwise expose nothing, so a demo scenario would
    // pass vacuously. Zero (or the stated sentinel) on every other engine,
    // which is what the CSV should show.
    //
    // clock_now(), not clock_hz(): DETUNE's glide means the two differ
    // whenever the clock is chasing a moved lane, which is exactly when a CSV
    // reader is most likely to be looking. The CSV should record what the
    // instrument actually did, not merely what it was asked to do.
    float bbd_clock_hz(int p) const {
        return _parts[p].engine_id() == ENGINE_BBD ? _parts[p].bbd().clock_now() : 0.f;
    }
    int   bbd_stages(int p) const {
        return _parts[p].engine_id() == ENGINE_BBD ? _parts[p].bbd().stages() : 0;
    }
    int   bbd_div(int p) const {
        return _parts[p].engine_id() == ENGINE_BBD ? _parts[p].bbd().div_index() : -1;
    }
    bool  bbd_frozen(int p) const {
        return _parts[p].engine_id() == ENGINE_BBD && _parts[p].bbd().frozen();
    }
    bool  bbd_time_clamped(int p) const {
        return _parts[p].engine_id() == ENGINE_BBD && _parts[p].bbd().time_clamped();
    }
    bool  bbd_scale_truncated(int p) const {
        return _parts[p].engine_id() == ENGINE_BBD && _parts[p].bbd().scale_truncated();
    }

    // --- M5 sampler API (spec "Instrument API") ---
    void sampler_record(int p, bool on) {
        _parts[p].sampler().set_recording(on);
        // Monitoring follows REC automatically, in one place, so both hosts
        // get it right (plan: deliberate deviation 3).
        _parts[p].sampler().set_monitor(on);
    }
    void  sampler_clear(int p)              { _parts[p].sampler().clear(); }
    float sampler_fill(int p) const         { return _parts[p].sampler().buffer_fill(); }
    bool  sampler_empty(int p) const        { return _parts[p].sampler().is_empty(); }
    bool  sampler_is_recording(int p) const { return _parts[p].sampler().is_recording(); }
    void  sampler_monitor(int p, bool on)   { _parts[p].sampler().set_monitor(on); }
    int   sampler_grains(int p) const       { return _parts[p].sampler().active_grains(); }
    // Live marker count. In the CSV this is the one observable that separates
    // the slice-groove's two STEP modes: >= sampler_cfg::kMinSlices means the
    // walk is following transients, below it means the tempo-grid fallback.
    // The render scenarios' whole claim about which mode they exercise rests
    // on this column -- before it existed, sampler_slice_field.json asserted
    // "falls back to the grid" in a comment and in fact ran in marker mode.
    int   sampler_slices(int p) const       { return _parts[p].sampler().slice_count(); }
    void  sampler_speed_mode(int p, bool tape) { _parts[p].sampler().set_tape_mode(tape); }
    void  sampler_reverse(int p, bool on)   { _parts[p].sampler().set_reverse(on); }
    void  sampler_feedback(int p, float n)  { _parts[p].sampler().set_feedback(n); }
    // --- M5c sampler controls (spec 2026-07-21 morphagene-controls) ---
    // NOTE: not "morph" -- set_morph is already taken by the global A/B
    // control (see set_morph above). This is the grain overlap.
    void  sampler_overlap(int p, float n)  { _parts[p].set_sampler_overlap(n); }
    void  sampler_scan(int p, float bipolar) { _parts[p].sampler().set_scan(bipolar); }
    void  sampler_punch(int p)             { _parts[p].sampler().punch(); }
    float sampler_scan_pos(int p) const    { return _parts[p].sampler().scan_pos(); }
    // last_spawn_pos(): the actual centre a grain last read from, i.e. SOURCE
    // (clamped) * span + scan_pos + jitter, folded (SamplerEngine::_spawn_one).
    // scan_pos() alone is only the tape-head OFFSET, not the read position --
    // it sits at 0 whenever ORGANIZE parks the head mid-buffer. The VCV ring's
    // read-position dot wants the read position (spec 2026-07-21
    // morphagene-controls, "Der Kopf wird sichtbar"), so it must use this, not
    // sampler_scan_pos(). sampler_scan_pos() itself stays -- tests/
    // test_scenario.cpp pins it directly.
    float sampler_last_spawn_pos(int p) const { return _parts[p].sampler().last_spawn_pos(); }
    // Observer for tests/scenarios: the knob plus MOTION's swing, as last
    // pushed to the engine on the most recent control tick (Part::overlap_eff).
    float sampler_overlap_eff(int p) const { return _parts[p].overlap_eff(); }
    // Observer only: the pitch ratio the most recent grain spawned at. Lets a
    // test pin that a sampler deck grants every grain ONE pitch.
    float sampler_last_spawn_ratio(int p) const {
        return _parts[p].sampler().last_spawn_ratio();
    }
    // Observer only, for tests: the engine's own cumulative spawn counter
    // (SamplerEngine::spawn_count()). last_spawn_pos()/last_spawn_ratio() hold
    // their value BETWEEN spawns, so a test that only watches those two can
    // pass vacuously if no spawn actually lands in its observation window --
    // this lets it assert a real count of spawns happened instead (K-01,
    // review 2026-07-22, mirroring the guard F-04's "ORGANIZE reaches the
    // spawn position" test already uses via the bare Part).
    int sampler_spawn_count(int p) const { return _parts[p].sampler().spawn_count(); }
    // Observer only: how many notes the SYNTH leg last received, so a test can
    // pin that the sampler's chord flattening does not reach the synth.
    int synth_chord_n(int p) const { return _parts[p].synth().chord_n(); }
    void  load_sample(int p, const float* l, const float* r, size_t frames) {
        _parts[p].sampler().load_sample(l, r, frames);
    }
    const SampleBuffer::Frame* sampler_data(int p) const {
        return _parts[p].sampler().sample_data();
    }
    size_t sampler_rec_size(int p) const { return _parts[p].sampler().rec_size(); }

    float lane_output(int p, int s)  const { return _parts[p].lane_output(s); }
    float target_value(int p, int s) const { return _parts[p].target_value(s); }
    bool  lane_fired(int p, int s)   const { return _parts[p].lane_fired(s); }
    bool  gate(int p)  const { return _parts[p].gate(); }
    float pitch_cv(int p) const { return _parts[p].pitch_cv(); }
    bool  pitch_gate(int p) const { return _parts[p].mod().pitch_gate(); }
    // The bank's own published rhythm (see mod/rhythm_view.h). Read again by
    // the control tick: each part's FLUX takes its THIN pattern from the
    // SIBLING's rhythm through this
    // accessor -- `_parts[PART_A].fx().set_rhythm(rhythm(PART_B))` and the
    // mirror, in instrument.cpp's control tick -- and tests read it directly
    // (tests/test_instrument.cpp).
    const RhythmView& rhythm(int p) const { return _parts[p].mod().rhythm(); }

    // --- M4 center section ---
    void set_morph(float m)  { _center.set_morph(m); }
    void set_couple(float c) { _center.set_couple(c); }
    void set_drift(float d)  { _center.set_drift(d); }
    void set_tide(float n)   { for (auto& p : _parts) p.mod().set_tide(n); }
    void set_sync(bool on) {
        _center.set_sync(on);
        for (auto& p : _parts) p.mod().set_synced(on);
    }
    // CHOKE (spec 2026-07-16 choke-priority, rev. 2): discrete zones.
    // 0 = off (bit-identical bypass); negative = A priority / B yields,
    // positive mirrored. |c| <= 0.5 blocks during the priority gate only,
    // |c| > 0.5 through the full decay (while the voice is audible).
    void set_choke(float c) { _choke = clampf(c, -1.f, 1.f); }
    void clock_pulse()     { _center.clock_pulse(); }
    // RST = bar resync: zero the downbeat, drop the grid offsets a live STEPS
    // turn left behind, and restart the loops at phase 0 — everything lands on
    // the fresh bar start together (no servo drag).
    void reset_transport() {
        _center.reset_transport();
        for (auto& p : _parts) p.mod().reset_phases();
    }
    void spot()   { _center.spot(_parts[PART_A].mod(),   _parts[PART_B].mod()); }
    void settle() { _center.settle(_parts[PART_A].mod(), _parts[PART_B].mod()); }
    float morph()     const { return _center.morph(); }
    float couple()    const { return _center.couple(); }
    float drift()     const { return _center.drift(); }
    float weather()   const { return _center.weather(); }
    float phase_err() const { return _center.phase_err(); }
    bool reverb_asleep() const { return _rev_asleep; }

    void process(const float* inL, const float* inR, float* outL, float* outR, size_t n);

private:
    std::array<Part, PART_COUNT> _parts;
    AmbientReverb* _reverb = nullptr;
    // Per-deck equal-power mix (spec 2026-07-23): dry rides cos, the wet SEND
    // rides sin, one shared room. Indexed by PART_A / PART_B.
    float   _rev_dry_target[PART_COUNT] = { 1.f, 1.f };  // exact endpoints
    float   _rev_wet_target[PART_COUNT] = { 0.f, 0.f };
    OnePole _rev_dry[PART_COUNT], _rev_wet[PART_COUNT];  // 10 ms glide per deck
    bool    _rev_primed = false;    // first process() snaps the mix gains
    bool    _rev_asleep = false;    // both decks dry: room cleared, process() skipped
    // Bloom duck (spec 2026-08-03-reverb-bloom-duck): while the room is over
    // unity loop gain its return envelope pulls the dry bus back. Exactly
    // 1.0 whenever it is not ducking -- guarded by a test, like the return
    // ceiling's limiter_gain().
    float _duck_gain = 1.f, _duck_target = 1.f;
    // Who controls the room: below unity loop gain the player does (never
    // duck, at any level -- an ordinary long room legitimately returns +6 dB
    // over its send, overlapping the bloom's +7.6 dB, so LEVEL cannot tell
    // them apart; 10961a0 measured that). Above unity the loop drives itself
    // and the duck takes the envelope. Not 02134e3's dead trim: the knob is
    // read as a regime bit here, never as a level.
    bool _duck_armed = false;
    Limiter _limiter;
    Center _center;
    // Excitation bus, cross-deck source (spec §6, Task 10): each part's own
    // dry mono output (pre-MORPH, pre-reverb-send, pre-limiter -- see the
    // capture point in process()), latched once per control block on the
    // block's LAST sample and handed to the SIBLING part on the FOLLOWING
    // block's first sample. That one-block lag is load-bearing, not
    // incidental: CHOKE picks which part processes first every sample
    // (process()'s `pri`/`yld` swap below), so a same-sample read would make
    // deck A hear deck B's current sample or its previous one depending on a
    // knob that has nothing to do with excitation. Reading the previous
    // block for both decks removes the question and keeps the coupling
    // symmetric regardless of that order. Instrument is the only scope where
    // both parts are visible; Part still gets no pointer to its sibling.
    float  _dry_tap[PART_COUNT] = { 0.f, 0.f };
    // Audio-rate cross-deck bus (spec 2026-07-31 bbd-part-engine §4.3).
    // Distinct from _dry_tap above, which is the control-rate MONO excitation
    // bus and is unchanged: this one is stereo, written every sample, and
    // carries the deck's post-*part*-FX output -- after that part's own
    // Grit/Flux/Comp chain, but taken BEFORE the cross-deck MORPH blend
    // (_center.gain_a/b below), the shared reverb return, and the master
    // Limiter. So it is not simply "what the player hears": the reverb
    // return in particular is deliberately excluded from the loop -- folding
    // one shared room back into both decks would couple them acoustically,
    // which this bus does not do. Read at the top of the sample and written
    // at the bottom, so the latency is one sample in both directions no
    // matter which deck CHOKE runs first.
    float _deck_tap[PART_COUNT][2] = { { 0.f, 0.f }, { 0.f, 0.f } };
    int    _ctrl_ctr = 0;    // counts down to the next control-rate Center::update
    float _choke = 0.f;        // -1..+1 event-priority knob (discrete zones)
                               // (boots true: the FLOW drone predates any fire)
    float _sr = 48000.f;
    float _bpm = 120.f;
};

} // namespace spky
