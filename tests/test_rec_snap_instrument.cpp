// REC on the bar, wired into the Instrument (spec 2026-11-07-sampler-records-
// other-deck-on-the-bar §4.2, plan Task 6). The core is equivalence: a deck
// with the snap on, pressed off the line, records exactly what a deck with
// the snap off records when it is pressed on the line.
#include <doctest/doctest.h>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include "control/control_law.h"
#include "instrument.h"
using namespace spky;
using namespace ffctl;

namespace {

constexpr float  kSr   = 48000.f;
constexpr size_t kBuf  = 48000 * 10;     // 10 s per deck
constexpr size_t kRing = 72128;          // one 1.5 s beat plus slack

// The input both rigs hear: a fixed function of the frame, so two rigs fed
// frame by frame hear the same thing at the same frame.
float input_at(uint64_t f) {
    uint32_t s = static_cast<uint32_t>(f) * 2654435761u + 0x9e3779b9u;
    s ^= s >> 15; s *= 0x2c1b3c6du; s ^= s >> 12;
    return 0.5f * (static_cast<float>(s >> 8) / 8388608.f - 1.f);
}

// One instrument with its own FX, sampler and ring memory. Deck A is a
// SAMPLER deck at 120 BPM.
template <class Inst = Instrument>
struct RigT {
    std::vector<float> echo[PART_COUNT][2];
    std::vector<SampleBuffer::Frame> smp[PART_COUNT];
    std::vector<SampleBuffer::Frame> ring[PART_COUNT];
    std::unique_ptr<AmbientReverb> reverb = std::make_unique<AmbientReverb>();
    std::unique_ptr<Inst> inst = std::make_unique<Inst>();
    FxMem m;

