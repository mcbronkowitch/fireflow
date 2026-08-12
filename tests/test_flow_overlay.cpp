// tests/test_flow_overlay.cpp
//
// The base overlay (spec 2026-08-11 flow-patch-transfer §4). Two claims, and
// the second one is the whole design: an overlay reaches every kBaseRules
// parameter, and reaches NO story-owned parameter.
#include "doctest/doctest.h"
#include "flow/terrain.h"
#include "flow/taste.h"
#include "flow/flow.h"
#include "instrument.h"

using namespace spky::flow;

TEST_CASE("is_base_rule agrees with the kBaseRules table") {
    // Derived from the table on both sides on purpose -- but the SUBJECT is
    // the function and the EXPECTATION is the raw table, so a function that
    // stopped reading the table would fail here.
    bool in_table[P_COUNT] = {};
    for (int i = 0; i < kBaseRuleCount; ++i) in_table[kBaseRules[i].param] = true;
    for (int p = 0; p < P_COUNT; ++p) CHECK(is_base_rule(p) == in_table[p]);

    int n = 0;
    for (int p = 0; p < P_COUNT; ++p) if (is_base_rule(p)) ++n;
    CHECK(n == kBaseRuleCount);
    // Pins the two facts the plan's Background section states. If taste.h
    // legitimately grows a base rule, update BOTH numbers together and say so
    // in the commit -- do not delete the assertion.
    //
    // 38 -> 42 on 2026-08-12: P_TIDE, P_COLOR_A, P_COLOR_B and P_SUB_A moved
    // out of the story tables and into kBaseRules, so a hand-authored patch
    // carries its own texture-lane speed and its own chord size. The move also
    // deleted M_DENSITY's "thick" variant, which is what had left COLOR
    // unmapped on half of all terrains (tests/test_flow_chord_reach.cpp).
    CHECK(kBaseRuleCount == 42);
    CHECK(is_base_rule(P_COMP_B));
    CHECK_FALSE(is_base_rule(P_COMP_A));
}

TEST_CASE("an overlay reaches every base-rule parameter") {
    TerrainState st; st.master = 0x51A7E1u;
    const Terrain plain = generate(st, nullptr);

    BaseOverlay ov;
    // A value that differs from the drawn one for every row: take the opposite
    // end of each parameter's own range.
    for (int i = 0; i < kBaseRuleCount; ++i) {
        const int p = kBaseRules[i].param;
        if (p == P_ENGINE_A || p == P_ENGINE_B) {
            // kParams[P_ENGINE_*] is 0..5, and 0 is ENGINE_TEST_TONE while 5 is
            // ENGINE_BBD -- neither carries. Task 2 rejects a carrier-less pair
            // whole, so the range flip the other rows use would make this case
            // assert against a rejected overlay. Pick a carrier that differs
            // from the drawn value instead; the claim is unchanged.
            const int drawn = int(plain.base[p] + 0.5f);
            for (int k = 0; k < kCarrierEngineCount; ++k)
                if (kCarrierEngine[k] != drawn) { ov.v[p] = float(kCarrierEngine[k]); break; }
        } else {
            const float mid = 0.5f * (kParams[p].lo + kParams[p].hi);
            ov.v[p] = plain.base[p] < mid ? kParams[p].hi : kParams[p].lo;
        }
        ov.has[p] = true;
    }
    const Terrain over = generate(st, &ov);

    for (int i = 0; i < kBaseRuleCount; ++i) {
        const int p = kBaseRules[i].param;
        // P_ENGINE_A/B and the constrained rows may be moved again by
        // apply_constraints(); assert the overlay MOVED the value rather than
        // that it landed exactly, so this gate does not fight §4.2's last word.
        CHECK(over.base[p] != doctest::Approx(plain.base[p]));
    }
}

TEST_CASE("an overlay reaches no story-owned parameter") {
    TerrainState st; st.master = 0x7A11E5u;
    const Terrain plain = generate(st, nullptr);

    BaseOverlay ov;
    for (int p = 0; p < P_COUNT; ++p) {
        if (is_base_rule(p)) continue;
        ov.v[p]   = kParams[p].hi;   // as far from any drawn floor as the range allows
        ov.has[p] = true;
    }
    const Terrain over = generate(st, &ov);

    for (int p = 0; p < P_COUNT; ++p)
        CHECK(over.base[p] == doctest::Approx(plain.base[p]));
}

