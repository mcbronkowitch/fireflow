// The shared CLOCK/RESET law (control/clock_law.hpp). Spec:
// docs/superpowers/specs/2026-10-19-clock-and-reset-jacks-design.md §2.1,
// §2.2, §6 and §8.1. Every case runs at the two call sizes the hosts use:
// 1 sample (VCV) and one 96-sample block (the firmware at 48 kHz).
#include <doctest/doctest.h>
#include "control/clock_law.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <vector>

namespace {

constexpr float    kSr    = 48000.f;
constexpr uint32_t kBlock = 96;     // the firmware's audio block
constexpr int      kFull  = control::kClockWindowPeriods;

double period_of(double bpm) { return 60.0 * kSr / bpm; }

// The quantization bound every tempo tolerance below comes from.
//
// An edge is stamped at the end of the call that sees it, so each stamp is
// late by a value in [0, q) for a call size of q samples. The law's period is
// the mean of k consecutive intervals; their sum telescopes to (last stamp -
// first stamp), whose error is the difference of two stamp errors and lies
// strictly inside (-q, q). So the summed span is inside (kP - q, kP + q), and
//     |bpm' - bpm| < bpm * q / (kP - q).
// k = 1 is the single-period measurement (±0.43 % at 130 BPM in blocks, spec
// §3); k = kClockWindowPeriods is the full window.
double tol(double bpm, uint32_t q, int k) {
    const double P = period_of(bpm);
    return bpm * double(q) / (double(k) * P - double(q));
}

// One call the rig made that did something: an input edge, any output flag,
// or a change of measured_bpm. Calls that did none of these are not kept, so a
// change of measured_bpm can never hide between logged calls.
struct Event {
    control::ClockOut out;
    uint64_t end;          // sample time at the end of the call (the edge stamp)
    bool clk, rst;         // the inputs of that call
    bool knob_moved;       // tempo_knob differs from the previous call's
    float prev_bpm;        // measured_bpm of the previous call
};

// Drives the law in calls of q samples. An edge at time t is fed to the call
// whose span (end - q, end] holds t.
struct Rig {
    control::ClockLaw law;
    uint32_t q;
    uint64_t now = 0;
    float knob = 0.5f, phase = 0.f;
    float last_knob = 0.5f, last_bpm = 0.f;
    std::vector<Event> log;

    explicit Rig(uint32_t q_) : q(q_) { law.init(kSr); }

    control::ClockOut tick(bool clk = false, bool rst = false) {
        now += q;
        const control::ClockOut o = law.step(q, clk, rst, knob, phase);
        const bool moved = knob != last_knob;
        if (clk || rst || moved || o.pulse || o.restart || o.lost || o.clock_lamp ||
            o.reset_lamp || o.measured_bpm != last_bpm)
            log.push_back({o, now, clk, rst, moved, last_bpm});
        last_knob = knob;
        last_bpm = o.measured_bpm;
        return o;
    }
    // Plain calls up to the call that holds time t; that call gets the edges.
    control::ClockOut edge_at(double t, bool clk, bool rst) {
        REQUIRE(t > double(now));
        while (double(now + q) < t) tick();
        return tick(clk, rst);
    }
    control::ClockOut clock_at(double t) { return edge_at(t, true, false); }
    control::ClockOut reset_at(double t) { return edge_at(t, false, true); }
    void idle_until(double t) { while (double(now) < t) tick(); }

    // A steady train of n edges from `first`; the outputs of the edge calls.
    std::vector<control::ClockOut> train(double first, double bpm, int n) {
        std::vector<control::ClockOut> v;
        for (int i = 0; i < n; ++i) v.push_back(clock_at(first + i * period_of(bpm)));
        return v;
    }
    size_t lost_count() const {
        return (size_t)std::count_if(log.begin(), log.end(), [](const Event& e) { return e.out.lost; });
    }
};

// The measured_bpm of every pulse, in order.
std::vector<float> pulse_bpms(const std::vector<control::ClockOut>& outs) {
    std::vector<float> v;
    for (const auto& o : outs) if (o.pulse) v.push_back(o.measured_bpm);
    return v;
}

// 1-based index of the first pulse from which every later pulse is within
// tol of target; 0 when even the last one is not.
int settle_index(const std::vector<float>& bpms, double target, double tolerance) {
    int idx = 0;
    for (int i = (int)bpms.size() - 1; i >= 0; --i) {
        if (std::fabs(bpms[i] - target) >= tolerance) break;
        idx = i + 1;
    }
    return idx;
}

} // namespace