    explicit RigT(bool snap, bool rings = true) {
        for (int p = 0; p < PART_COUNT; ++p) {
            for (int ch = 0; ch < 2; ++ch) {
                echo[p][ch].assign(Flux::kMaxSamples, 0.f);
                m.echo[p][ch] = echo[p][ch].data();
            }
            smp[p].assign(kBuf, SampleBuffer::Frame{ 0.f, 0.f });
            m.sampler_buf[p] = smp[p].data();
            if (rings) {
                ring[p].assign(kRing, SampleBuffer::Frame{ 0.f, 0.f });
                m.rec_ring[p] = ring[p].data();
            }
        }
        m.sampler_frames = kBuf;
        m.rec_ring_frames = rings ? kRing : 0;
        m.reverb = reverb.get();
        inst->init(kSr, m);
        inst->set_tempo_bpm(120.f);
        inst->set_pace(0.5f);
        inst->set_engine(PART_A, ENGINE_SAMPLER);
        inst->set_rec_snap(snap);
    }
    uint64_t now() const { return inst->frame_now(); }
    const SamplerEngine& deck() const { return inst->part(PART_A).sampler(); }
    void step() {
        const float x = input_at(now());
        float l, r;
        inst->process(&x, &x, &l, &r, 1);
    }
    void run_to(uint64_t f) { while (now() < f) step(); }
};
using Rig = RigT<>;

// The bar lines at 120 BPM, from a dry run: line k at lines()[k].
const std::vector<uint64_t>& lines() {
    static const std::vector<uint64_t> v = [] {
        Rig g(false);
        std::vector<uint64_t> out{ 0 };
        uint32_t seq = g.inst->bar_seq();
        while (out.size() < 8) {
            g.step();
            if (g.inst->bar_seq() != seq) { seq = g.inst->bar_seq(); out.push_back(g.inst->last_bar_frame()); }
        }
        return out;
    }();
    return v;
}
uint64_t L(int k) { return lines()[static_cast<size_t>(k)]; }

constexpr uint64_t kBeat = 24000;               // 120 BPM at PACE x1
constexpr uint64_t kOff  = kBeat * 3 / 10;      // 0.3 beat

enum Act { START, STOP, RST, CLEAR, LOAD };
struct Ev { uint64_t at; Act act; };

// Plays the events (in frame order) before the frame they name, then runs
// on until the deck has stopped writing and nothing is held or pending.
template <class R>
void play(R& g, const std::vector<Ev>& evs, uint64_t end) {
    size_t i = 0;
    while (g.now() < end) {
        while (i < evs.size() && evs[i].at == g.now()) {
            switch (evs[i].act) {
            case START: g.inst->sampler_record(PART_A, true);  break;
            case STOP:  g.inst->sampler_record(PART_A, false); break;
            case RST:   g.inst->reset_transport();            break;
            case CLEAR: g.inst->sampler_clear(PART_A);        break;
            case LOAD:  g.inst->sampler_begin_load(PART_A);   break;
            }
            ++i;
        }
        g.step();
    }
    REQUIRE(i == evs.size());
}

// Both decks idle, and the same take, frame for frame.
void same_take(const Rig& a, const Rig& b) {
    const SamplerEngine& x = a.deck();
    const SamplerEngine& y = b.deck();
    REQUIRE_FALSE(x.is_recording());
    REQUIRE_FALSE(y.is_recording());
    REQUIRE(x.rec_pending() == 0);
    REQUIRE(y.rec_pending() == 0);
    REQUIRE(x.rec_size() > 1000);                 // a real take, not two empty decks
    CHECK(x.rec_size() == y.rec_size());
    long first = -1;
    float peak = 0.f;
    for (size_t i = 0; i < x.capacity(); ++i) {
        const auto& fx = x.sample_data()[i];
        const auto& fy = y.sample_data()[i];
        peak = std::fmax(peak, std::fabs(fx.l));
        if (first < 0 && (fx.l != fy.l || fx.r != fy.r)) first = static_cast<long>(i);
    }
    CHECK(peak > 0.1f);
    CHECK(first == -1);
    uint32_t mfx[64], mfy[64];
    uint8_t  msx[64], msy[64];
    const int nx = x.export_markers(mfx, msx, 64);
    const int ny = y.export_markers(mfy, msy, 64);
    REQUIRE(nx == ny);
    for (int i = 0; i < nx; ++i) {
        CHECK(mfx[i] == mfy[i]);
        CHECK(msx[i] == msy[i]);
    }
}

// Snap on with `on`, snap off with `off`; the same take.
void equivalent(const std::vector<Ev>& on, const std::vector<Ev>& off, uint64_t end) {
    Rig a(false), b(true);
    REQUIRE(b.inst->rec_snap());
    REQUIRE(b.inst->frames_per_beat() == doctest::Approx(float(kBeat)));
    play(a, off, end);
    play(b, on, end);
    same_take(a, b);
}

} // namespace

TEST_CASE("snap: the dry run's bar lines are a bar apart") {
    // +-1: a line is interpolated inside its control block and floored.
    for (int k = 2; k < 8; ++k) {
        const int64_t d = static_cast<int64_t>(L(k) - L(k - 1)) - static_cast<int64_t>(4 * kBeat);
        CHECK(std::abs(d) <= 1);
    }
}

TEST_CASE("snap: late start") {
    const uint64_t stop = L(2) + 2 * kBeat;
    equivalent({ { L(1) + kOff, START }, { stop, STOP } },
               { { L(1), START },        { stop, STOP } }, L(4));
}

TEST_CASE("snap: early start") {
    const uint64_t stop = L(2) + 2 * kBeat;
    equivalent({ { L(1) - kOff, START }, { stop, STOP } },
               { { L(1), START },        { stop, STOP } }, L(4));
}

TEST_CASE("snap: late stop") {
    const uint64_t start = L(1) + 2 * kBeat;
    equivalent({ { start, START }, { L(2) + kOff, STOP } },
               { { start, START }, { L(2), STOP } }, L(4));
}

TEST_CASE("snap: early stop") {
    const uint64_t start = L(1) + 2 * kBeat;
    equivalent({ { start, START }, { L(2) - kOff, STOP } },
               { { start, START }, { L(2), STOP } }, L(4));
}

TEST_CASE("snap: overdub late start / late stop") {
    const uint64_t s1 = L(1) + 2 * kBeat, e1 = L(2) + 2 * kBeat;
    equivalent({ { s1, START }, { e1, STOP }, { L(3) + kOff, START }, { L(4) + kOff, STOP } },
               { { s1, START }, { e1, STOP }, { L(3), START },        { L(4), STOP } }, L(6));
}

