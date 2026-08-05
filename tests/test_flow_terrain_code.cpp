// tests/test_flow_terrain_code.cpp
#include "doctest/doctest.h"
#include "flow/terrain_code.h"
#include "flow/terrain.h"
#include "flow/taste.h"
using namespace spky::flow;

TEST_CASE("flow code: roundtrip") {
    TerrainState a; a.master = 0xDEADBEEF;
    a.reroll[M_BRIGHT] = 3; a.reroll[M_SPACE] = 1;
    char buf[48]; REQUIRE(encode_code(a, buf, sizeof buf) > 0);
    TerrainState b; REQUIRE(decode_code(buf, b));
    CHECK(b.master == a.master);
    for (int m = 0; m < MACRO_COUNT; ++m) CHECK(b.reroll[m] == a.reroll[m]);
    TerrainState c; CHECK(!decode_code("garbage", c));
}

TEST_CASE("flow code: decode rejects truncated and wrong-prefix codes") {
    TerrainState out;
    // Truncated: only 4 hex digits of counters instead of 12.
    CHECK(!decode_code("F1-DEADBEEF-0001", out));
    // Wrong format version prefix (right otherwise-correct length).
    CHECK(!decode_code("F2-DEADBEEF-000000000000", out));
}

TEST_CASE("flow code: decode rejects non-hex digits at the right length") {
    // The spec (§5) invites a user to paste a terrain code into the VCV
    // context menu, so a correct-LENGTH, correct-PREFIX string full of the
    // wrong characters is a real input, not a hypothetical one -- and it is
    // the only thing decode_code's two is_hex loops exist for. Without these
    // two lines both loops could be deleted with the whole suite still green,
    // and a pasted typo would decode to a silently different terrain
    // (hex_val('G') == 16, which shifts straight into the master).
    TerrainState out;
    CHECK(!decode_code("F1-DEADBEEG-000000000000", out));   // master field
    CHECK(!decode_code("F1-DEADBEEF-00000000000G", out));   // counter field
    CHECK(!decode_code("F1-DEADBEEF-0000000000 0", out));   // and whitespace
    // ...and the loops are not simply rejecting everything: lowercase hex is
    // legal input and must still decode (is_hex/hex_val both accept a-f).
    TerrainState lower;
    REQUIRE(decode_code("F1-deadbeef-0000000000ff", lower));
    CHECK(lower.master == 0xDEADBEEFu);
    CHECK(lower.reroll[MACRO_COUNT - 1] == 0xFFu);
}

TEST_CASE("flow distance: NEW lands elsewhere, deterministically (spec 7.4)") {
    TerrainState cur; cur.master = 0xC0FFEE;
    Terrain here = generate(cur);
    Rng seq1; seq1.seed(42);
    Rng seq2; seq2.seed(42);
    TerrainState n1 = draw_new(cur, seq1), n2 = draw_new(cur, seq2);
    CHECK(n1.master == n2.master);                       // deterministic chain
    CHECK(distance(here, generate(n1)) >= kDistanceMin); // clears threshold
    for (int m = 0; m < MACRO_COUNT; ++m) CHECK(n1.reroll[m] == 0);
    CHECK(n1.master != cur.master);                       // NEW never returns cur
}
