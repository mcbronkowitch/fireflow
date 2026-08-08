#include "sdram_mem.h"
#include "hw/board.h"   // zieht daisy_core.h und damit DSY_SDRAM_BSS herein

namespace shell {
namespace {

// --- SDRAM ------------------------------------------------------------
// Alles hier ist ein reines Array ohne Konstruktor. Das ist kein Zufall,
// sondern die Bedingung: .sdram_bss ist in alt_sram.lds als NOLOAD
// deklariert, und der Startup-Code fasst es nicht an. Ein Objekt mit
// Konstruktor an dieser Stelle wuerde in .init_array landen -- also VOR
// main() und damit vor hw.Init() laufen -- und in einen SDRAM-Adressraum
// schreiben, den noch kein FMC bedient. Siehe den Reverb-Block unten.

// FLUX-Bandspeicher: ein Stereo-Paar pro Part, zusammen 4 MiB.
float DSY_SDRAM_BSS g_echo[spky::PART_COUNT][2][spky::Flux::kMaxSamples];

// Die zwei Leitungen der BBD-Part-Engine pro Deck, gleiche Idiomatik:
// 2 x 2 x 8192 Floats = 128 KB.
float DSY_SDRAM_BSS g_bbd[spky::PART_COUNT][2][spky::BbdEngine::kCells];

// Der Textur-Deck-Aufnahmepuffer, ein Stereo-Frame-Feld pro Part. Der
// groesste Posten der Firmware ueberhaupt: 32,3 MB. Bleibt er nullptr,
// laeuft der Sampler des Parts still -- was ein Host verdient, der ihn
// vergessen hat, aber nicht das, was diese Firmware tun soll.
spky::SampleBuffer::Frame DSY_SDRAM_BSS
    g_sampler[spky::PART_COUNT][kSamplerFrames];

// --- SRAM -------------------------------------------------------------
// Der Reverb liegt in AXI-SRAM, NICHT in SDRAM -- eine bewusste Abweichung
// vom Plan (Task 5 Schritt 1: "Die Reverb-Instanz kommt ebenfalls nach
// SDRAM"). Der Grund ist Task 6, nicht Geschmack:
//
// Die 2,17 Punkte Reserve, gegen die der Shell-Aufschlag gerechnet wird,
// stammen aus bench/mem.cpp, und dort haengt FxMem::reverb an `g_rev_sram`,
// also an AXI-SRAM. Ein Shell, der denselben Reverb aus SDRAM fahren wuerde,
// haette schon im nackten process() eine andere Last als die gemessene
// Zeile -- und der Unterschied wuerde in Task 6 als "Shell-Aufschlag"
// verbucht, obwohl er von einer verschobenen Allokation kommt. Das ist
// dieselbe Fehlerform wie der SRAM_EXEC-Confound aus dem Board-Vergleich
// (docs/bench/2026-08-07-seed-vs-patch-sm.md), und sie ist hier vermeidbar,
// indem der Shell allokiert wie die Bench.
//
// Bench UND Shell hier zu aendern (beide nach SDRAM) waere die andere
// zulaessige Antwort. Sie kostet aber die gesamte Messhistorie und muss
// gemessen, nicht angenommen werden. Wer das will, misst es.
//
// Ein plain global ist hier korrekt, anders als es in SDRAM waere:
// AmbientReverb hat einen nicht-trivialen Default-Konstruktor (Member-
// Initializer auf _sr/_ctrl plus das eingebettete clouds::Oliverb), der in
// .init_array vor main() laeuft. In SRAM ist das harmlos -- der Speicher
// steht ab Reset. In SDRAM bus-faultet genau dieser Konstruktor
// (CFSR IMPRECISERR, bestaetigt 2026-07-18, siehe bench/mem.cpp:18-29).
// Wer den Reverb doch nach SDRAM verschiebt, braucht dort Rohspeicher plus
// placement-new nach board_init() und nicht dieses Global.
spky::AmbientReverb g_reverb;

spky::FxMem g_mem;
bool        g_mem_ready = false;

} // namespace

const spky::FxMem& fx_mem()
{
    if (!g_mem_ready) {
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            g_mem.echo[p][0] = g_echo[p][0];
            g_mem.echo[p][1] = g_echo[p][1];
            g_mem.bbd[p][0]  = g_bbd[p][0];
            g_mem.bbd[p][1]  = g_bbd[p][1];
            g_mem.sampler_buf[p] = g_sampler[p];
        }
        g_mem.sampler_frames = kSamplerFrames;
        g_mem.reverb = &g_reverb;
        g_mem_ready = true;
    }
    return g_mem;
}

} // namespace shell