TEST_CASE("snap: elsewhere acts at the press, one beat later") {
    const uint64_t s = L(1) + kBeat + kBeat / 2, e = L(2) + kBeat + kBeat / 2;
    equivalent({ { s, START }, { e, STOP } }, { { s, START }, { e, STOP } }, L(4));

    Rig b(true);
    play(b, { { s, START } }, s + 1);
    CHECK(b.inst->sampler_rec_requested(PART_A));
    CHECK_FALSE(b.inst->sampler_rec_held(PART_A));
    CHECK_FALSE(b.deck().is_recording());          // the writer is a beat behind
    b.run_to(s + kBeat);
    CHECK_FALSE(b.deck().is_recording());
    b.run_to(s + kBeat + 1);
    CHECK(b.deck().is_recording());                 // ... and reaches the press
}

TEST_CASE("snap: second press cancels a held start") {
    Rig b(true);
    play(b, { { L(1) - kOff, START } }, L(1) - kOff + 1);
    REQUIRE(b.inst->sampler_rec_held(PART_A));
    play(b, { { L(1) - kOff / 2, STOP } }, L(3));
    CHECK_FALSE(b.inst->sampler_rec_held(PART_A));
    CHECK_FALSE(b.inst->sampler_rec_requested(PART_A));
    CHECK_FALSE(b.deck().is_recording());
    CHECK(b.deck().rec_size() == 0);
}

TEST_CASE("snap: second press cancels a held stop") {
    const uint64_t s = L(1) + 2 * kBeat, e = L(3) + 2 * kBeat;
    equivalent({ { s, START }, { L(2) - kOff, STOP }, { L(2) - kOff / 2, START }, { e, STOP } },
               { { s, START }, { e, STOP } }, L(5));
}

TEST_CASE("snap: a stop with nothing requested is a no-op") {
    // The writer's fade-out after a stop is not a request; a stop pushed in
    // it -- the law's re-push before Task 6 -- must not become a held stop.
    Rig b(true);
    const uint64_t s = L(1) + kBeat, e = L(1) + 2 * kBeat;
    play(b, { { s, START }, { e, STOP }, { L(2) - kOff, STOP } }, L(2) - kOff + 1);
    CHECK_FALSE(b.inst->sampler_rec_held(PART_A));
    CHECK_FALSE(b.inst->sampler_rec_requested(PART_A));
}

TEST_CASE("snap: clear and begin_load cancel") {
    for (Act a : { CLEAR, LOAD }) {
        INFO((a == CLEAR ? "clear" : "begin_load"));
        SUBCASE("a held start") {
            Rig b(true);
            play(b, { { L(1) - kOff, START } }, L(1) - kOff + 1);
            REQUIRE(b.inst->sampler_rec_held(PART_A));
            play(b, { { L(1) - kOff / 2, a } }, L(2));
            CHECK_FALSE(b.inst->sampler_rec_held(PART_A));
            CHECK_FALSE(b.inst->sampler_rec_requested(PART_A));
            CHECK(b.deck().rec_pending() == 0);
            CHECK_FALSE(b.deck().is_recording());
        }
        SUBCASE("a pending start") {
            Rig b(true);
            play(b, { { L(1) + kOff, START } }, L(1) + kOff + 1);
            REQUIRE(b.deck().rec_pending() == 1);       // L1 is still ahead of the writer
            play(b, { { L(1) + kOff + 10, a } }, L(2));
            CHECK_FALSE(b.inst->sampler_rec_requested(PART_A));
            CHECK(b.deck().rec_pending() == 0);
            CHECK_FALSE(b.deck().is_recording());
        }
    }
}

TEST_CASE("snap: engine change stops at once") {
    Rig b(true);
    const uint64_t s = L(1) + kBeat;
    play(b, { { s, START } }, s + kBeat + kBeat / 2);  // mid-bar: a stop acts "now"
    REQUIRE(b.deck().is_writing());
    b.inst->sampler_record(PART_A, false);            // a stop the writer has not reached
    REQUIRE(b.deck().rec_pending() == 1);
    b.inst->set_engine(PART_A, ENGINE_SYNTH);
    CHECK_FALSE(b.deck().is_writing());
    CHECK(b.deck().rec_pending() == 0);
    CHECK_FALSE(b.inst->sampler_rec_requested(PART_A));
    CHECK_FALSE(b.inst->sampler_rec_held(PART_A));
}

