#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "instrument.h"
#include "fx/flux.h"
#include "mod/divisions.h"
#include "mod/song_ladder.h"
#include "../bench/audition/init_patch.h"
#include "vcv/src/generated_panel.hpp"
#include "vcv/src/init_patch.hpp"


TEST_CASE("Seed audition applies the VCV init engine and arranger state")
{
    spky::Instrument inst;
    inst.init(48000.f);

    audition::apply_init_patch(inst);

    float in[96] = {};
    float out_l[96] = {};
    float out_r[96] = {};
    for(int block = 0; block < 8; ++block)
        inst.process(in, in, out_l, out_r, 96);

    CHECK(inst.engine_id(spky::PART_A) == spky::ENGINE_WAVE);
    CHECK(inst.engine_id(spky::PART_B) == spky::ENGINE_SYNTH);

    // SONG_A/B's init default is a ladder rung index (spec 2026-08-09
    // hw-control-reduction task 3), and the factory patch asks for rung 0 on
    // deck A and rung 13 on deck B. Both decks boot STEPS_A/B == 0, i.e. FLOW.
    //
    // Before spec 2026-08-13 flow-melody-engine task 8, that meant NEITHER
    // rung landed at boot: ModLane::set_form/set_song only raise
    // pending_form/pending_song (lane.cpp), and the selection changes in
    // _apply_pending_song_work(), reached only from _wrap_events() -- which
    // used to return immediately for a melodic lane outside step mode. Task 8
    // wires Part::init/_engine_swap to push set_flow_melody(true) into the
    // PITCH lane for every note engine (both decks here are WAVE/SYNTH), and
    // _wrap_events() no longer early-returns once the FLOW melody engine is
    // on -- it is the mechanism that walks the phrase slots. So the pending
    // rung now lands on the first control tick, same as it would in STEP.
    //
    // Rung 0 is {Principle::TwoMotif, SongMode::Off} and rung 13 is
    // {Principle::Ostinato, SongMode::Mirror} (song_ladder.h's kLadder).
    // Pinned as the ladder's rungs, not the ladder's index, for the same
    // reason the ladder test itself does not pin order -- see song_ladder.h.
    //
    // The pending-until-wrap semantics have their own real coverage in
    // tests/test_song_lane.cpp, which drives set_step(true, ..) and watches
    // the selection land; SAMPLER/BBD decks keep the old parked behaviour,
    // covered by tests/test_flow_melody_wiring.cpp.
    CHECK(spkyvcv::initParamDefault(spkyvcv::STEPS_A) == doctest::Approx(0.f));
    CHECK(spkyvcv::initParamDefault(spkyvcv::STEPS_B) == doctest::Approx(0.f));
    CHECK(inst.form(spky::PART_A) == static_cast<int>(spky::Principle::TwoMotif));
    CHECK(inst.form(spky::PART_B) == static_cast<int>(spky::Principle::Ostinato));
    CHECK(inst.song(spky::PART_A) == static_cast<int>(spky::SongMode::Off));
    CHECK(inst.song(spky::PART_B) == static_cast<int>(spky::SongMode::Mirror));
}


