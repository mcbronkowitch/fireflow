// The song offset: a song rebuilt after a ROOT turn starts from the new root.
// Spec: docs/superpowers/specs/2026-10-18-root-and-wobl-knobs-design.md §2.1
//
// A ROOT turn alone only moves the scale grid (Quantizer::set_root). What this
// file pins is the second half of the decision: each note deck latches a
// signed semitone offset, folded from its root, at every FULL rebuild of the
// phrase and at init, and adds it to the pitch before quantizing. Because the
// quantizer's mask is root-relative, a song generated in D is then the C song
// moved up two semitones -- exactly, not approximately (measured, spec §3).
//
// Bit-identity at root 0 is NOT tested here. A twin comparison at root 0 would
// run the same code on both sides and could never go red; the guard is the
// existing ctrl_identity render hash, whose RED was proven once with a 1e-7f
// sabotage in Part::_offset_norm() (task-2 report).
#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include "instrument.h"
using namespace spky;

namespace {

constexpr int   kBlock   = 96;
constexpr int   kBlocksPerSec = 48000 / kBlock;   // 500
constexpr float kEps     = 1e-3f;                 // semitones, absolute

std::unique_ptr<Instrument> make_rig() {
    auto inst = std::make_unique<Instrument>();
    inst->init(48000.f);
    inst->set_sync(true);
    inst->set_step(PART_A, true, 8);
    return inst;
}

void block(Instrument& inst) {
    static float l[kBlock], r[kBlock];
    inst.process(nullptr, nullptr, l, r, kBlock);
}

// The quantized pitch the engine receives, in semitones of the 36-semi span.
float semis(const Instrument& inst) {
    return inst.target_value(PART_A, LANE_PITCH) * Quantizer::SPAN_SEMIS;
}

// Render (every given rig in lockstep) until `inst` reports `want` as its
// latched offset. Bounded, so a missing latch fails instead of hanging.
template <typename... Rest>
bool render_until_offset(Instrument& inst, int want, float max_s, Rest&... rest) {
    const int max_blocks = static_cast<int>(max_s * kBlocksPerSec);
    for (int b = 0; b < max_blocks; ++b) {
        if (inst.song_offset_for_test(PART_A) == want) return true;
        block(inst);
        (block(rest), ...);
    }
    return inst.song_offset_for_test(PART_A) == want;
}

template <typename... All>
void render_s(float s, All&... all) {
    const int n = static_cast<int>(s * kBlocksPerSec);
    for (int b = 0; b < n; ++b) (block(all), ...);
}

// Engine swaps fade first and land at the idle point in process(), so wait
// for the observer instead of assuming a block count.
template <typename... All>
bool render_until_engine(EngineId e, All&... all) {
    for (int b = 0; b < 5 * kBlocksPerSec; ++b) {
        if (((all.engine_id(PART_A) == e) && ...)) return true;
        (block(all), ...);
    }
    return ((all.engine_id(PART_A) == e) && ...);
}

uint32_t builds(const Instrument& inst) {
    return inst.part(PART_A).mod().pitch_phrase_builds();
}

// Compare X against Y for `s` seconds, block by block, and require X to sit
// exactly `want` semitones above Y wherever both are clear of the span edges
// (at the edges an offset song is mirrored at the span edge -- spec §3,
// measured; spec 2026-10-26 pitch-range-law §4.2). Returns the number of
// blocks compared. Any further rigs are rendered in lockstep alongside.
template <typename... Rest>
int compare_distance(Instrument& x, Instrument& y, float want, float s, Rest&... rest) {
    const int n = static_cast<int>(s * kBlocksPerSec);
    int compared = 0, bad = 0;
    float worst = 0.f;
    for (int b = 0; b < n; ++b) {
        block(x);
        block(y);
        (block(rest), ...);
        const float px = semis(x), py = semis(y);
        if (px < 1.f || px > 35.f || py < 1.f || py > 35.f) continue;
        ++compared;
        const float err = std::fabs((px - py) - want);
        if (err >= kEps) { ++bad; if (err > worst) worst = err; }
    }
    CAPTURE(want);
    CAPTURE(worst);
    CHECK(bad == 0);
    return compared;
}

// The pitch-level half of spec §5's first anti-goal: a ROOT turn is not a
// transpose. `turned` is a deck turned to D WITHOUT a rebuild, `c_song` the
// root-0 C song on the same phrase. The turn may only re-snap each note onto
// D's grid -- at most one semitone away from the C song -- and must never sit
// the full root distance (2) above it, which is what a transpose would do.
// song_offset_for_test() alone cannot see this: it reports the latch, not
// what pitch_pre_quant() reads, so an _offset_norm() regressed to the LIVE
// root would leave every offset check green while each turn moved the melody.
// Same span-edge skip as compare_distance(). Returns the blocks compared.
// Measured 2026-10-19 in this rig (boot scale Dorian, the first SYNTH leg of
// both loop iterations below): worst
// |Y - Z| exactly 1 semitone, 0 of 2000 ticks at +2. With _offset_norm()
// sabotaged to read the live root: 2000 of 2000 ticks at exactly +2.
template <typename... Rest>
int check_turn_is_not_transpose(Instrument& turned, Instrument& c_song, float s,
                                Rest&... rest) {
    const int n = static_cast<int>(s * kBlocksPerSec);
    int compared = 0, over = 0, transposed = 0;
    float worst = 0.f;
    for (int b = 0; b < n; ++b) {
        block(turned);
        block(c_song);
        (block(rest), ...);
        const float pt = semis(turned), pc = semis(c_song);
        if (pt < 1.f || pt > 35.f || pc < 1.f || pc > 35.f) continue;
        ++compared;
        const float d = pt - pc;
        if (std::fabs(d) > worst) worst = std::fabs(d);
        if (std::fabs(d) > 1.f + kEps) ++over;
        if (std::fabs(d - 2.f) < kEps) ++transposed;
    }
    CAPTURE(worst);
    CAPTURE(over);
    CAPTURE(transposed);
    CAPTURE(compared);
    CHECK(over == 0);
    CHECK(transposed < compared);
    return compared;
}

} // namespace