// ---------------------------------------------------------------- tempo ----

TEST_CASE("clock law: steady tempo") {
    for (double bpm : {60.0, 120.0, 240.0, 130.0}) {
        for (uint32_t q : {1u, kBlock}) {
            CAPTURE(bpm); CAPTURE(q);
            Rig rig(q);
            const double P = period_of(bpm);
            // Start off the block grid so the stamps carry a real phase.
            rig.train(0.37 * P, bpm, 24);
            rig.idle_until(double(rig.now) + 0.5 * P);
            // The k-th pulse has measured k intervals, up to the full window.
            int k = 0;
            for (const auto& e : rig.log) {
                if (e.out.pulse) ++k;
                if (k == 0) continue;
                CHECK(std::fabs(e.out.measured_bpm - bpm) < tol(bpm, q, std::min(k, kFull)));
            }
            CHECK(k == 23);                 // the first edge is no pulse
            CHECK(rig.law.running());
            CHECK(rig.lost_count() == 0);
        }
    }
}

TEST_CASE("clock law: the tempo changes only at pulses") {
    Rig rig(kBlock);
    double t = 1234.0;
    for (int i = 0; i < 12; ++i, t += period_of(130.0)) rig.clock_at(t);
    for (int i = 0; i < 10; ++i, t += period_of(90.0))  rig.clock_at(t);
    t += 0.5 * period_of(90.0);                          // a 1.5-period pause
    for (int i = 0; i < 6; ++i, t += period_of(90.0))   rig.clock_at(t);
    rig.idle_until(t + 3.0 * period_of(90.0));           // the clock is lost
    rig.knob = 0.8f;                                     // the knob takes over
    rig.idle_until(double(rig.now) + 4800.0);
    t = double(rig.now) + 100.0;
    for (int i = 0; i < 5; ++i, t += period_of(120.0))  rig.clock_at(t);

    int pulse_changes = 0, takeovers = 0;
    for (const auto& e : rig.log) {
        if (e.out.measured_bpm == e.prev_bpm) continue;
        const bool takeover = e.knob_moved && e.out.measured_bpm == 0.f;
        CHECK((e.out.pulse || takeover));
        if (e.out.pulse) ++pulse_changes;
        if (takeover) ++takeovers;
    }
    // Not vacuous: the tempo did move at pulses, and the knob took over once.
    REQUIRE(pulse_changes >= 10);
    CHECK(takeovers == 1);
    CHECK(rig.lost_count() == 1);
}

TEST_CASE("clock law: per-pulse step at 130 BPM is bounded") {
    // 130 BPM is 230.77 blocks, so a full window of kFull periods spans either
    // floor or ceil of kFull * 230.77 blocks: the measurement can step by one
    // block's worth of span per pulse. With the span bound above, the step is
    // below the full-window tolerance  bound_full = bpm*q/(kFull*P - q).
    // A single-period measurement (N = 2) steps by up to bpm*q/(P - q)
    // = 0.566 BPM (130.43 / 129.87, spec §3). The second CHECK demands at least
    // half of that removed -- the reason N > 2.
    const double bpm = 130.0;
    Rig rig(kBlock);
    const auto bpms = pulse_bpms(rig.train(777.0, bpm, 48));
    REQUIRE(bpms.size() == 47);
    double max_step = 0.0;
    for (size_t i = kFull; i < bpms.size(); ++i)  // both ends see a full window
        max_step = std::max(max_step, (double)std::fabs(bpms[i] - bpms[i - 1]));
    const double bound_full = tol(bpm, kBlock, kFull);
    const double single     = tol(bpm, kBlock, 1);
    CAPTURE(max_step); CAPTURE(bound_full); CAPTURE(single);
    CHECK(max_step < bound_full);
    CHECK(max_step < 0.5 * single);
}