TEST_CASE("Seed audition shares the complete generated VCV parameter snapshot")
{
    // This literal is expected to move whenever the panel inventory changes
    // -- most of the remaining tasks in the 2026-08-09 hw-control-reduction
    // plan remove more parameters. Update it to the new true count (and
    // nothing else) when that happens; do not carry a mismatch forward as a
    // "pre-existing failure" (spec 2026-08-09 hw-control-reduction task 3
    // review, Finding 5 -- this exact mistake shipped once already).
    // 69 -> 73 on 2026-08-19: DEPTH_A/B and DAMP_A/B, the two FEED knobs that
    // moved out of the context menu onto both panels.
    CHECK_MESSAGE(spkyvcv::NUM_PARAMS == 73,
                  "NUM_PARAMS is " << spkyvcv::NUM_PARAMS << ", want 73 -- "
                  "if the panel inventory genuinely changed, update this "
                  "literal to match");
    // FF_hw_Init.vcvm (2026-08-09) pairs WAVE (2) on deck A with SYNTH (0) on
    // deck B, where the drone.vcvm lineage before it paired SYNTH with BODY.
    // Both ENG pins must track the snapshot exactly, because
    // bench/audition/init_patch.cpp dispatches off this same table -- and a
    // number here that the dispatcher has no arm for boots as SAMPLER in
    // silence (see "boots the same engines the VCV host does" below).
    CHECK(spkyvcv::initParamDefault(spkyvcv::ENGINE_A)
          == doctest::Approx(2.f));
    CHECK(spkyvcv::initParamDefault(spkyvcv::ENGINE_B)
          == doctest::Approx(0.f));
    // Bottom of the tempo range: bpm == 40 + 0 * 200 == 40.
    CHECK(spkyvcv::initParamDefault(spkyvcv::TEMPO)
          == doctest::Approx(0.f));
    // DETUNE is a plain per-deck knob position again. It stopped needing a
    // derivation the moment no deck booted BODY: the old value here was
    // solved as sqrt(24 / 140) to hold BODY's 24 ct through its private
    // kDetuneScale, and with SYNTH on deck B there is no private scale left
    // to solve against. The engine-side consequence (0.456^2 * 105 == 21.83
    // ct reaching the oscillators) is pinned in the golden test below; this
    // is only the raw snapshot value.
    CHECK(spkyvcv::initParamDefault(spkyvcv::DETUNE_B)
          == doctest::Approx(0.455999434f));
    // LINK's default is the branch's one load-bearing product invariant:
    // centre (0) is the bit-identical path (spec 2026-07-28 flux-link).
    CHECK(spkyvcv::initParamDefault(spkyvcv::LINK_A) == doctest::Approx(0.f));
    CHECK(spkyvcv::initParamDefault(spkyvcv::LINK_B) == doctest::Approx(0.f));
    // FLUXRATE_A/B are raw 12-detent indices since task 6 (spec 2026-08-09
    // hw-control-reduction) replaced the normalized float that used to be run
    // through flux_division_index(). Both decks sit on index 1 == "1/4.".
    //
    // The conversion this file used to prove -- that the old normalized
    // defaults 0.392727494 / 0.254666120 round to indices 4 / 3 and reproduce
    // the same delay time -- is gone with the patch that carried those
    // numbers. Nothing in production calls flux_division_index() any more
    // (only comments name it), and its endpoints stay covered by
    // tests/test_flux.cpp.
    CHECK(spkyvcv::initParamDefault(spkyvcv::FLUXRATE_A)
          == doctest::Approx(1.f));
    CHECK(spkyvcv::initParamDefault(spkyvcv::FLUXRATE_B)
          == doctest::Approx(1.f));
}


// The FLUXRATE equivalence case that used to sit here is deliberately gone,
// not moved. It built one instrument from the OLD normalized default run
// through flux_division_index() and a second from apply_init_patch(), and
// required identical tape delay targets -- proving task 6's float-to-index
// conversion did not move the factory delay. FF_hw_Init.vcvm (2026-08-09)
// retired the snapshot those normalized numbers belonged to, so there is no
// longer an "old formula" reading of the current patch to compare against.
// Rebuilding it against hard-coded indices would have compared set_flux_rate
// with itself: a test that cannot go red.


TEST_CASE("Seed audition boots the same engines the VCV host does")
{
    spky::Instrument inst;
    inst.init(48000.f);
    audition::apply_init_patch(inst);

    // set_engine() only arms a click-free swap; it completes inside
    // process() at the idle point, so engine_id() must be read after
    // running audio, exactly as the sibling test case above does.
    float in[96] = {};
    float out_l[96] = {};
    float out_r[96] = {};
    for(int block = 0; block < 8; ++block)
        inst.process(in, in, out_l, out_r, 96);

    // The snapshot puts deck A on WAVE and deck B on SYNTH (FF_hw_Init.vcvm,
    // 2026-08-09). The previous one paired SYNTH with BODY.
    //
    // What this case is really guarding has not changed with the patch: every
    // engine the snapshot can name needs its own explicit arm in the
    // dispatcher, and anything unrecognised falls through to SAMPLER. Before
    // 2026-07-31 there was no BODY arm and deck B silently booted as SAMPLER
    // -- audible on the Seed, invisible in every test. WAVE is arm 2 and has
    // been since it was added; this is the first factory patch to use it.
    CHECK(inst.engine_id(spky::PART_A) == spky::ENGINE_WAVE);
    CHECK(inst.engine_id(spky::PART_B) == spky::ENGINE_SYNTH);
}

