#include <doctest/doctest.h>
#include "parts/test_tone_engine.h"
#include "part_engine_contract.h"

using namespace spky;

// A SECOND, independent caller for tests/part_engine_contract.h, and the
// reason it exists: a "universal" contract with exactly one implementer is
// indistinguishable from a description of that implementer. TestToneEngine is
// as far from BbdEngine as this repo's engines get -- no input, no buffers, no
// feedback, no freeze, and it overrides none of the five no-op setters the
// third block calls -- so a contract both of them satisfy is a contract about
// IPartEngine rather than about the BBD.
//
// It also pins the vacuous branch: TestToneEngine::consumes_input() is the
// base-class default (false), so the process_in/consumes_input block is
// skipped here rather than failing. That is correct -- there is no pairing to
// check on an engine that has no input -- and this is the case that proves the
// skip works instead of leaving it to be discovered by the next voiceless
// engine.
TEST_CASE("part engine contract: the test tone satisfies it too") {
    check_part_engine_contract<TestToneEngine>([](TestToneEngine& e) {
        e.init(48000.f);
    });
}