TEST_CASE("clock law: swing and tempo steps settle") {
    for (uint32_t q : {1u, kBlock}) {
        CAPTURE(q);
        SUBCASE("55/45 swing at 120 BPM") {
            // The window holds an even number of periods, so every full
            // window holds whole long+short pairs and the mean is the beat:
            // only the span bound remains.
            const double P = period_of(120.0);
            Rig rig(q);
            double t = 0.41 * P;
            std::vector<control::ClockOut> outs;
            for (int i = 0; i < 32; ++i) {
                outs.push_back(rig.clock_at(t));
                t += (i % 2 == 0) ? 1.1 * P : 0.9 * P;
            }
            const auto bpms = pulse_bpms(outs);
            REQUIRE(bpms.size() == 31);
            for (size_t i = kFull - 1; i < bpms.size(); ++i) {   // full windows
                CAPTURE(i);
                CHECK(std::fabs(bpms[i] - 120.0) < tol(120.0, q, kFull));
            }
            CHECK(rig.lost_count() == 0);
        }
        SUBCASE("120 -> 90 BPM settles within kFull pulses") {
            // 90/120 lengthens the period by 1.33x, inside kClockPauseRatio:
            // the window ramps, and after kFull pulses at the new period it
            // holds only new intervals. Criterion: the single-period bound.
            Rig rig(q);
            double t = 555.0;
            for (int i = 0; i < 16; ++i, t += period_of(120.0)) rig.clock_at(t);
            t += period_of(90.0) - period_of(120.0);
            std::vector<control::ClockOut> after;
            for (int i = 0; i < 16; ++i, t += period_of(90.0)) after.push_back(rig.clock_at(t));
            const auto bpms = pulse_bpms(after);
            REQUIRE(bpms.size() == 16);
            const int s = settle_index(bpms, 90.0, tol(90.0, q, 1));
            CAPTURE(s);
            CHECK(s >= 1);
            CHECK(s <= kFull);
            CHECK(rig.lost_count() == 0);
        }
        SUBCASE("120 -> 75 BPM settles within 2 pulses") {
            // 1.6x is beyond kClockPauseRatio: the first long period is held
            // aside like a pause, the second confirms the new tempo.
            Rig rig(q);
            double t = 555.0;
            for (int i = 0; i < 16; ++i, t += period_of(120.0)) rig.clock_at(t);
            t += period_of(75.0) - period_of(120.0);
            std::vector<control::ClockOut> after;
            for (int i = 0; i < 16; ++i, t += period_of(75.0)) after.push_back(rig.clock_at(t));
            const auto bpms = pulse_bpms(after);
            REQUIRE(bpms.size() == 16);
            const int s = settle_index(bpms, 75.0, tol(75.0, q, 1));
            CAPTURE(s);
            CHECK(s >= 1);
            CHECK(s <= 2);
            CHECK(rig.lost_count() == 0);
        }
    }
}

// -------------------------------------------------------- running state ----

TEST_CASE("clock law: out-of-range pulses are no clock") {
    for (double bpm : {480.0, 15.0}) {
        for (uint32_t q : {1u, kBlock}) {
            CAPTURE(bpm); CAPTURE(q);
            Rig rig(q);
            const double P = period_of(bpm);
            double t = 0.3 * P;
            for (int i = 0; i < 6; ++i, t += P) {
                const auto o = rig.clock_at(t);
                CHECK_FALSE(o.pulse);
                CHECK_FALSE(rig.law.running());
                if (i == 3) {
                    // A reset between two of them restarts at once.
                    const auto r = rig.reset_at(t + 0.5 * P);
                    CHECK(r.restart);
                    CHECK(r.restart_age == 0u);
                }
            }
            CHECK(rig.law.running() == false);
            for (const auto& e : rig.log) {
                CHECK_FALSE(e.out.pulse);
                CHECK(e.out.measured_bpm == 0.f);
            }
        }
    }
}

