// tests/test_flow_transfer_diff.cpp
//
// THE TRANSFER RIG: what a Fireflow patch actually sounds like once it is
// carried onto a Glow pad, measured at the engine rather than at the converter.
//
// Everything that already existed checked one half of the trip. The bridge
// suite (test_flow_patch_bridge.cpp) checks that to_flow_base fills the right
// overlay slots and writes the right notes; the overlay suite
// (test_flow_overlay.cpp) checks that generate() honours a BaseOverlay. Neither
// asks the question the owner asks, which is whether the pad sounds like the
// patch -- and that question is only answerable downstream of both, after
// wake() has pushed everything into a live Instrument.
//
// The failure that motivated this file is the shape of failure it exists to
// catch: a patch whose RATE transferred exactly and whose modulation still ran
// four times too fast, because a lane's Hz is built from RATE *and* TIDE *and*
// kLaneRatio *and* the sync flag, and only the first of those was carried. A
// parameter-level check reports that transfer as perfect. It is not.
//
// So the rig compares in engine units -- Hz, tone counts, clocking mode -- and
// against a REFERENCE instrument driven with the same values directly, rather
// than against numbers written down here. A hard-coded expectation would have
// to be recomputed by hand every time the rate curve or the lane ratios move,
// which is how a rig quietly stops measuring anything.
#include "doctest/doctest.h"
#include <cmath>
#include <string>
#include "flow/flow.h"
#include "flow/taste.h"
#include "flow/terrain.h"
#include "mod/lane_id.h"
#include "pitch/chord.h"
#include "vcv/src/flow_patch_bridge.hpp"
using namespace spky;
using namespace spky::flow;
using namespace spkyvcv;

namespace {

// A patch shaped like something somebody would actually build: both decks on
// carrier engines so the transfer is not rejected, FREE clocking on both sides
// so no deck disagrees with the global grid, chords on deck A, a slow drifting
// texture on deck B. The values matter less than that they are all DISTINCT --
// a rig built on a patch of zeros passes while reading the wrong slot.
FireflowPatch ambient_patch() {
    FireflowPatch fp{};
    fp.p[kFfEngineA] = 0.f;      // SYNTH  (carrier)
    fp.p[kFfEngineB] = 2.f;      // WAVE   (carrier)
    fp.p[kFfCouple]  = 0.20f;    // FREE zone -> P_MODE 0, no grid
    fp.p[kFfStepsA]  = 0.f;      // ... and both decks agree with it
    fp.p[kFfStepsB]  = 0.f;
    fp.p[kFfRateA]   = 0.32f;
    fp.p[kFfRateB]   = 0.18f;
    fp.p[kFfTide]    = 0.30f;    // x0.57 on the texture lanes: slower than x1
    fp.p[kFfColorA]  = 0.70f;    // four tones on deck A
    fp.p[kFfColorB]  = 0.20f;    // two on deck B
    fp.p[kFfSubA]    = 0.45f;
    fp.p[kFfSubB]    = 0.25f;
    fp.p[kFfTuneA]   = 0.55f;
    fp.p[kFfTuneB]   = 0.45f;
    fp.p[kFfShapeA]  = 0.10f;
    fp.p[kFfSmoothA] = 0.80f;
    fp.p[kFfRangeA]  = 0.35f;
    fp.p[kFfModA]    = 0.40f;
    fp.p[kFfAttackA] = 0.85f;
    fp.p[kFfDecayA]  = 0.90f;
    fp.p[kFfScale]   = 5.f;
    fp.p[kFfTempo]   = 0.20f;    // 80 BPM, inside flow's 50..140
    // 0.75 -> x2 (pace_mult, mod/divisions.h): well away from 0.5's neutral
    // no-op, so a rig that forgot to apply PACE cannot pass by coincidence,
    // and still a position a real patch could sit at -- not the extreme
    // x1/32 or x4 ends. PACE is new, and an UNSET (zero) knob would have been
    // its slowest end, not its centre, silently dividing every lane rate
    // below by up to 32; a non-neutral value is what makes that mistake
    // visible instead of merely avoiding it.
    fp.p[kFfPace]    = 0.75f;
    return fp;
}

int tones_at(float color) {
    ChordBuilder cb;
    cb.init();
    cb.set_color(color);
    return cb.note_count();
}

// Wake a Flow on `master` carrying `ov`, and leave every macro at the position
// Glow boots them to. The macros matter even for base-rule questions: a story
// that still owned a carried parameter would only show itself once a knob was
// somewhere other than zero.
void wake_with(Flow& f, uint32_t master, const BaseOverlay& ov) {
    TerrainState st;
    st.master = master;
    f.wake(st, &ov);
    for (int m = 0; m < MACRO_COUNT; ++m) f.set_macro(m, 0.5f);
    f.tick();
}

const int kTextureLanes[4] =
    { LANE_SOURCE, LANE_SIZE, LANE_MOTION, LANE_LEVEL };

// Did the converter warn that the RUNTIME will move this value after it is
// applied? Today that is taste.h's veto bands (P_COMP_B is confined to
// 0.40..0.60, a by-ear ruling) and Glow's TEMPO fader. Such a parameter is
// carried faithfully and still not heard as stored, which is the whole reason
// the report exists -- so the rig reads the report rather than keeping its own
// list of exceptions that could drift out of date.
bool rewritten_at_runtime(const TransferReport& r, int param) {
    for (int i = 0; i < r.note_count; ++i)
        if (r.notes[i].param == param &&
            severe_tag(r.notes[i].reason) &&
            std::string(severe_tag(r.notes[i].reason)) == "REWRITTEN AT RUNTIME")
            return true;
    return false;
}

} // namespace