TEST_CASE("Seed audition dispatcher routes generated STAGES by generated engine")
{
    std::array<float, spkyvcv::NUM_PARAMS> snapshot{};
    std::copy(std::begin(spkyvcv::kInitParamDefaults),
              std::end(spkyvcv::kInitParamDefaults), snapshot.begin());
    snapshot[spkyvcv::ENGINE_A] = 4.f;  // BBD
    snapshot[spkyvcv::ENGINE_B] = 1.f;  // SAMPLER (non-BBD, unquantized pitch)
    snapshot[spkyvcv::STAGES_A] = 0.8125f;
    snapshot[spkyvcv::STAGES_B] = 0.9375f;
    snapshot[spkyvcv::TUNE_A] = 0.5f;
    snapshot[spkyvcv::TUNE_B] = 0.5f;

    spky::Instrument inst;
    inst.init(48000.f);
    audition::apply_init_patch(inst, snapshot.data());

    // Make pitch_cv expose the dispatched bases without lane modulation.
    inst.set_target_active(spky::PART_A, spky::LANE_PITCH, false);
    inst.set_target_active(spky::PART_B, spky::LANE_PITCH, false);
    float l = 0.f, r = 0.f;
    for(int i = 0; i < 4000; ++i)
        inst.process(nullptr, nullptr, &l, &r, 1);

    CHECK(inst.engine_id(spky::PART_A) == spky::ENGINE_BBD);
    CHECK(inst.engine_id(spky::PART_B) == spky::ENGINE_SAMPLER);
    // Raw, unquantized STAGES_A dispatched straight to pitch_cv -- this
    // depends on the deck booting in FLOW (STEPS_A's default is 0, i.e.
    // step mode off, spec 2026-08-09 hw-control-reduction task 3 review,
    // Finding 7). part.cpp's _pitch_q rule quantizes a BBD deck's pitch to
    // the scale grid whenever _step_on is true; only FLOW leaves it
    // continuous. If STEPS_A's default ever moves off 0 again, this value
    // will too -- see the same review's Finding 6 for the quantized number
    // and the full mechanism trace.
    CHECK(inst.pitch_cv(spky::PART_A) == doctest::Approx(0.8125f));
    CHECK(inst.pitch_cv(spky::PART_B) == doctest::Approx(0.5f));
}

// GRIT is one bipolar knob (spec 2026-08-09 hw-control-reduction task 4):
// sign picks Reduce/Drive, magnitude (past a 0.03 dead zone) is the mix.
// Task 4's review flagged that nothing exercised this mapping end to end --
// the panel guard only substring-checks Fireflow.cpp's source text, so a
// regression that declares kGritDead but stops wiring it into the fx_on
// gate (exactly the bug task 4 fixed) would still pass every existing
// check. These cases drive bench/audition/init_patch.cpp's
// apply_init_patch -- the same sign/magnitude/dead-zone mapping
// Fireflow.cpp's pushParams performs, reachable here from a doctest -- and
// read the engine back through the grit_*_for_test() observers
// (engine/instrument.h), not through source text.
namespace {
// Mirrors Fireflow.cpp's kGritDead / bench/audition/init_patch.cpp's
// kGritDead. res/test_panel.py's test_grit_dead_zone_constant_agrees
// keeps those two -- and only those two -- in sync; this local copy is a
// third, independent expectation, not a fourth source of truth to sync.
constexpr float kGritDeadForTest = 0.03f;
}  // namespace

TEST_CASE("Seed audition: negative GRIT engages Reduce and leaves the effect on")
{
    std::array<float, spkyvcv::NUM_PARAMS> snapshot{};
    std::copy(std::begin(spkyvcv::kInitParamDefaults),
              std::end(spkyvcv::kInitParamDefaults), snapshot.begin());
    snapshot[spkyvcv::GRIT_A] = -0.5f;

    spky::Instrument inst;
    inst.init(48000.f);
    audition::apply_init_patch(inst, snapshot.data());

    // This is the exact case the task-4 fix rescued: set_fx_on's old
    // `pp(GRIT_A, p) > 1e-4f` gate tested the RAW (now signed) value, so
    // every negative GRIT value failed it and Reduce/CRSH was silently
    // muted no matter how far left the knob turned. If that regresses,
    // this CHECK goes red.
    CHECK(static_cast<int>(inst.grit_mode_for_test(spky::PART_A))
          == static_cast<int>(spky::GritMode::Reduce));
    CHECK(inst.grit_engaged_for_test(spky::PART_A));
}