TEST_CASE("a null overlay leaves generate unchanged") {
    TerrainState st; st.master = 0xBEEF01u;
    const Terrain a = generate(st);
    const Terrain b = generate(st, nullptr);
    for (int p = 0; p < P_COUNT; ++p) CHECK(a.base[p] == doctest::Approx(b.base[p]));
    CHECK(a.arch == b.arch);
    CHECK(a.a_carries == b.a_carries);
}

TEST_CASE("an out-of-range overlay value is clamped on the way in") {
    TerrainState st; st.master = 0xC1A11Fu & 0xFFFFFFu;   // any master
    BaseOverlay ov;
    ov.v[P_TUNE_A] = 40.f;      // P_TUNE_A is 0..1
    ov.has[P_TUNE_A] = true;
    const Terrain t = generate(st, &ov);
    CHECK(t.base[P_TUNE_A] <= kParams[P_TUNE_A].hi);
    CHECK(t.base[P_TUNE_A] >= kParams[P_TUNE_A].lo);
}

using spky::flow::is_carrier_engine;

// Find a master whose drawn roles put the carrier on the deck we do NOT want,
// so the test is about the recomputation and not about a lucky coin.
static uint32_t master_with_a_carrying(bool want_a) {
    for (uint32_t m = 1; m < 4000u; ++m) {
        const Terrain t = generate(TerrainState{ m, {} });
        if (t.a_carries == want_a) return m;
    }
    FAIL("no master found -- the roles coin cannot be this skewed");
    return 1u;
}

TEST_CASE("a_carries follows the overlaid engines") {
    // Deck A gets a texture-only engine, deck B a carrier engine. Whatever the
    // coin said, B must carry.
    TerrainState st; st.master = master_with_a_carrying(true);
    BaseOverlay ov;
    ov.v[P_ENGINE_A] = float(ENGINE_SAMPLER); ov.has[P_ENGINE_A] = true;
    ov.v[P_ENGINE_B] = float(ENGINE_SYNTH);   ov.has[P_ENGINE_B] = true;

    const Terrain t = generate(st, &ov);
    CHECK(t.a_carries == false);
    CHECK(int(t.base[P_ENGINE_A] + 0.5f) == ENGINE_SAMPLER);
    CHECK(int(t.base[P_ENGINE_B] + 0.5f) == ENGINE_SYNTH);
}

// Same search as master_with_a_carrying, but also requires the drawn
// TEXTURE engine to be carrier-eligible, so overlaying only P_ENGINE_A with
// a texture-only engine still has somewhere for the carrier to land -- the
// case below never touches P_ENGINE_B at all.
static uint32_t master_with_a_carrying_and_texture_carrier_eligible() {
    for (uint32_t m = 1; m < 4000u; ++m) {
        const Terrain t = generate(TerrainState{ m, {} });
        if (t.a_carries && is_carrier_engine(int(t.base[P_ENGINE_B] + 0.5f))) return m;
    }
    FAIL("no master found -- deck B's drawn engine is never carrier-eligible while A carries");
    return 1u;
}

TEST_CASE("a single-slot overlay still recomputes the carrier") {
    // Finding 1 (Task 2 review): gating the recompute on "both engine slots
    // set" left this path unguarded -- decode_base (Tasks 7-8) can produce a
    // single-slot overlay from decoded text. Only P_ENGINE_A is overlaid,
    // to a texture-only engine, on a master where deck A started as the
    // carrier; deck B's UNTOUCHED drawn engine happens to be carrier-eligible
    // too (search above), so the carrier must move to B and the overlay must
    // NOT be rejected.
    TerrainState st; st.master = master_with_a_carrying_and_texture_carrier_eligible();
    BaseOverlay ov;
    ov.v[P_ENGINE_A] = float(ENGINE_SAMPLER); ov.has[P_ENGINE_A] = true;

    const Terrain t = generate(st, &ov);
    CHECK(t.a_carries == false);
    CHECK(int(t.base[P_ENGINE_A] + 0.5f) == ENGINE_SAMPLER);   // overlay applied, not rejected
}

