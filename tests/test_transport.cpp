#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include "center/transport.h"
using namespace spky;

TEST_CASE("transport: beats advance at bpm/60 per second of control ticks") {
    Transport t;
    t.init(500.f);            // Center's control rate at 48 kHz
    t.set_bpm(120.f);         // 2 beats per second
    for (int i = 0; i < 500; ++i) t.tick();
    CHECK(t.beats() == doctest::Approx(2.0).epsilon(1e-6));
    CHECK(t.beat_phase() == doctest::Approx(0.f).epsilon(1e-4));
}

TEST_CASE("transport: clock_pulse snaps the phase to the nearest beat") {
    Transport t;
    t.init(500.f);
    t.set_bpm(120.f);
    for (int i = 0; i < 540; ++i) t.tick();   // 2.16 beats
    t.clock_pulse(1.f);
    CHECK(t.beats() == doctest::Approx(2.0));
    for (int i = 0; i < 210; ++i) t.tick();   // 2.84 beats
    t.clock_pulse(1.f);
    CHECK(t.beats() == doctest::Approx(3.0)); // rounds up too
}

TEST_CASE("transport: reset zeroes the downbeat") {
    Transport t;
    t.init(500.f);
    t.set_bpm(97.f);
    for (int i = 0; i < 1234; ++i) t.tick();
    t.reset();
    CHECK(t.beats() == doctest::Approx(0.0));
    CHECK(t.beat_phase() == doctest::Approx(0.f));
}

TEST_CASE("transport: set_bpm rejects non-positive and non-finite values") {
    // Guards the source that feeds nearest_division()/division_hz() (COUPLE's
    // grid gravity) and the transport's own beat_phase()/beats() readers: a
    // scenario-file `bpm: 0` (host/render/scenario.cpp forwards it
    // unvalidated) must not reach a divide and produce a non-finite grid.
    // The last good tempo is kept rather than clamped to an arbitrary floor
    // -- 0/negative/NaN/Inf are bad input, not a real tempo.
    Transport t;
    t.init(500.f);
    t.set_bpm(140.f);
    CHECK(t.bpm() == doctest::Approx(140.f));

    t.set_bpm(0.f);
    CHECK(t.bpm() == doctest::Approx(140.f));

    t.set_bpm(-10.f);
    CHECK(t.bpm() == doctest::Approx(140.f));

    t.set_bpm(std::numeric_limits<float>::quiet_NaN());
    CHECK(t.bpm() == doctest::Approx(140.f));

    t.set_bpm(std::numeric_limits<float>::infinity());
    CHECK(t.bpm() == doctest::Approx(140.f));

    // A subsequent genuinely valid tempo still applies normally.
    t.set_bpm(90.f);
    CHECK(t.bpm() == doctest::Approx(90.f));
}

TEST_CASE("transport: an external clock survives a paced transport") {
    spky::Transport t;
    t.init(500.f);                 // control rate
    t.set_bpm(120.f * 0.03125f);   // 120 BPM at PACE x1/32

    // One pulse per REAL beat: at 120 BPM that is every 0.5 s = 250 ticks.
    // Each pulse must advance the paced transport by `pace` beats, not snap
    // it back to the same integer.
    for (int pulse = 0; pulse < 8; ++pulse) {
        for (int i = 0; i < 250; ++i) t.tick();
        t.clock_pulse(0.03125f);
    }
    CHECK(t.beats() == doctest::Approx(8.0 * 0.03125).epsilon(0.02));

    // The PACE knob turns BETWEEN pulses, not exactly on one: let 800 ticks
    // (800 * 3.75/(60*500) = 0.1 paced beats, at the OLD tempo) pass first,
    // so _beats (0.35) sits ahead of the last pulse's anchor (0.25) at the
    // moment set_pace_anchor() runs. That gap is what makes the call
    // load-bearing: a no-op set_pace_anchor() would leave the STALE 0.25
    // anchor in place, and the next pulse's snap below would then land 0.1
    // beat short of the value asserted -- far past this check's ~0.026 beat
    // tolerance, so a no-op cannot pass it by accident.
    for (int i = 0; i < 800; ++i) t.tick();
    t.set_bpm(120.f * 1.32f);
    t.set_pace_anchor();            // what Instrument::set_pace calls
    const double before = t.beats();

    // A pace CHANGE mid-stream must not jump: the anchor is the previous
    // pulse (just re-anchored above), not absolute zero. Anchored at zero
    // this walks off by up to half a real beat on every pulse while the
    // knob moves -- straight into a hard servo whose authority is
    // kLockCap = 0.35.
    for (int i = 0; i < 250; ++i) t.tick();
    t.clock_pulse(1.32f);
    CHECK(t.beats() - before == doctest::Approx(1.32).epsilon(0.02));
}