TEST_CASE("Seed audition: positive GRIT engages Drive and leaves the effect on")
{
    std::array<float, spkyvcv::NUM_PARAMS> snapshot{};
    std::copy(std::begin(spkyvcv::kInitParamDefaults),
              std::end(spkyvcv::kInitParamDefaults), snapshot.begin());
    snapshot[spkyvcv::GRIT_B] = 0.5f;

    spky::Instrument inst;
    inst.init(48000.f);
    audition::apply_init_patch(inst, snapshot.data());

    CHECK(static_cast<int>(inst.grit_mode_for_test(spky::PART_B))
          == static_cast<int>(spky::GritMode::Drive));
    CHECK(inst.grit_engaged_for_test(spky::PART_B));
}

TEST_CASE("Seed audition: GRIT at the dead-zone boundary is off with zero mix, both signs")
{
    std::array<float, spkyvcv::NUM_PARAMS> snapshot{};
    std::copy(std::begin(spkyvcv::kInitParamDefaults),
              std::end(spkyvcv::kInitParamDefaults), snapshot.begin());
    snapshot[spkyvcv::GRIT_A] = -kGritDeadForTest;
    snapshot[spkyvcv::GRIT_B] = kGritDeadForTest;

    spky::Instrument inst;
    inst.init(48000.f);
    audition::apply_init_patch(inst, snapshot.data());

    CHECK_FALSE(inst.grit_engaged_for_test(spky::PART_A));
    CHECK(inst.grit_mix_for_test(spky::PART_A) == doctest::Approx(0.f));
    CHECK_FALSE(inst.grit_engaged_for_test(spky::PART_B));
    CHECK(inst.grit_mix_for_test(spky::PART_B) == doctest::Approx(0.f));
}

TEST_CASE("Seed audition: full-deflection GRIT reaches mix 1.0, both signs")
{
    std::array<float, spkyvcv::NUM_PARAMS> snapshot{};
    std::copy(std::begin(spkyvcv::kInitParamDefaults),
              std::end(spkyvcv::kInitParamDefaults), snapshot.begin());
    snapshot[spkyvcv::GRIT_A] = -1.f;
    snapshot[spkyvcv::GRIT_B] = 1.f;

    spky::Instrument inst;
    inst.init(48000.f);
    audition::apply_init_patch(inst, snapshot.data());

    CHECK(inst.grit_mix_for_test(spky::PART_A) == doctest::Approx(1.f));
    CHECK(inst.grit_mix_for_test(spky::PART_B) == doctest::Approx(1.f));
}