TEST_CASE("song offset: fold table") {
    CHECK(song_offset_for_root(0)  ==  0);
    CHECK(song_offset_for_root(1)  ==  1);
    CHECK(song_offset_for_root(6)  ==  6);
    CHECK(song_offset_for_root(7)  == -5);
    CHECK(song_offset_for_root(11) == -1);
    CHECK(song_offset_for_root(12) ==  0);
    CHECK(song_offset_for_root(-1) == -1);
    CHECK(song_offset_for_root(23) == -1);
}

TEST_CASE("song offset: a rebuilt song sits exactly the root distance away") {
    auto t1 = make_rig();
    auto t2 = make_rig();
    t1->set_root(PART_A, 0);
    t2->set_root(PART_A, 2);
    t1->new_phrase(PART_A);
    t2->new_phrase(PART_A);
    REQUIRE(render_until_offset(*t2, 2, 30.f, *t1));
    REQUIRE(t1->song_offset_for_test(PART_A) == 0);
    // Both twins rebuilt on the same sample (one new_phrase each, one RNG
    // stream each), so from here on they play the same phrase.
    REQUIRE(builds(*t1) == builds(*t2));
    // The first new note glides out of the OLD song's last note, which was
    // quantized in C on one twin and in D on the other, so the quantizer's
    // slew and hysteresis start from different places for the length of that
    // glide. Not part of the rebuilt song; skipped. (Measured 2026-10-19
    // without the skip: 18 of the 8 s window's ticks off, worst 0.9 semi --
    // 36 ms, the length of the quantizer's 40 ms change slew.)
    render_s(0.5f, *t1, *t2);
    const int compared = compare_distance(*t2, *t1, 2.f, 8.f);
    CHECK(compared >= 100);
}

TEST_CASE("song offset: a root turn alone does not re-latch") {
    auto inst = make_rig();
    inst->set_root(PART_A, 2);
    inst->new_phrase(PART_A);
    REQUIRE(render_until_offset(*inst, 2, 30.f));

    // The turn moves the grid only; the running song keeps its offset until
    // the player causes a rebuild (spec §5: ROOT itself raises no new_pending).
    inst->set_root(PART_A, 4);
    const uint32_t before = builds(*inst);
    render_s(10.f, *inst);
    CHECK(builds(*inst) == before);
    CHECK(inst->song_offset_for_test(PART_A) == 2);

    inst->new_phrase(PART_A);
    CHECK(render_until_offset(*inst, 4, 30.f));

    inst->set_root(PART_A, 7);
    inst->new_phrase(PART_A);
    CHECK(render_until_offset(*inst, -5, 30.f));

    inst->set_root(PART_A, 6);
    inst->new_phrase(PART_A);
    CHECK(render_until_offset(*inst, 6, 30.f));
}