TEST_CASE("transfer rig: every carried value survives to the engine") {
    // The end-to-end contract, stated once: what to_flow_base put in the
    // overlay is what the instrument is playing. Checked across many masters
    // and at a non-zero macro position, because the way this breaks is a
    // parameter that is BOTH a base rule and a story target -- generate()
    // applies the overlay in stage 3 and stage 4 then overwrites it, so the
    // damage is invisible on the terrain that happens to draw quietly.
    const FireflowPatch fp = ambient_patch();
    const TransferReport r = to_flow_base(fp);
    REQUIRE_FALSE(r.overlay_rejected);

    Instrument in;
    in.init(48000.f);
    Flow f;
    f.init(&in, 100.f);
    for (uint32_t k = 1; k <= 200; ++k) {
        wake_with(f, k * 2654435761u, r.overlay);
        for (int p = 0; p < P_COUNT; ++p) {
            if (!r.overlay.has[p]) continue;
            // P_MODE and P_STEPS_A/B do not travel through apply_param at all
            // -- Flow::push_mode_and_steps owns them, and _pushed carries them
            // untouched -- so param_now is the right reader for every row here.
            CAPTURE(k);
            CAPTURE(std::string(kParams[p].name));
            // A carried value may legitimately be moved by the runtime -- the
            // COMP veto band does exactly that -- but it may never be moved
            // SILENTLY. One condition covers both directions: either the
            // engine is playing what was stored, or the report said in advance
            // that it would not be. An overlay entry that generate() dropped
            // fails this too, which is the case worth having.
            if (f.param_now(p) == doctest::Approx(r.overlay.v[p])) continue;
            CHECK(rewritten_at_runtime(r, p));
        }
    }
}

TEST_CASE("transfer rig: the mod lanes clock at the patch's own speed") {
    // The gate this file was built for. Both decks, all five lanes, in Hz.
    //
    // The reference is a second instrument driven THE WAY FIREFLOW DRIVES IT,
    // straight from the patch's knobs -- Fireflow.cpp pushes RATE and TIDE
    // with no transform (`set_rate(p, pp(RATE_A, p))`, `set_tide(params
    // [TIDE])`) and takes sync from COUPLE's zone split. Reading the OVERLAY
    // here instead would be the rig's own version of the bug it hunts: an
    // earlier draft did exactly that, and when P_TIDE was deliberately
    // sabotaged in the converter the reference followed the sabotage and the
    // gate stayed green. The reference has to stand outside the thing under
    // test or it measures nothing.
    const FireflowPatch fp = ambient_patch();
    const TransferReport r = to_flow_base(fp);
    REQUIRE(r.overlay.has[P_TIDE]);
    REQUIRE(r.overlay.has[P_RATE_A]);
    REQUIRE(r.overlay.has[P_RATE_B]);

    Instrument ref;
    ref.init(48000.f);
    const bool synced = fp.p[kFfCouple] >= 0.5f;
    ref.set_sync(synced);
    ref.set_tide(fp.p[kFfTide]);
    // set_pace THE WAY FIREFLOW DRIVES IT (Fireflow.cpp:962), straight off the
    // knob -- PACE is a direct conversion, but this reference has to prove
    // that, not assume it. Without this line _pace defaults to 1.f and the
    // gate agrees with the transferred instrument by coincidence at 0.5; the
    // fixture above is pinned away from 0.5 for exactly this reason.
    ref.set_pace(fp.p[kFfPace]);
    ref.set_rate(PART_A, fp.p[kFfRateA]);
    ref.set_rate(PART_B, fp.p[kFfRateB]);
    ref.set_step(PART_A, synced, 8);
    ref.set_step(PART_B, synced, 8);

    Instrument in;
    in.init(48000.f);
    Flow f;
    f.init(&in, 100.f);
    for (uint32_t k = 1; k <= 200; ++k) {
        wake_with(f, k * 2654435761u, r.overlay);
        for (int p = 0; p < 2; ++p) {
            CAPTURE(k);
            CAPTURE(p);
            CHECK(in.step_mode_for_test(p) == synced);
            for (int i = 0; i < 5; ++i) {
                CAPTURE(i);
                CHECK(in.lane_rate_hz_for_test(p, i) ==
                      doctest::Approx(ref.lane_rate_hz_for_test(p, i)));
            }
        }
    }
}