TEST_CASE("clock law: two valid edges before running") {
    // Edge A: no pulse, not running, and a reset after it restarts at once
    // (no arming). Edge B: pulse, running, and nothing was armed. A reset
    // after B arms, and edge C restarts.
    auto check_from = [](Rig& rig, double t0, double bpm) {
        const double P = period_of(bpm);
        const float held = rig.last_bpm;
        const auto a = rig.clock_at(t0);
        CHECK_FALSE(a.pulse);
        CHECK_FALSE(rig.law.running());
        CHECK(a.measured_bpm == held);                 // a held tempo stays
        const auto ra = rig.reset_at(t0 + 0.05 * kSr);
        CHECK(ra.restart);
        CHECK(ra.restart_age == 0u);
        const auto b = rig.clock_at(t0 + P);
        CHECK(b.pulse);
        CHECK(rig.law.running());
        CHECK_FALSE(b.restart);
        CHECK(std::fabs(b.measured_bpm - bpm) < tol(bpm, rig.q, 1));
        const auto rb = rig.reset_at(t0 + P + 0.05 * kSr);
        CHECK_FALSE(rb.restart);
        const auto c = rig.clock_at(t0 + 2 * P);
        CHECK(c.pulse);
        CHECK(c.restart);
        CHECK(c.restart_age == 0u);
    };
    for (uint32_t q : {1u, kBlock}) {
        CAPTURE(q);
        SUBCASE("after boot") {
            Rig rig(q);
            check_from(rig, 1000.0, 120.0);
        }
        SUBCASE("after discontinuity()") {
            Rig rig(q);
            const double P = period_of(120.0);
            rig.train(1000.0, 120.0, 10);
            rig.law.discontinuity();
            // The train goes on at the same period: the interval across the
            // discontinuity is still not a period.
            check_from(rig, 1000.0 + 10 * P, 120.0);
            CHECK(rig.last_bpm > 0.f);
        }
        SUBCASE("after a loss") {
            Rig rig(q);
            const double P = period_of(120.0);
            rig.train(1000.0, 120.0, 10);
            rig.idle_until(1000.0 + 9 * P + 2.5 * P);
            REQUIRE(rig.lost_count() == 1);
            REQUIRE_FALSE(rig.law.running());
            check_from(rig, double(rig.now) + 0.4 * P, 100.0);
        }
    }
}

TEST_CASE("clock law: a short pause keeps the clock") {
    // The pause period is held aside: the skipped interval splits the window
    // into two telescoping chains, so the span error is below 2q, and
    // |bpm' - bpm| < bpm*2q/(kFull*P - 2q).
    for (uint32_t q : {1u, kBlock}) {
        CAPTURE(q);
        const double bpm = 120.0, P = period_of(bpm);
        Rig rig(q);
        double t = 0.29 * P;
        for (int i = 0; i < 16; ++i, t += P) rig.clock_at(t);
        t += 0.5 * P;                                  // 1.5 periods, under 2x
        const auto first = rig.clock_at(t);
        CHECK(first.pulse);                            // it still aligns the beat
        t += P;
        std::vector<control::ClockOut> after{first};
        for (int i = 0; i < 12; ++i, t += P) after.push_back(rig.clock_at(t));
        const double tol2 = bpm * 2.0 * q / (kFull * P - 2.0 * q);
        for (const auto& o : after) {
            CHECK(o.pulse);
            CHECK(std::fabs(o.measured_bpm - bpm) < tol2);
        }
        CHECK(rig.lost_count() == 0);
        CHECK(rig.law.running());
    }
}

// ---------------------------------------------------------------- reset ----

TEST_CASE("clock law: reset before a pulse restarts on that pulse") {
    for (uint32_t q : {1u, kBlock}) {
        CAPTURE(q);
        const double P = period_of(120.0);
        Rig rig(q);
        rig.train(500.0, 120.0, 10);
        const double next = 500.0 + 10 * P;
        const auto r = rig.reset_at(next - 0.003 * kSr);   // 3 ms early
        CHECK_FALSE(r.restart);
        CHECK(r.reset_lamp);
        const auto p = rig.clock_at(next);
        CHECK(p.pulse);
        CHECK(p.restart);
        CHECK(p.restart_age == 0u);
        CHECK_FALSE(rig.clock_at(next + P).restart);       // fired once
    }
}