// Review finding IMPORTANT 6 (2026-08-09 hw-control-reduction final review):
// four times in this branch a control changed meaning while its stored init
// value stayed put, silently changing how the factory patch sounds --
// caught each time only because a human reasoned it through. The FLUXRATE
// case above is the one exception: a bespoke test compares the old
// formula's delay time against the new one. This generalises that idea into
// one golden test that pins every engine-side observable the owner would
// actually hear, applying the SAME snapshot apply_init_patch() gives the
// real host. Every expected number below is a LITERAL, computed by hand
// from the formulas in bench/audition/init_patch.cpp / Fireflow.cpp's
// pushParams as they stand today, NOT re-derived from the same constants
// the production code uses -- if it referenced e.g. spky::SynthEngine::
// kDetuneCeilCt directly, a ceiling change would move both sides together
// and this test could never catch it. A future control merge that changes
// what the factory patch sounds like must change a number here and justify
// why, instead of depending on somebody noticing by ear.
TEST_CASE("Seed audition's factory init pins every engine-side observable "
          "a future control merge could silently move")
{
    // FLUX needs real echo memory to report a delay target (Flux::
    // delay_target_for_test() is a no-op otherwise -- see the FLUXRATE case
    // above). The reverb, sampler and BBD buffers are left null: this test
    // does not touch any of them, and FxMem documents null as "that part
    // runs silent," not a crash.
    struct TapeMem {
        std::vector<float> echo[spky::PART_COUNT][2];
        TapeMem() {
            for (int p = 0; p < spky::PART_COUNT; ++p)
                for (int ch = 0; ch < 2; ++ch)
                    echo[p][ch].resize(spky::Flux::kMaxSamples);
        }
        spky::FxMem mem() {
            spky::FxMem m;
            for (int p = 0; p < spky::PART_COUNT; ++p)
                for (int ch = 0; ch < 2; ++ch)
                    m.echo[p][ch] = echo[p][ch].data();
            return m;
        }
    };
    static TapeMem tape;
    static spky::AmbientReverb reverb;

    spky::Instrument inst;
    spky::FxMem mem = tape.mem();
    mem.reverb = &reverb;
    inst.init(48000.f, mem);
    audition::apply_init_patch(inst);

    float in[96] = {};
    float out_l[96] = {};
    float out_r[96] = {};
    // DRIFT glides through a 0.3 s OnePole (Center::init), unlike the
    // instantaneous controls checked below -- run several time constants'
    // worth of blocks (2500 * 96 samples == 5 s) so drift() has actually
    // converged, the same wait test_center.cpp uses for the same reason.
    for (int block = 0; block < 2500; ++block)
        inst.process(in, in, out_l, out_r, 96);

    // --- per-deck grit mode and mix: GRIT_A == 0.173493922, positive and ---
    // past the 0.03 dead zone, so deck A boots with DRV audible:
    // (0.173493922 - 0.03) / (1 - 0.03) == 0.147932. Deck B is at 0.0, inside
    // the dead zone -- present but silent. Mode is Drive on both because the
    // mode follows the SIGN, and neither knob is negative.
    CHECK(static_cast<int>(inst.grit_mode_for_test(spky::PART_A))
          == static_cast<int>(spky::GritMode::Drive));
    CHECK(static_cast<int>(inst.grit_mode_for_test(spky::PART_B))
          == static_cast<int>(spky::GritMode::Drive));
    CHECK(inst.grit_mix_for_test(spky::PART_A)
          == doctest::Approx(0.147932f).epsilon(0.0001));
    CHECK(inst.grit_mix_for_test(spky::PART_B) == doctest::Approx(0.f));

    // --- flux delay target: the tape echo time TIME/FLUXRATE resolves to. ---
    // TEMPO == 0.0 is the bottom of the range, so bpm == 40 + 0 * 200 == 40.
    // Both decks sit at FLUXRATE index 1, which is kDivisions[5 + 1] == "1/4."
    // at cpb 2/3: hz == (40/60) * (2/3) == 4/9, delay == 9/4 == 2.25 s exactly.
    // Well inside the 5.46 s the 262144-sample tape holds at 48 kHz, so the
    // clamp in update_time_target never bites and this is the raw figure.
    CHECK(inst.flux_delay_target_for_test(spky::PART_A)
          == doctest::Approx(2.25f).epsilon(0.001));
    CHECK(inst.flux_delay_target_for_test(spky::PART_B)
          == doctest::Approx(2.25f).epsilon(0.001));

    // --- form and song per deck: the snapshot asks for ladder rung 0 on ---
    // deck A and rung 13 on deck B, and (since spec 2026-08-13
    // flow-melody-engine task 8 wires set_flow_melody(true) into every note
    // engine's PITCH lane) both now land at boot: _wrap_events() no longer
    // early-returns for a melodic lane outside step mode once the FLOW
    // melody engine is on, so the pending rung applies on the first control
    // tick. See the long note in "applies the VCV init engine and arranger
    // state" above for the full mechanism.
    //
    // Pinned as the ladder's rungs (song_ladder.h's kLadder), not literals
    // derived from SongState's boot default: rung 0 is {TwoMotif, Off},
    // rung 13 is {Ostinato, Mirror}.
    CHECK(inst.form(spky::PART_A) == static_cast<int>(spky::Principle::TwoMotif));
    CHECK(inst.form(spky::PART_B) == static_cast<int>(spky::Principle::Ostinato));
    CHECK(inst.song(spky::PART_A) == static_cast<int>(spky::SongMode::Off));
    CHECK(inst.song(spky::PART_B) == static_cast<int>(spky::SongMode::Mirror));

    // --- per-part level and compressor amount: both decks boot INSIDE ---
    // the comp zone now, which no earlier factory patch did. Level clamps to
    // unity for anything at or above kLvlCompSplit (0.6), and the amount is
    // kCompTop * ((knob - 0.6) / 0.4) ^ kCompShape:
    //   deck A  0.761333168 -> 0.7 * (0.403333) ^ 0.6 == 0.405972
    //   deck B  0.848000109 -> 0.7 * (0.620000) ^ 0.6 == 0.525452
    // i.e. roughly +7.1 dB and +10.9 dB of make-up at boot. These two numbers
    // are only meaningful together with kCompShape: reshape the taper and the
    // same knob positions give different amounts, which is exactly what this
    // pin is here to force somebody to notice.
    CHECK(inst.part_level_for_test(spky::PART_A) == doctest::Approx(1.f));
    CHECK(inst.part_level_for_test(spky::PART_B) == doctest::Approx(1.f));
    CHECK(inst.comp_amount_for_test(spky::PART_A)
          == doctest::Approx(0.405972f).epsilon(0.0001));
    CHECK(inst.comp_amount_for_test(spky::PART_B)
          == doctest::Approx(0.525452f).epsilon(0.0001));

    // --- applied detune cents per deck: deck A boots WAVE, deck B SYNTH. ---
    // Both are SynthEngineT<VoiceT<..>>, so both read the same law and no
    // engine-private scale sits in between (the kDetuneScale caveat that used
    // to belong here was BODY's, and no deck boots BODY any more).
    //
    // The host pushes DETUNE SQUARED (Fireflow.cpp: set_voice_detune(knob *
    // knob), a quadratic taper so the fine beating near zero is not squeezed
    // into a fifth of the travel at a 105 ct ceiling), then set_detune
    // multiplies by kDetuneCeilCt:
    //   deck A  0.377333373^2 * 105 == 14.9500 ct
    //   deck B  0.455999434^2 * 105 == 21.8332 ct
    CHECK(inst.applied_detune_ct_for_test(spky::PART_A)
          == doctest::Approx(14.9500f).epsilon(0.0001));   // deck A (WAVE)
    CHECK(inst.applied_detune_ct_for_test(spky::PART_B)
          == doctest::Approx(21.8332f).epsilon(0.0001));   // deck B (SYNTH)

    // --- scale: SCALE == 3 is SCALE_LYDIAN, mask 0x0AD5 (0 2 4 6 7 9 11). ---
    // The mask is the literal, not SCALE_MASKS[SCALE_LYDIAN]: reading the same
    // table set_scale() reads would let a retuned mask move both sides at once.
    //
    // This pin exists because SCALE is the control that already got away. Its
    // configParam hard-coded spky::SCALE_LYDIAN instead of the snapshot value,
    // so the panel booted Lydian while INIT_DEFAULTS said 2 (Mixolydian) --
    // two boot scales for one instrument, and nothing here noticed, because
    // nothing read a mask back. res/test_panel.py guards the host branch;
    // this guards the engine state it produces.
    CHECK(inst.scale_mask_for_test(spky::PART_A) == 0x0AD5);
    CHECK(inst.scale_mask_for_test(spky::PART_B) == 0x0AD5);

    // --- drift, couple, sync ---
    // COUPLE == 1.0 (top of travel): grid zone, SYNC on, couple == 1.0 (the
    // texture lanes lock fully to the grid).
    CHECK(inst.synced(spky::PART_A) == true);
    CHECK(inst.synced(spky::PART_B) == true);
    CHECK(inst.couple() == doctest::Approx(1.f));
    // DRIFT == 0.791999996, past the 0.02 SETL zone: (0.791999996 - 0.02) /
    // (1 - 0.02) == 0.787755 -- a moderate weather walk, calmer than the
    // near-maxed 0.958667 the previous factory patch booted at.
    CHECK(inst.drift() == doctest::Approx(0.787755f).epsilon(0.0001));

    // --- master drive, reverb smear, reverb mod: fixed-by-ear constants ---
    // (spec 2026-08-09 hw-control-reduction task 9). PUSH == 0.40 ->
    // Limiter's pre-gain 1 + 3*0.4*0.4 == 1.48 (a warm, not transparent,
    // ceiling). SMEAR == 0.30 (a moderate wash), WOBL/MOD == 0.15 (gentle
    // tail wobble).
    CHECK(inst.master_drive_pre_gain_for_test() == doctest::Approx(1.48f));
    CHECK(inst.reverb_smear_for_test() == doctest::Approx(0.30f));
    CHECK(inst.reverb_mod_for_test() == doctest::Approx(0.15f));
}