TEST_CASE("transfer rig: the chords the patch was built with arrive") {
    // COLOR is the chord size (pitch/chord.h), and the tone count is what the
    // owner hears -- so the assertion is in tones, not in COLOR units. The
    // patch asks for four tones on deck A and two on deck B; a transfer that
    // delivered one of each would be the exact symptom that started this work.
    const FireflowPatch fp = ambient_patch();
    const TransferReport r = to_flow_base(fp);
    REQUIRE(r.overlay.has[P_COLOR_A]);
    REQUIRE(r.overlay.has[P_COLOR_B]);
    const int want_a = tones_at(fp.p[kFfColorA]);
    const int want_b = tones_at(fp.p[kFfColorB]);
    REQUIRE(want_a > want_b);           // the patch really does differ per deck

    Instrument in;
    in.init(48000.f);
    Flow f;
    f.init(&in, 100.f);
    for (uint32_t k = 1; k <= 200; ++k) {
        CAPTURE(k);
        wake_with(f, k * 2654435761u, r.overlay);
        CHECK(tones_at(f.param_now(P_COLOR_A)) == want_a);
        CHECK(tones_at(f.param_now(P_COLOR_B)) == want_b);
    }
}

TEST_CASE("transfer rig: the readable diff") {
    // Not a gate -- a READOUT, and the point of the file for a human. Run
    //
    //   ./build/spky_tests.exe -tc="transfer rig: the readable diff" -s
    //
    // and it prints, in engine units, what one terrain does to one patch: the
    // lane speeds of both decks, the tone counts, the clocking, and the tempo
    // that is actually running against the one the patch asked for. That is
    // the answer to "does this pad sound like my patch", in a form that can be
    // read rather than inferred from a parameter dump.
    //
    // It asserts nothing on purpose. A readout that also gated would either be
    // a weak gate or a readout nobody dares change.
    const FireflowPatch fp = ambient_patch();
    const TransferReport r = to_flow_base(fp);

    Instrument in;
    in.init(48000.f);
    Flow f;
    f.init(&in, 100.f);
    wake_with(f, 0xF12E5EEDu, r.overlay);

    int carried = 0;
    for (int p = 0; p < P_COUNT; ++p) if (r.overlay.has[p]) ++carried;
    MESSAGE("carried " << carried << " of " << kBaseRuleCount
            << " base rules; " << r.note_count << " notes");

    for (int p = 0; p < 2; ++p) {
        std::string lanes;
        for (int i = 0; i < 5; ++i) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "%s%.4f",
                          i ? " " : "", in.lane_rate_hz_for_test(p, i));
            lanes += buf;
        }
        MESSAGE("deck " << (p ? "B" : "A")
                << ": lanes(Hz) " << lanes
                << " | tones " << tones_at(f.param_now(p ? P_COLOR_B
                                                         : P_COLOR_A))
                << " | step " << std::string(in.step_mode_for_test(p) ? "on" : "off")
                << " (" << in.deck_steps_for_test(p) << ")");
    }
    // The texture lanes are the ones TIDE scales; naming their ratio against
    // the PITCH lane makes a speed regression readable at a glance instead of
    // requiring five numbers to be divided by hand.
    for (int p = 0; p < 2; ++p) {
        const float pitch = in.lane_rate_hz_for_test(p, LANE_PITCH);
        std::string ratios;
        for (int i = 0; i < 4; ++i) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "%sx%.3f", i ? " " : "",
                          pitch > 0.f
                              ? in.lane_rate_hz_for_test(p, kTextureLanes[i]) / pitch
                              : 0.f);
            ratios += buf;
        }
        MESSAGE("deck " << (p ? "B" : "A") << ": texture/pitch " << ratios);
    }

    MESSAGE("tempo: patch asked " << r.overlay.v[P_TEMPO_BPM]
            << " BPM, terrain is running " << f.param_now(P_TEMPO_BPM)
            << " BPM -- and Glow's TEMPO fader overwrites this every tick");
    MESSAGE("\n" << format_report(r));
}