TEST_CASE("clock law: reset and pulse in one call") {
    for (uint32_t q : {1u, kBlock}) {
        CAPTURE(q);
        const double P = period_of(120.0);
        Rig rig(q);
        rig.train(500.0, 120.0, 10);
        const auto p = rig.edge_at(500.0 + 10 * P, true, true);
        CHECK(p.pulse);
        CHECK(p.restart);
        CHECK(p.restart_age == 0u);
        CHECK_FALSE(rig.clock_at(500.0 + 11 * P).restart);
    }
}

TEST_CASE("clock law: reset 5 ms after a pulse is back-dated") {
    for (uint32_t q : {1u, kBlock}) {
        CAPTURE(q);
        const double P = period_of(120.0);
        Rig rig(q);
        rig.train(500.0, 120.0, 10);
        const double last = 500.0 + 9 * P;
        // 5 ms = 240 samples. Both stamps are late by [0, q), so the measured
        // age is 240 within one call size.
        const auto r = rig.reset_at(last + 0.005 * kSr);
        CHECK(r.restart);
        CAPTURE(r.restart_age);
        CHECK(std::abs((int)r.restart_age - 240) <= (int)q);
        // The window took the reset: nothing is armed for the next pulse.
        CHECK_FALSE(rig.clock_at(last + P).restart);
        // 15 ms (720 samples) is outside the 10 ms window even with the stamp
        // error (720 - q > 480): it arms and waits for the next pulse.
        const auto r15 = rig.reset_at(last + P + 0.015 * kSr);
        CHECK_FALSE(r15.restart);
        const auto p = rig.clock_at(last + 2 * P);
        CHECK(p.restart);
        CHECK(p.restart_age == 0u);
    }
}

TEST_CASE("clock law: reset without a clock restarts at once") {
    for (uint32_t q : {1u, kBlock}) {
        CAPTURE(q);
        Rig rig(q);
        rig.idle_until(4800.0);
        const auto r = rig.reset_at(9000.0);
        CHECK(r.restart);
        CHECK(r.restart_age == 0u);
        // And after a loss.
        const double P = period_of(120.0);
        rig.train(20000.0, 120.0, 6);
        rig.idle_until(20000.0 + 5 * P + 2.5 * P);
        REQUIRE(rig.lost_count() == 1);
        const auto r2 = rig.reset_at(double(rig.now) + 1000.0);
        CHECK(r2.restart);
        CHECK(r2.restart_age == 0u);
    }
}

// --------------------------------------------------- loss and takeover ----

TEST_CASE("clock law: loss holds the tempo") {
    for (uint32_t q : {1u, kBlock}) {
        CAPTURE(q);
        const double P = period_of(120.0);
        Rig rig(q);
        const auto outs = rig.train(321.0, 120.0, 12);
        const float m = outs.back().measured_bpm;
        REQUIRE(std::fabs(m - 120.0) < tol(120.0, q, kFull));
        const uint64_t last_stamp = rig.now;
        rig.idle_until(double(last_stamp) + 4 * P);
        REQUIRE(rig.lost_count() == 1);
        const auto it = std::find_if(rig.log.begin(), rig.log.end(),
                                     [](const Event& e) { return e.out.lost; });
        // It fires on the first call past 2 periods since the last edge.
        CHECK(double(it->end - last_stamp) > 2 * P);
        CHECK(double(it->end - q - last_stamp) <= 2 * P);
        CHECK(it->out.measured_bpm == m);
        CHECK_FALSE(rig.law.running());
        CHECK(rig.last_bpm == m);
    }
}

