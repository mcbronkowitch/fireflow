// process() darf nicht davon abhaengen, wie viele Frames pro Aufruf kommen.
//
// Warum dieser Test bis 2026-08-08 gefehlt hat, und warum das teuer war:
// host/render/main.cpp ruft `inst.process(&in_l, &in_r, &l, &r, 1)` auf --
// EIN Sample pro Aufruf. Die gesamte Desktop-Messhistorie, jedes
// Referenzrender, jeder Vergleich "klingt auf dem Rechner richtig" ist also
// ausschliesslich mit n=1 entstanden. Die Firmware ruft mit n=96 auf. Ein
// Effekt, der einmal pro AUFRUF statt einmal pro SAMPLE passiert, ist auf
// dem Desktop damit systematisch unsichtbar und auf dem Board hoerbar --
// genau das Muster eines Stoertons auf der Blockrate (shell/README.md,
// "Offener Befund").
//
// Der Test ist deshalb nicht "noch eine Absicherung", sondern schliesst
// eine Luecke zwischen den Hosts, durch die ein Fehler dieser Form
// unbemerkt hindurchpasst.
#include <doctest/doctest.h>
#include <vector>
#include <cmath>
#include "instrument.h"

using namespace spky;

namespace {

// Eigener Speicher pro Instrument -- zwei Instrumente derselben Sitzung
// duerfen sich keine Echo-, BBD- oder Reverb-Zustaende teilen, sonst
// vergleicht der Test zwei Laeufe durch DENSELBEN Hall und findet
// Uebereinstimmung, wo keine ist.
struct OwnMem {
    std::vector<float> echo[PART_COUNT][2];
    std::vector<float> bbd[PART_COUNT][2];
    AmbientReverb      reverb;

    OwnMem() {
        for (int p = 0; p < PART_COUNT; ++p) {
            for (int ch = 0; ch < 2; ++ch) {
                echo[p][ch].assign(Flux::kMaxSamples, 0.f);
                bbd[p][ch].assign(BbdEngine::kCells, 0.f);
            }
        }
    }

    FxMem bind() {
        FxMem m;
        for (int p = 0; p < PART_COUNT; ++p) {
            for (int ch = 0; ch < 2; ++ch) {
                m.echo[p][ch] = echo[p][ch].data();
                m.bbd[p][ch]  = bbd[p][ch].data();
            }
        }
        m.reverb = &reverb;
        return m;
    }
};

// Derselbe Betriebspunkt, den shell/main.cpp fest einstellt.
void set_shell_operating_point(Instrument& inst) {
    inst.set_tempo_bpm(96.0f);
    inst.set_rate(PART_A, 0.4f);
    inst.set_density(PART_A, 0.6f);
}

// n Frames rendern, in Bloecken zu `block` Frames pro process()-Aufruf.
std::vector<float> render(size_t frames, size_t block, OwnMem& mem) {
    Instrument inst;
    const FxMem fx = mem.bind();
    inst.init(48000.f, fx);
    set_shell_operating_point(inst);

    std::vector<float> out;
    out.reserve(frames);

    std::vector<float> in_l(block, 0.f), in_r(block, 0.f);
    std::vector<float> ol(block, 0.f),   orr(block, 0.f);

    size_t done = 0;
    while (done < frames) {
        const size_t n = (frames - done < block) ? (frames - done) : block;
        inst.process(in_l.data(), in_r.data(), ol.data(), orr.data(), n);
        for (size_t i = 0; i < n; ++i) out.push_back(ol[i]);
        done += n;
    }
    return out;
}

} // namespace

TEST_CASE("instrument: process() is invariant to the frames-per-call size") {
    // Zwei Sekunden: lang genug, dass das 96-Sample-Steuerraster oft genug
    // gelaufen ist, um einen Unterschied aufzubauen, und kurz genug fuer
    // einen Unit-Test.
    constexpr size_t kFrames = 96 * 1000;

    OwnMem mem_one, mem_block;
    const std::vector<float> one   = render(kFrames, 1,  mem_one);
    const std::vector<float> block = render(kFrames, 96, mem_block);

    REQUIRE(one.size() == kFrames);
    REQUIRE(block.size() == kFrames);

    // KEIN Bit-Vergleich: dieses Repo fordert ausdruecklich keine
    // Bit-Exaktheit, und unterschiedliche Aufrufgroessen duerfen die
    // Reihenfolge von Fliesskommaoperationen aendern. Was NICHT passieren
    // darf, ist ein hoerbarer Unterschied -- und ein Knacks an jeder
    // Blockgrenze waere genau das.
    //
    // Die Schranke ist am Signal orientiert, nicht absolut: der
    // Betriebspunkt liegt bei rund -27 dBFS RMS, und -60 dB darunter ist
    // sicher unhoerbar, aber weit ueber jeder Umordnungs-Rundung.
    double sum_sq = 0.0, diff_sq = 0.0;
    float  worst = 0.f;
    for (size_t i = 0; i < kFrames; ++i) {
        const double s = one[i];
        const double d = static_cast<double>(block[i]) - s;
        sum_sq  += s * s;
        diff_sq += d * d;
        const float ad = std::fabs(static_cast<float>(d));
        if (ad > worst) worst = ad;
    }
    const double rms      = std::sqrt(sum_sq  / kFrames);
    const double rms_diff = std::sqrt(diff_sq / kFrames);

    INFO("rms=" << rms << " rms_diff=" << rms_diff << " worst_sample=" << worst);
    REQUIRE(rms > 1e-4);                       // sonst misst der Test Stille
    CHECK(rms_diff < rms * 1e-3);              // -60 dB relativ zum Signal
}