TEST_CASE("two carrier engines keep the drawn coin") {
    for (bool want : { true, false }) {
        TerrainState st; st.master = master_with_a_carrying(want);
        BaseOverlay ov;
        ov.v[P_ENGINE_A] = float(ENGINE_SYNTH); ov.has[P_ENGINE_A] = true;
        ov.v[P_ENGINE_B] = float(ENGINE_WAVE);  ov.has[P_ENGINE_B] = true;
        CHECK(generate(st, &ov).a_carries == want);
    }
}

TEST_CASE("an overlay with no carrier is rejected whole") {
    TerrainState st; st.master = 0x10AD5u;
    const Terrain plain = generate(st);

    BaseOverlay ov;
    ov.v[P_ENGINE_A] = float(ENGINE_SAMPLER); ov.has[P_ENGINE_A] = true;
    ov.v[P_ENGINE_B] = float(ENGINE_BBD);     ov.has[P_ENGINE_B] = true;
    ov.v[P_TUNE_A]   = 0.9f;                  ov.has[P_TUNE_A]   = true;

    const Terrain t = generate(st, &ov);
    // WHOLE, not just the engines: a terrain with no carrier has no defined
    // role structure, so half-applying it would be worse than not applying it.
    CHECK(t.base[P_TUNE_A] == doctest::Approx(plain.base[P_TUNE_A]));
    CHECK(int(t.base[P_ENGINE_A] + 0.5f) == int(plain.base[P_ENGINE_A] + 0.5f));
    CHECK(t.a_carries == plain.a_carries);
    CHECK_FALSE(is_carrier_engine(ENGINE_SAMPLER));   // the premise, pinned
    CHECK_FALSE(is_carrier_engine(ENGINE_BBD));
}

// A minimal harness: Flow needs an Instrument to push into, and the tests here
// only read Flow's own view of the terrain, never the audio.
struct FlowFixture {
    spky::Instrument inst;
    Flow             flow;
    FlowFixture() { inst.init(48000.f); flow.init(&inst, 100.f); }
};

static BaseOverlay tune_overlay(float tune_a) {
    BaseOverlay ov;
    ov.v[P_TUNE_A] = tune_a; ov.has[P_TUNE_A] = true;
    return ov;
}

TEST_CASE("wake applies the overlay") {
    FlowFixture f;
    const BaseOverlay ov = tune_overlay(0.87f);
    TerrainState st; st.master = 0x515Eu;
    f.flow.wake(st, &ov);
    CHECK(f.flow.terrain_for_test().base[P_TUNE_A] == doctest::Approx(0.87f));
}

TEST_CASE("a hold-reroll keeps the overlay") {
    FlowFixture f;
    const BaseOverlay ov = tune_overlay(0.87f);
    TerrainState st; st.master = 0x515Eu;
    f.flow.wake(st, &ov);
    REQUIRE(f.flow.new_partial(0x3F));
    CHECK(f.flow.terrain_for_test().base[P_TUNE_A] == doctest::Approx(0.87f));
}

TEST_CASE("undo restores the overlay that belongs to the terrain it restores") {
    FlowFixture f;
    const BaseOverlay a = tune_overlay(0.10f);
    const BaseOverlay b = tune_overlay(0.90f);

    TerrainState sa; sa.master = 0xAAA1u;
    TerrainState sb; sb.master = 0xBBB2u;
    f.flow.wake(sa, &a);
    REQUIRE(f.flow.new_full());            // accepted press: arms the undo slot
    f.flow.wake(sb, &b);
    f.flow.wake(sa, &a);
    REQUIRE(f.flow.new_full());
    REQUIRE(f.flow.undo());

    // The failure this catches: the seed goes back and the overlay does not,
    // so the restored terrain is one place's base under another's stories.
    CHECK(f.flow.terrain_for_test().base[P_TUNE_A] == doctest::Approx(0.10f));
}

TEST_CASE("waking without an overlay clears the previous one") {
    FlowFixture f;
    const BaseOverlay ov = tune_overlay(0.87f);
    TerrainState st; st.master = 0x515Eu;
    f.flow.wake(st, &ov);
    const float overlaid = f.flow.terrain_for_test().base[P_TUNE_A];

    f.flow.wake(st, nullptr);
    // A pad with no overlay must play the drawn terrain, not the last pad's
    // base. Same rule Glow.cpp already applies to a place with no code.
    CHECK(f.flow.terrain_for_test().base[P_TUNE_A] != doctest::Approx(overlaid));
}