TEST_CASE("clock law: knob takeover") {
    const double P = period_of(120.0);
    Rig rig(kBlock);
    rig.train(400.0, 120.0, 6);
    rig.knob = 0.7f;                                   // turned while running
    const auto outs = rig.train(400.0 + 6 * P, 120.0, 6);
    const float m = outs.back().measured_bpm;
    CHECK(std::fabs(m - 120.0) < tol(120.0, kBlock, kFull));   // the clock wins
    rig.idle_until(double(rig.now) + 3 * P);
    REQUIRE(rig.lost_count() == 1);
    for (int i = 0; i < 100; ++i) CHECK(rig.tick().measured_bpm == m);   // unmoved: held
    rig.knob = 0.71f;
    CHECK(rig.tick().measured_bpm == 0.f);             // the first move takes over
    CHECK(rig.tick().measured_bpm == 0.f);
    rig.knob = 0.7f;                                   // and keeps it
    CHECK(rig.tick().measured_bpm == 0.f);
    // Returning pulses win again, after two valid edges.
    const double t = double(rig.now) + 300.0;
    const auto a = rig.clock_at(t);
    CHECK_FALSE(a.pulse);
    CHECK(a.measured_bpm == 0.f);
    const auto b = rig.clock_at(t + period_of(100.0));
    CHECK(b.pulse);
    CHECK(std::fabs(b.measured_bpm - 100.0) < tol(100.0, kBlock, 1));
}

TEST_CASE("clock law: armed reset at loss fires on the next beat wrap") {
    for (uint32_t q : {1u, kBlock}) {
        CAPTURE(q);
        const double P = period_of(120.0);
        Rig rig(q);
        rig.phase = 0.1f;
        rig.train(400.0, 120.0, 10);
        const double last = 400.0 + 9 * P;
        CHECK_FALSE(rig.reset_at(last + 0.05 * kSr).restart);   // armed
        // While the clock still runs, a beat wrap does not fire it.
        rig.phase = 0.9f; CHECK_FALSE(rig.tick().restart);
        rig.phase = 0.2f; CHECK_FALSE(rig.tick().restart);
        rig.phase = 0.3f;
        rig.idle_until(last + 2.2 * P);
        REQUIRE(rig.lost_count() == 1);
        for (const auto& e : rig.log) CHECK_FALSE(e.out.restart);
        rig.phase = 0.6f; CHECK_FALSE(rig.tick().restart);
        rig.phase = 0.95f; CHECK_FALSE(rig.tick().restart);
        rig.phase = 0.02f;                                  // the wrap
        const auto w = rig.tick();
        CHECK(w.restart);
        CHECK(w.restart_age == 0u);
        rig.phase = 0.9f; CHECK_FALSE(rig.tick().restart);
        rig.phase = 0.1f; CHECK_FALSE(rig.tick().restart);  // fired once
    }
}

// ---------------------------------------------------------------- lamps ----

TEST_CASE("clock law: lamps show every edge") {
    Rig rig(kBlock);
    // A first edge and out-of-range edges still light CLK.
    CHECK(rig.clock_at(1000.0).clock_lamp);
    for (int i = 1; i <= 4; ++i) {
        const auto o = rig.clock_at(1000.0 + i * period_of(480.0));
        CHECK(o.clock_lamp);
        CHECK_FALSE(o.pulse);
    }
    // RESET lights RST whatever it does: restart at once (no clock) ...
    CHECK(rig.reset_at(double(rig.now) + 5000.0).reset_lamp);
    const double P = period_of(120.0);
    const double t0 = double(rig.now) + 2 * P;
    rig.train(t0, 120.0, 8);
    const double last = t0 + 7 * P;
    // ... back-dated in the window, armed, and armed again.
    CHECK(rig.reset_at(last + 0.004 * kSr).reset_lamp);
    CHECK(rig.reset_at(last + 0.1 * kSr).reset_lamp);
    CHECK(rig.reset_at(last + 0.2 * kSr).reset_lamp);
    // A lamp lights only with its own jack's edge.
    for (const auto& e : rig.log) {
        CHECK(e.out.clock_lamp == e.clk);
        CHECK(e.out.reset_lamp == e.rst);
    }
}
