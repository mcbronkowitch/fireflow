#pragma once

// Die Engine hat keinen Heap (engine/instrument.h, "No heap"): sie bekommt
// ihren Speicher injiziert. Auf Daisy kommt der grosse Teil davon aus SDRAM.
// Der Sampler ist der dicke Posten -- 42 s Stereo pro Part, ~16 MB -- und
// passt nur, weil das Submodule 64 MB traegt. Wer hier eine Zahl aendert,
// aendert das Instrument.
//
// Diese Datei ist absichtlich die kleine Schwester von bench/mem.h und nicht
// deren Kopie: die Bench legt zusaetzlich Mess-Arenen an, die in der Firmware
// nichts verloren haben, und der Shell legt nur an, was FxMem verlangt. Was
// beide teilen, ist die AUFTEILUNG auf die Speicher -- und die zu teilen ist
// kein Stil, sondern die Voraussetzung dafuer, dass die Bench-Zahlen
// ueberhaupt etwas ueber diese Firmware aussagen (Task 6 rechnet den
// Shell-Aufschlag gegen 2,17 Punkte, die aus genau dieser Aufteilung stammen).

#include <cstddef>
#include "instrument.h"
#include "fx/reverb.h"
#include "sampler/sampler_config.h"
#include "sampler/rec_snap.h"

namespace shell {

// Das Board laeuft auf 48 kHz (src/hw/board.h setzt SAI_48KHZ). Die Engine
// bekommt dieselbe Zahl in main() noch einmal explizit, weil init() sie
// braucht -- hier steht sie, damit die Puffergroesse unten daran haengt und
// nicht an einer zweiten, still abweichenden Konstante.
constexpr float kSampleRate = 48000.f;

// Spec-Sizing, identisch zu beiden Desktop-Hosts (host/render/main.cpp und
// host/vcv/src/Fireflow.cpp rechnen 42.0 * sample_rate) und zu bench/mem.h.
// 2 016 000 Frames = 16,1 MB pro Part, zwei Parts, also 32,3 MB allein hier.
// NICHT als `42 * 48000` hingeschrieben, obwohl das dieselbe Zahl ergibt:
// sampler_cfg::kSizeCeilS ist die Stelle, an der die Spec diese Groesse
// festlegt, und eine zweite handgeschriebene 42 liefe beim naechsten
// Spec-Schritt still auseinander.
constexpr size_t kSamplerFrames =
    static_cast<size_t>(spky::sampler_cfg::kSizeCeilS * kSampleRate);

// The REC snap's ring per deck (spec 2026-11-07-sampler-records-other-deck-
// on-the-bar §4.2): the longest beat the snap acts on, plus the writer's
// slack. 72 128 frames = 577 KB per deck. Allocated only in SHELL_REC_SNAP
// images (sdram_mem.cpp).
constexpr size_t kRecRingFrames =
    static_cast<size_t>(spky::kSnapMaxBeatFrames) + spky::SamplerEngine::kRingSlack;

// Einmalig gebaut, lebt bis zum Reset. Reine Zeigerarbeit -- der Aufruf
// selbst fasst kein SDRAM an. Was SDRAM anfasst, ist Instrument::init(), das
// die Echo- und BBD-Leitungen nullt; deshalb steht die Reihenfolge
// board_init() -> init() in main.cpp und nicht hier.
//
// Der Name sagt fx_mem und nicht sdram_fx_mem (so hiess er im Plan): der
// Reverb liegt NICHT in SDRAM, und ein Name, der das behauptet, waere die
// Sorte falsches Etikett, die dieses Projekt teuer bezahlt hat. Warum er
// nicht dort liegt, steht in sdram_mem.cpp.
const spky::FxMem& fx_mem();

} // namespace shell