TEST_CASE("undo then redo keeps each state paired with its own overlay") {
    // A patch-load shape: wake onto the live place (overlay A), then
    // restore_undo() seeds the slot from a DIFFERENT place with a different
    // overlay (B) -- exactly what Tasks 7/8's restore order does. The first
    // undo() plays the slot (B); the second undo() is a redo, and must play
    // the place we left under ITS OWN overlay (A), not under B.
    FlowFixture f;
    const BaseOverlay a = tune_overlay(0.15f);
    const BaseOverlay b = tune_overlay(0.85f);

    TerrainState s1; s1.master = 0xC0DE1u;
    TerrainState s2; s2.master = 0xC0DE2u;
    f.flow.wake(s1, &a);
    f.flow.restore_undo(s2, true, &b);

    REQUIRE(f.flow.undo());
    CHECK(f.flow.terrain_for_test().base[P_TUNE_A] == doctest::Approx(0.85f));

    REQUIRE(f.flow.undo());
    // The failure this catches: begin_blend() re-derives the fresh slot from
    // whatever _overlay undo() just set for RENDERING `back` (B) instead of
    // the overlay of the state actually being left (A), so a redo would play
    // s1 under B -- one place's base under another's stories, on the second
    // press instead of the first.
    CHECK(f.flow.terrain_for_test().base[P_TUNE_A] == doctest::Approx(0.15f));
}

// What this actually gates, post-refactor: that generate() writes the wish
// filters' results into the terrain and that no later stage overwrites them.
// It is NOT a check that two implementations agree -- there is only one
// implementation now (generate() calls roles_of/tonality_of/mode_of
// directly), so a bug inside one of them moves both sides of every CHECK
// below equally. See "roles_of puts a carrier on the deck a_carries names"
// for a property that lives on one side only.
//
// RED recipe (it is not the obvious one): mutate the CALL SITE where
// generate() consumes roles_of's output -- e.g. swap the two engine ids as
// they are written into the terrain in terrain.cpp -- and this test reddens
// immediately. Mutating roles_of ITSELF cannot redden it, for the reason
// above. That is the whole reason the invariant assertion in the other test
// exists.
TEST_CASE("the wish filters agree with generate, over many masters") {
    for (uint32_t m = 1; m < 600u; ++m) {
        const Terrain t = generate(TerrainState{ m, {} });

        int ea = -1, eb = -1; bool ac = false;
        roles_of(m, ea, eb, ac);
        int scale = -1, root = -1;
        tonality_of(m, scale, root);

        CAPTURE(m);
        CHECK(ea == int(t.base[P_ENGINE_A] + 0.5f));
        CHECK(eb == int(t.base[P_ENGINE_B] + 0.5f));
        CHECK(ac == t.a_carries);
        CHECK(scale == int(t.base[P_SCALE] + 0.5f));
        CHECK(root  == int(t.base[P_ROOT]  + 0.5f));
        CHECK(mode_of(m) == int(t.base[P_MODE] + 0.5f));
    }
}

TEST_CASE("the wish filters ignore the reroll counters") {
    // The point of the filters: everything they report is drawn at counter 0,
    // so a rerolled terrain still answers the same wish. If this ever fails,
    // the filters are reading a stage that moved.
    TerrainState st; st.master = 0x2C0FFEEu & 0xFFFFFFu;
    for (int m = 0; m < MACRO_COUNT; ++m) st.reroll[m] = uint16_t(7 * m + 3);
    const Terrain t = generate(st);

    int ea = -1, eb = -1; bool ac = false;
    roles_of(st.master, ea, eb, ac);
    CHECK(ea == int(t.base[P_ENGINE_A] + 0.5f));
    CHECK(eb == int(t.base[P_ENGINE_B] + 0.5f));
    CHECK(mode_of(st.master) == int(t.base[P_MODE] + 0.5f));
}

TEST_CASE("roles_of puts a carrier on the deck a_carries names") {
    // Independent of generate(): a property of the roles draw itself. This
    // is what the comparison case above cannot see -- generate() calls
    // roles_of, so a bug inside it moves both sides of that comparison
    // equally. Here there is only one side.
    for (uint32_t m = 1; m < 600u; ++m) {
        int ea = -1, eb = -1; bool ac = false;
        roles_of(m, ea, eb, ac);
        CAPTURE(m);
        CHECK(is_carrier_engine(ac ? ea : eb));
    }
}