TEST_CASE("snap: RST while a start is held starts on the reset line") {
    const uint64_t r = L(1) - kOff / 2;
    const uint64_t e = r + 6 * kBeat;                  // mid-bar after the reset
    equivalent({ { L(1) - kOff, START }, { r, RST }, { e, STOP } },
               { { r, RST }, { r, START }, { e, STOP } }, e + 3 * kBeat);
}

TEST_CASE("snap: a re-init drops what is held and keeps the setting") {
    Rig b(true);
    play(b, { { L(1) - kOff, START } }, L(1) - kOff + 1);
    REQUIRE(b.inst->sampler_rec_held(PART_A));
    b.inst->init(kSr, b.m);
    b.inst->set_tempo_bpm(120.f);
    b.inst->set_pace(0.5f);
    b.inst->set_engine(PART_A, ENGINE_SAMPLER);
    CHECK(b.inst->rec_snap());
    CHECK_FALSE(b.inst->sampler_rec_held(PART_A));
    CHECK_FALSE(b.inst->sampler_rec_requested(PART_A));
    b.run_to(b.now() + 3 * 4 * kBeat);                 // past three lines
    CHECK_FALSE(b.deck().is_recording());
    CHECK(b.deck().rec_size() == 0);
}

TEST_CASE("snap: set_rec_snap refused without rings") {
    Rig g(true, false);
    CHECK_FALSE(g.inst->rec_snap());
    Rig h(true);
    CHECK(h.inst->rec_snap());
}

namespace {
// The instrument with its REC presses counted. The law is a template on the
// instrument and this hides the base's sampler_record for it.
struct Counted : Instrument {
    int presses = 0;
    void sampler_record(int p, bool on) { if (p == PART_A) ++presses; Instrument::sampler_record(p, on); }
};
} // namespace

TEST_CASE("law: re-push in fade-out is not a press") {
    RigT<Counted> g(true);
    control::ControlLawT<Counted> law;
    law.on_restore();
    float k[NUM_PARAMS];
    for (int i = 0; i < NUM_PARAMS; ++i) k[i] = kInitParamDefaults[i];
    k[ENGINE_A] = 5.f;                                 // knob slot 5 = SAMPLER
    k[REC_A] = 0.f;
    control::Options opt{};
    float il[96], ir[96], ol[96], orr[96];
    auto block = [&] {
        law.tick(k, opt, *g.inst);
        for (int i = 0; i < 96; ++i) il[i] = ir[i] = input_at(g.now() + uint64_t(i));
        g.inst->process(il, ir, ol, orr, 96);
    };
    // Settle on the law's own tempo, then press in the middle of a bar.
    while (g.now() < 48000) block();
    REQUIRE(g.inst->rec_snap());
    REQUIRE(g.inst->engine_id(PART_A) == ENGINE_SAMPLER);
    while (g.inst->beat_in_bar() != 1) block();
    k[REC_A] = 1.f;
    const uint64_t beat = static_cast<uint64_t>(g.inst->frames_per_beat());
    const uint64_t on_until = g.now() + 4 * beat;
    while (g.now() < on_until) {                       // the writer starts a beat late
        block();
        CHECK(g.inst->presses == 1);
    }
    REQUIRE(g.deck().is_recording());
    while (g.inst->beat_in_bar() != 1) block();
    k[REC_A] = 0.f;
    block();
    REQUIRE(g.inst->presses == 2);
    const uint64_t off_until = g.now() + 8 * beat;     // the delayed stop, the fade-out, past a line
    while (g.now() < off_until) {
        block();
        CHECK(g.inst->presses == 2);
        CHECK_FALSE(g.inst->sampler_rec_held(PART_A));
    }
    CHECK_FALSE(g.deck().is_recording());
    CHECK(g.deck().rec_size() > 0);
}
