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
    // PACE (spec 2026-08-12 modulation-pace). Guards ONLY non-finite input --
    // NOT positivity, unlike set_tempo_bpm's guard on `bpm`. norm == 0.f is
    // itself the lowest legal knob position (pace_mult(0.f) == x1/32,
    // divisions.h), not bad input the way a non-positive bpm would be: do NOT
    // add a `norm > 0.f` check here, it would silently drop every push at or
    // below the knob's own minimum and freeze PACE wherever it last was.
    // host/render/scenario.cpp forwards scenario-file values unvalidated, the
    // same reason set_tempo_bpm needs its own guard; a NaN pace is worse than
    // a NaN BPM -- Transport::set_bpm catches the latter, but a NaN _base_hz
    // is mapped to 0 by set_rate_hz and both decks go silent with no error.
    void set_pace(float norm);

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
    // Wie form()/song() ein gewoehnlicher Observer, kein Testsonderweg.
    // tests/test_controls_map.cpp braucht ihn, um die Abbildung Mux-Kanal ->
    // Setter der Firmware-Shell auf dem Host pruefen zu koennen.
    float rate(int p) const { return _parts[p].mod().rate(); }
    // Observers for the mode invariant (spec 2026-08-06 §5): steps, count and
    // grid are one decision, and a test must be able to see all three parts.
    bool step_on(int p) const { return _parts[p].mod().step_mode(); }
    bool synced(int p) const  { return _parts[p].mod().synced(); }
    int  deck_steps(int p) const { return _parts[p].mod().deck_steps(); }
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
    // What a mod lane actually clocks at, in Hz, plus the clocking mode that
    // decides how that Hz was derived. SuperModulator has held these observers
    // for a while; _parts is private, so from outside the engine they were
    // unreachable and only the SETTER side of a rate could be tested.
    //
    // That gap is what the Fireflow -> flow transfer rig needs closed
    // (tests/test_flow_transfer_diff.cpp). "RATE transferred correctly" and
    // "the lanes run at the speed the patch was built at" are DIFFERENT
    // claims, and only the second one is audible: RATE is one of five inputs
    // to a lane's Hz, alongside TIDE, kLaneRatio, the sync flag and -- when
    // synced -- the tempo. A rig that could only compare parameter values
    // would have reported a clean transfer for a patch running four times too
    // fast, which is precisely the failure it was built to find.
    float lane_rate_hz_for_test(int p, int lane) const {
        return _parts[p].mod().lane_rate_hz_for_test(lane);
    }
    // Real motion through the WHOLE stack -- see ModLane::wrap_count_for_test.
    uint32_t lane_wraps_for_test(int p, int lane) const {
        return _parts[p].mod().lane_wraps_for_test(lane);
    }
    // Fractional remainder, paired with lane_wraps_for_test into "turns".
    float lane_phase_for_test(int p, int lane) const {
        return _parts[p].mod().lane_phase_for_test(lane);
    }
    // The value a lane last emitted, read through the whole stack. See
    // ModLane::_last_out for why this is not _slew.value().
    float lane_value_for_test(int p, int lane) const {
        return _parts[p].mod().lane_last_out_for_test(lane);
    }
    // The melody engine's cross-mode phrase-length invariant, readable through
    // the whole stack: the length the lane is supposed to have right now vs the
    // length its pattern was actually generated at. A gate that hard-codes
    // either side stops testing the invariant the moment a host pushes a STEPS
    // the two happen to share (spec 2026-08-13 flow-melody-engine, §10 14-15).
    int lane_effective_length_for_test(int p, int lane) const {
        return _parts[p].mod().lane_effective_length_for_test(lane);
    }
    int lane_pattern_groove_len_for_test(int p, int lane) const {
        return _parts[p].mod().lane_pattern_groove_len_for_test(lane);
    }
    bool step_mode_for_test(int p)  const { return _parts[p].mod().step_mode(); }
    int  deck_steps_for_test(int p) const { return _parts[p].mod().deck_steps(); }
    float pace_for_test() const { return _pace; }
    // Reads the TRANSPORT's bpm, which carries the pace -- NOT Instrument::_bpm,
    // which is the raw value PACE never touches. A gate written against _bpm
    // would be measuring something this control cannot move.
    float transport_bpm_for_test() const { return _center.transport().bpm(); }
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
    // Observer only, for tests: the semitone mask a deck is quantizing to.
    // set_scale() is index-in, mask-out and nothing read the mask back, so a
    // host pushing the wrong scale index was invisible from the engine side --
    // which is exactly how the VCV panel came to boot Lydian while the init
    // snapshot said Mixolydian (Fireflow.cpp's SCALE branch ignored the
    // snapshot for as long as it hard-coded SCALE_LYDIAN).
    uint16_t scale_mask_for_test(int p) const {
        return _parts[p].quant().scale_mask();
    }

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
    // Observer only, for tests (spec 2026-08-09 hw-control-reduction task 4
    // review): whether a host's bipolar-GRIT sign/mix mapping actually
    // reached the engine the way it claims to, not just whether the host
    // source text contains the right substrings.
    GritMode grit_mode_for_test(int p) const {
        return _parts[p].fx().grit().mode();
    }
    bool grit_engaged_for_test(int p) const {
        return _parts[p].fx().grit().engaged();
    }
    float grit_mix_for_test(int p) const {
        return _parts[p].fx().grit().mix_for_test();
    }
    // Observer only, for tests (task 6, spec 2026-08-09
    // hw-control-reduction): the FLUX tape delay's target time, so a test
    // can prove a TIME knob's raw detent index (and the pinned-neutral
    // FXT_FLUX_TIME base) actually reach the tape's delay time, not just
    // that the host source text claims to route them there.
    float flux_delay_target_for_test(int p) const {
        return _parts[p].fx().flux().delay_target_for_test();
    }
    // Observer only, for tests (review finding IMPORTANT 6, 2026-08-09
    // hw-control-reduction final review): the compressor's own applied
    // amount, so a future LVL/COMP split change has to move a pinned number
    // instead of drifting unnoticed. Comp::amount() is already a plain
    // public getter -- this just reaches it through the same part/fx path
    // as comp_mix_for_test's siblings above.
    float comp_amount_for_test(int p) const {
        return _parts[p].fx().comp().amount();
    }
    // Observer only, for tests: the spread actually reaching voice 0's
    // oscillators (VoiceT<>::detune_cents() / BodyVoice::detune_cents()),
    // i.e. set_voice_detune(p, n)'s result AFTER SynthEngineT::set_detune
    // multiplies by kDetuneCeilCt -- not the raw panel knob. Both engines
    // share the same 0..kDetuneCeilCt (105 ct) units; BODY additionally
    // scales this by its own kDetuneScale (4/3) at the audio callsite
    // (body_voice.cpp), which this deliberately does NOT include, because
    // that scale is BODY's own private implementation detail, not part of
    // the shared VOICE contract this observer exposes.
    //
    // WAVE is here because the factory patch moved onto it (FF_hw_Init.vcvm,
    // 2026-08-09: deck A boots WAVE, deck B SYNTH). This used to answer 0 for
    // anything but SYNTH/BODY, on the stated grounds that the DETUNE contract
    // only pinned those two -- which stopped being true the moment a deck
    // booted WAVE with DETUNE at 0.377. The golden test would have pinned that
    // 0 and reported it as coverage. WAVE is SynthEngineT<VoiceT<WtOsc>>, the
    // same template with the same units, so there was nothing to model.
    //
    // Still 0 on BBD and SAMPLER: they receive set_voice_detune but neither
    // reaches a VoiceT, so there is no spread to report. A test pinning 0
    // there is pinning "this engine has no detuned voice pair", which is true
    // -- but do not read it as a pinned DETUNE value.
    float applied_detune_ct_for_test(int p) const {
        if (_parts[p].engine_id() == ENGINE_SYNTH)
            return _parts[p].synth().applied_detune_ct();
        if (_parts[p].engine_id() == ENGINE_WAVE)
            return _parts[p].wave().applied_detune_ct();
        if (_parts[p].engine_id() == ENGINE_BODY)
            return _parts[p].body().applied_detune_ct();
        return 0.f;
    }
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
    // Observer only, for tests (review finding IMPORTANT 6): the limiter's
    // actual pre-gain (Limiter::set_drive's `1 + 3*n*n`), so a pinned test
    // can catch PUSH's fixed-by-ear constant drifting without depending on
    // a source-text grep for "0.40f".
    float master_drive_pre_gain_for_test() const { return _limiter.pre_gain(); }
    // Observer only, for tests: LVL's smoothed per-deck level, read after a
    // control tick (see Center::level()'s comment for why).
    float part_level_for_test(int p) const { return _center.level(p); }
    // Observers only, for tests: SMEAR/WOBL's fixed-by-ear constants, as
    // actually stored on the shared room (see AmbientReverb's _for_test
    // getters). 0.f with no reverb attached (engine-only init(sample_rate)).
    float reverb_smear_for_test() const {
        return _reverb ? _reverb->diffuser_mod_depth_for_test() : 0.f;
    }
    float reverb_mod_for_test() const {
        return _reverb ? _reverb->mod_depth_for_test() : 0.f;
    }
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
    // 0 while the master shaper is transparent, rising as it bends. This is
    // the audible onset, not the gain reduction -- see limiter.h.
    float limiter_squash() const { return _limiter.squash(); }
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
    // The modulation term alone -- what the LED law displays. Deliberately
    // NOT target_value(), which is base + mod and would show the knob.
    float lane_excursion(int p, int s) const { return _parts[p].lane_excursion(s); }
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
    void set_part_level(int p, float lvl) { _center.set_level(p, lvl); }
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
    void clock_pulse()     { _center.clock_pulse(_pace); }
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
    // The single door, shared by both set_tempo_bpm and set_pace. PACE reaches
    // the transport (_center.set_tempo_bpm(_bpm * _pace)) and the mod lanes
    // (SuperModulator::set_pace) as a tempo multiplier. FLUX's own delay
    // deliberately stays in real time -- it gets the RAW bpm
    // (p.fx().set_bpm(_bpm), no _pace) -- but its rhythm READER is corrected
    // for pace via set_rhythm_pace() below: Flux::update_thin_pattern
    // (engine/fx/flux.cpp) multiplies the already-paced gap it counts against
    // by that factor before comparing it to the delay's own real-time period,
    // so the thinning ratio means what it meant before PACE existed (spec
    // 2026-08-12 §3.3).
    void _apply_tempo();

    std::array<Part, PART_COUNT> _parts;
    AmbientReverb* _reverb = nullptr;
    // Per-deck equal-power mix (spec 2026-07-23): dry rides cos, the wet SEND
    // rides sin, one shared room. Indexed by PART_A / PART_B.
    float   _rev_dry_target[PART_COUNT] = { 1.f, 1.f };  // exact endpoints
    float   _rev_wet_target[PART_COUNT] = { 0.f, 0.f };
    OnePole _rev_dry[PART_COUNT], _rev_wet[PART_COUNT];  // 10 ms glide per deck
    bool    _rev_primed = false;    // first process() snaps the mix gains
    bool    _rev_asleep = false;    // both decks dry: room cleared, process() skipped
    // Return fade for clear-on-sleep: the wet gains ride the SEND, so the
    // moment they hit zero the room still holds a tail seconds long. Fade
    // the RETURN to zero first, then clear (audit 2026-08-04, finding 3).
    float _rev_return_gain = 1.f;
    float _rev_return_step = 0.f;
    // Bloom duck (spec 2026-08-03-reverb-bloom-duck): while the room is over
    // unity loop gain its return envelope pulls the dry bus back. Exactly
    // 1.0 whenever it is not ducking -- guarded by a test, like the return
    // ceiling's limiter_gain().
    float _duck_gain = 1.f, _duck_target = 1.f;
    // The residual itself (_duck_gain - _duck_target), maintained as its own
    // float and decayed by a pure multiply each sample: `_duck_residual *=
    // keep`. This is the state; `_duck_gain` is only ever a read-out
    // (`_duck_target + _duck_residual`), recomputed fresh and never fed back.
    // That split matters -- rebuilding the residual every sample by
    // subtracting target from an already-rounded _duck_gain is algebraically
    // the same update, but the intervening addition rounds the residual to
    // target's own ulp (~1.2e-7 near 1.0) before the next sample can act on
    // it, so it stalls at the exact same point the naive additive form
    // g += c*(t-g) does (measured and float32-simulated: both park ~0.994
    // forever after a bloom at 48 kHz). A pure multiply has no such
    // absorption step, so this one actually reaches the target in finite
    // time. Re-based onto a moved target in process() -- see there.
    float _duck_residual = 0.f;
    // Residual-form one-pole coefficients (the fraction of the gap KEPT each
    // sample): _duck_residual *= keep. Set in init().
    float _duck_keep_down = 1.f, _duck_keep_up = 1.f;
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
    float _pace = 1.f;
};

} // namespace spky
