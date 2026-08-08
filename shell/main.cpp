// Die erste FireFlow-Firmware, die engine/ enthaelt.
//
// Was sie ist: Board hoch, Speicher injizieren, process() im Audio-Callback.
// Was sie ausdruecklich nicht ist: kein UI, keine Panel-Logik, kein Preset.
// Die Abgrenzung gegen bench/ und gegen die Upstream-Firmware im Root steht
// in shell/README.md.
// shell_selftest.h vor allem anderen: der generierte Header traegt das
// SHELL_SELFTEST-Define, und das Makefile gibt ihm eine echte
// Abhaengigkeitskante auf dieses Objekt (siehe dort, "Selbsttest").
#include "shell_selftest.h"
#include "hw/board.h"
#include "sdram_mem.h"
#include "instrument.h"

static bench::Board    hw;
static spky::Instrument inst;

#if defined(SHELL_SELFTEST)
#include <cstdint>
namespace {

// Eine Sekunde: 48000 / 96 = 500 Bloecke. Grosszuegig gewaehlt. Derselbe
// Betriebspunkt erreicht auf dem Desktop seinen vollen Pegel schon in den
// ersten 0,25 s (build/render.exe, gemessen 2026-08-08: Peak 0,42371 ueber
// jedes Fenster von 0,25 s aufwaerts), es wird hier also nicht knapp.
constexpr uint32_t kSelfTestBlocks = 500;

// -60 dBFS. Weit unter den erwarteten 0,42 und weit ueber allem, was
// Rundung oder ein stehengebliebener Denormal beitragen koennten -- die
// Schwelle trennt also "Engine laeuft" von "Engine liefert nichts" und
// nicht zwei Pegel voneinander.
constexpr float kSilenceFloor = 0.001f;

volatile uint32_t g_blocks = 0;
volatile float    g_peak   = 0.f;

} // namespace
#endif

static void AudioCallback(daisy::AudioHandle::InputBuffer  in,
                          daisy::AudioHandle::OutputBuffer out,
                          size_t                           size)
{
    inst.process(in[0], in[1], out[0], out[1], size);

#if defined(SHELL_SELFTEST)
    // Laeuft nur die erste Sekunde und danach nie wieder -- aber der
    // Vergleich davor bleibt fuer immer im Callback stehen, und genau
    // deshalb ist der ganze Block per Default ausgeschaltet.
    if(g_blocks < kSelfTestBlocks)
    {
        float pk = g_peak;
        for(size_t i = 0; i < size; ++i)
        {
            const float a = out[0][i] < 0.f ? -out[0][i] : out[0][i];
            if(a > pk) pk = a;
        }
        g_peak   = pk;
        g_blocks = g_blocks + 1;
    }
#endif
}

int main(void)
{
    // Takt, Caches, SDRAM und das Audioformat, dazu der boot_info-Stempel,
    // ohne den der erste SDRAM-Zugriff HardFaultet. Alles davon und die
    // Begruendung fuer den Stempel steht in src/hw/board.h -- derselbe
    // Aufruf, den auch die Bench macht, und das ist der Punkt: haetten die
    // beiden je eine eigene Init-Sequenz, waere jeder Vergleich zwischen
    // ihren Zahlen wertlos.
    bench::board_init(hw);

    // REIHENFOLGE: erst board_init(), dann init(). fx_mem() selbst ist reine
    // Zeigerarbeit, aber Instrument::init() laeuft bis in TapeEcho::Init und
    // BbdLine::Init durch, und die nullen ihre Puffer -- also echte Schreiber
    // in SDRAM, das vor board_init() noch keinen FMC hinter sich hat.
    inst.init(shell::kSampleRate, shell::fx_mem());
    inst.set_tempo_bpm(96.0f);

    // Fester Betriebspunkt, damit ohne Bedienelemente ueberhaupt etwas
    // klingt. Task 6 ersetzt die feste Zeile durch einen echten Poti.
    //
    // Dass diese zwei Zeilen reichen, ist nicht geraten: derselbe
    // Betriebspunkt als Szenario durch build/render.exe gibt ueber 5 s
    // Peak -7,5 dBFS und RMS -26,9 dBFS (gemessen 2026-08-08). Die
    // Boot-Default-Engine ist ENGINE_SYNTH (engine/parts/part.h:107), es
    // muss also keine Klangquelle erst ausgewaehlt werden. Bleibt das Board
    // trotzdem stumm, liegt es NICHT an diesen Werten -- dann zuerst Audio-
    // Routing und Ausgangspegel pruefen, nicht an den Knoepfen drehen.
    inst.set_rate(spky::PART_A, 0.4f);
    inst.set_density(spky::PART_A, 0.6f);

    hw.StartAudio(AudioCallback);

    // Kein Rueckweg in den Bootloader von hier aus, anders als in der Bench:
    // die Bench springt nach BENCH_END selbst nach DFU, weil sie wiederholt
    // geflasht wird. Der Shell soll laufen. Neu flashen heisst deshalb
    // RESET, dann BOOT im Zwei-Sekunden-Fenster -- steht auch im README.

#if defined(SHELL_SELFTEST)
    // Das Submodule hat keine Klinkenbuchse, also kann niemand hoeren, ob
    // die Engine Ton macht. Die User-LED sagt es stattdessen, und zwar in
    // vier unterscheidbaren Zustaenden -- der Grund fuer vier statt zwei
    // ist, dass "LED dunkel" sonst gleichzeitig "still" und "haengt beim
    // Booten" hiesse, und das waere kein Beweis, sondern ein Raetsel:
    //
    //   dunkel und bleibt dunkel  -> main() kam nie bis hierher.
    //                                Init, SDRAM oder StartAudio.
    //   schnelles Flackern (10 Hz) -> wir warten, der Callback kommt nicht
    //                                oder nicht oft genug durch.
    //   Dauerlicht                 -> Callback lief 500 Bloecke und der
    //                                Ausgang fuehrte Signal. Das ist der
    //                                Beweis, um den es geht.
    //   langsames Blinken (2 Hz)   -> Callback lief, Ausgang war still.
    //
    // Das Flackern waehrend des Wartens ist nicht Dekoration: ohne es
    // waere die Wartephase von "nie angekommen" nicht zu unterscheiden.
    while(g_blocks < kSelfTestBlocks)
    {
        hw.SetLed(true);
        hw.Delay(50);
        hw.SetLed(false);
        hw.Delay(50);
    }

    const bool signal = (g_peak > kSilenceFloor);
    while(1)
    {
        if(signal)
        {
            hw.SetLed(true);
            hw.Delay(1000);
        }
        else
        {
            hw.SetLed(true);
            hw.Delay(250);
            hw.SetLed(false);
            hw.Delay(250);
        }
    }
#else
    while(1) {}
#endif
}