TEST_CASE("song offset: applies only on note engines") {
    for (EngineId other : { ENGINE_BBD, ENGINE_SAMPLER }) {
        CAPTURE(int(other));
        // X latches +2 at its rebuild. Y latches 0 at the same rebuild and is
        // turned to root 2 afterwards, without one. Both now run root 2 on the
        // same phrase; only the latched offset differs.
        //
        // Z is the reference for the note-engine legs: root 0, offset 0, the
        // same phrase -- the C song. X - Y is NOT a fixed 2 on a note engine:
        // Y quantizes the unmoved song onto D's grid, i.e. a ROOT turn, which
        // moves each note by 0 or 1 semitone (measured 2026-10-19 with Y as
        // the reference: 496 of 2000 ticks off, worst exactly 1 semitone, at
        // the quantizer's boot scale Dorian). Only X against the C song is an
        // exact 2, and that is the claim (spec §4 goal 7).
        auto x = make_rig();
        auto y = make_rig();
        auto z = make_rig();
        x->set_root(PART_A, 2);
        x->new_phrase(PART_A);
        y->new_phrase(PART_A);
        z->new_phrase(PART_A);
        REQUIRE(render_until_offset(*x, 2, 30.f, *y, *z));
        REQUIRE(y->song_offset_for_test(PART_A) == 0);
        REQUIRE(z->song_offset_for_test(PART_A) == 0);
        y->set_root(PART_A, 2);
        const uint32_t b0 = builds(*x);
        REQUIRE(builds(*y) == b0);
        REQUIRE(builds(*z) == b0);

        render_s(0.5f, *x, *y, *z);
        CHECK(compare_distance(*x, *z, 2.f, 4.f, *y) >= 100);
        // Y, the turned-not-rebuilt deck, against the C song: a re-snap, not
        // a transpose (see check_turn_is_not_transpose above).
        CHECK(check_turn_is_not_transpose(*y, *z, 4.f, *x) >= 100);

        // On BBD (in STEP, where it does quantize) and on SAMPLER (where it
        // does not, and is silent here for want of a record buffer, but
        // target_value still carries the pitch) the PITCH lane is not a note:
        // no offset, so X and Y agree although X still holds its +2.
        x->set_engine(PART_A, other);
        y->set_engine(PART_A, other);
        z->set_engine(PART_A, other);
        REQUIRE(render_until_engine(other, *x, *y, *z));
        render_s(0.5f, *x, *y, *z);
        CHECK(compare_distance(*x, *y, 0.f, 4.f, *z) >= 100);
        CHECK(x->song_offset_for_test(PART_A) == 2);

        // Back on a note engine the latched offset is live again, with no
        // rebuild in between (a rebuild would re-latch Y to +2 as well).
        x->set_engine(PART_A, ENGINE_SYNTH);
        y->set_engine(PART_A, ENGINE_SYNTH);
        z->set_engine(PART_A, ENGINE_SYNTH);
        REQUIRE(render_until_engine(ENGINE_SYNTH, *x, *y, *z));
        render_s(0.5f, *x, *y, *z);
        CHECK(compare_distance(*x, *z, 2.f, 4.f, *y) >= 100);
        CHECK(builds(*x) == b0);
        CHECK(builds(*y) == b0);
        CHECK(builds(*z) == b0);
        CHECK(y->song_offset_for_test(PART_A) == 0);
    }
}

TEST_CASE("song offset: init latches the current root") {
    auto inst = make_rig();
    inst->set_root(PART_A, 5);
    inst->init(48000.f);   // init() does not reset the quantizer's root
    CHECK(inst->song_offset_for_test(PART_A) == 5);
}

TEST_CASE("song offset: the pitch stays inside the span at the edge") {
    auto inst = make_rig();
    inst->set_root(PART_A, 6);
    inst->new_phrase(PART_A);
    REQUIRE(render_until_offset(*inst, 6, 30.f));
    inst->set_tune(PART_A, 1.f);
    int out = 0;
    for (int b = 0; b < 4 * kBlocksPerSec; ++b) {
        block(*inst);
        const float q   = inst->target_value(PART_A, LANE_PITCH);
        // The quantizer clamps its own input, so target_value alone could not
        // see a missing bound; the pre-quantize value is where it would show.
        // What keeps it inside is the reflection in pitch_pre_quant(), not a
        // clamp: the note that leaves the span is mirrored back in.
        const float pre = inst->part(PART_A).pitch_pre_quant();
        if (q < 0.f || q > 1.f || pre < 0.f || pre > 1.f) ++out;
    }
    CHECK(out == 0);
}
