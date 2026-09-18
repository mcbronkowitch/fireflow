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
#include "shell_cpu_probe.h"
#include "shell_mux_probe.h"
#include "shell_idle_fill.h"
#include "shell_coupon_probe.h"
#include "shell_settle_probe.h"
#include "shell_xtalk_probe.h"
#include "shell_tone_probe.h"
#include "hw/board.h"
#include "sdram_mem.h"
#include "instrument.h"

static bench::Board    hw;
static spky::Instrument inst;

#if SHELL_MUX_PROBE
#include "mux_scan.h"
static shell::MuxScan g_mux;
namespace {
// Ticked by the callback, read by the foreground. The foreground variant has
// to do the SAME number of steps per unit time as the callback variant, or
// the two numbers are not comparable -- pacing it off the audio block is the
// only clock both share.
volatile uint32_t g_block_tick = 0;
}
#endif

#if SHELL_COUPON_PROBE
#include "coupon_scan.h"
#endif

#if SHELL_SETTLE_PROBE
#include "settle_probe.h"
#endif

#if SHELL_XTALK_PROBE
#include "xtalk_probe.h"
#endif

#if SHELL_TONE_PROBE
#include "tone_probe.h"
#endif

#if defined(SHELL_CPU_PROBE)
#include <cstdint>
#include "util/CpuLoadMeter.h"
#endif

#if defined(SHELL_CPU_PROBE) || SHELL_COUPON_PROBE || SHELL_SETTLE_PROBE || SHELL_XTALK_PROBE || SHELL_TONE_PROBE
// libDaisy deklariert diese beiden in src/usbd/usbd_desc.c als
// `extern const char*` und definiert sie nie -- die Anwendung besitzt ihre
// eigene USB-Identitaet. Ohne sie scheitert der USB-Zweig beim LINKEN, nicht
// zur Laufzeit, was das gute Ende ist. `extern "C"`, sonst mangelt C++ die
// Namen und der C-Code findet sie nicht. Gleiche Stelle, gleicher Grund wie
// bench/report.cpp:19.
extern "C" {
const char* USBD_MANUFACTURER_STRING = "FireFlow";
const char* USBD_PRODUCT_STRING_HS   = "FireFlow Shell";
}
#endif

#if defined(SHELL_CPU_PROBE)
namespace {

// Wie lange gemessen wird. In BLOECKEN, nicht in Millisekunden, und das ist
// der Kern der Sache: ein Callback ueber Budget dehnt die Wanduhr, weil der
// naechste Block faellig ist, bevor der laufende fertig war. Eine
// Zeitschranke im Vordergrund wuerde von genau der Last verhungern, die sie
// begrenzen soll -- bench/anchor.cpp hat das 2026-07-18 auf Hardware
// erlebt: ein "4-Sekunden"-Abschnitt lief minutenlang. Blockzaehlung kann
// dieselbe Last nicht aushebeln.
constexpr float kProbeSeconds = 5.f;

daisy::CpuLoadMeter g_meter;
volatile uint32_t   g_probe_blocks = 0;
volatile bool       g_probe_done   = false;
uint32_t            g_probe_limit  = 0;

} // namespace
#endif

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

// Nur fuer die Leerlauf-Ablation (SHELL_IDLE_FILL). g_idle_never ist immer
// false und wird nie geschrieben; es steht da, damit die Schleife pro
// Iteration einen echten Ladezugriff macht, den der Compiler nicht
// wegoptimieren darf. g_idle_sink nimmt in Stellung 2 das Ergebnis auf, aus
// demselben Grund.
static volatile bool     g_idle_never = false;
static volatile uint32_t g_idle_sink  = 0;
static void AudioCallback(daisy::AudioHandle::InputBuffer  in,
                          daisy::AudioHandle::OutputBuffer out,
                          size_t                           size)
{
#if defined(SHELL_CPU_PROBE)
    // DER CALLBACK BEGRENZT SICH SELBST. Ist das Kontingent erreicht, wird er
    // schlagartig billig (Stille, sofortiges return) und gibt die CPU frei --
    // erst dadurch kommt der Vordergrund ueberhaupt wieder zum Zug und kann
    // das Ergebnis ausgeben. Ohne das bliebe eine Firmware ueber Budget im
    // Interruptkontext gesaettigt und saegte weiter DMA-Muell an die
    // Ausgaenge, ohne je etwas zu melden (bench/anchor.cpp, 2026-07-18).
    if(g_probe_done)
    {
        for(size_t i = 0; i < size; ++i) { out[0][i] = 0.f; out[1][i] = 0.f; }
        return;
    }

    g_meter.OnBlockStart();
    inst.process(in[0], in[1], out[0], out[1], size);
#if SHELL_MUX_PROBE == 1
    g_mux.step(hw);            // INSIDE the meter: that is the point
#endif
    g_meter.OnBlockEnd();

#if SHELL_MUX_PROBE == 2
    g_block_tick = g_block_tick + 1;   // outside the meter, on purpose
#endif

    if(++g_probe_blocks >= g_probe_limit) g_probe_done = true;
    return;
#endif

    inst.process(in[0], in[1], out[0], out[1], size);

#if SHELL_MUX_PROBE == 1
    g_mux.step(hw);
#endif
#if SHELL_MUX_PROBE == 2
    g_block_tick = g_block_tick + 1;
#endif

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

// libDaisy brings every ADC channel up at SPEED_8CYCLES_5 -- the default of
// AdcChannelConfig::InitSingle(), which DaisyPatchSM::Init() takes without
// asking (daisy_patch_sm.cpp:319) and offers no way to override.
//
// Measured on the coupon, 2026-09-18: at that window a 5150 ohm source -- a
// 20k pot at mid travel -- reads 19 counts low, and SPEED_16CYCLES_5 recovers
// 18 of them. Nothing longer buys anything, and that is the half of the
// measurement worth having: 31736 counts at 16.5 cycles against 31738 at
// 387.5, over a 23x range of window, is the reading standing still. The full
// ladder is in docs/hardware/settle-measured.md section 7. The error is small
// -- about 1.2 LSB of 12 bit -- but it is systematic and in the same direction
// for every pot on the panel, and it costs one rung to remove.
//
// Re-initialising after Init() rather than patching the submodule: the pin set
// and its ORDER are repeated verbatim from daisy_patch_sm.cpp:303-317, because
// AdcHandle::GetPtr(i) hands out &dma_buffer[i] (per/adc.cpp:401) and the
// AnalogControls bound during Init() keep those pointers. The array is static,
// so the addresses survive a re-Init -- but reorder the pins here and every
// control silently reads a different pin.
//
// Not in bench::board_init(), although that is where the rest of the board
// setup lives: the bench compares CPU numbers against a history of runs, and a
// longer conversion means more DMA traffic beside the measured workload. The
// shipping firmware is where this belongs; the measuring tool keeps the board
// it has always had.
#if defined(BENCH_BOARD_PATCH_SM)
static void adc_use_measured_sampling_time(bench::Board& board)
{
    using daisy::AdcChannelConfig;
    constexpr int kAdcCount = daisy::patch_sm::ADC_LAST;

    const daisy::Pin pins[kAdcCount] = {
        daisy::patch_sm::DaisyPatchSM::C5, daisy::patch_sm::DaisyPatchSM::C4,
        daisy::patch_sm::DaisyPatchSM::C3, daisy::patch_sm::DaisyPatchSM::C2,
        daisy::patch_sm::DaisyPatchSM::C9, daisy::patch_sm::DaisyPatchSM::C8,
        daisy::patch_sm::DaisyPatchSM::C6, daisy::patch_sm::DaisyPatchSM::C7,
        daisy::patch_sm::DaisyPatchSM::A2, daisy::patch_sm::DaisyPatchSM::A3,
        daisy::patch_sm::DaisyPatchSM::D9, daisy::patch_sm::DaisyPatchSM::D8,
    };

    AdcChannelConfig cfg[kAdcCount];
    for(int i = 0; i < kAdcCount; ++i)
        cfg[i].InitSingle(pins[i], AdcChannelConfig::SPEED_16CYCLES_5);

    board.StopAdc();
    board.adc.Init(cfg, kAdcCount);
    board.StartAdc();
}
#endif

int main(void)
{
    // Takt, Caches, SDRAM und das Audioformat, dazu der boot_info-Stempel,
    // ohne den der erste SDRAM-Zugriff HardFaultet. Alles davon und die
    // Begruendung fuer den Stempel steht in src/hw/board.h -- derselbe
    // Aufruf, den auch die Bench macht, und das ist der Punkt: haetten die
    // beiden je eine eigene Init-Sequenz, waere jeder Vergleich zwischen
    // ihren Zahlen wertlos.
    bench::board_init(hw);

#if defined(BENCH_BOARD_PATCH_SM)
    // Right after the board is up and before anything reads a control: the
    // ADC is already converting by then, so this is a restart, not a setup.
    adc_use_measured_sampling_time(hw);
#endif

    // REIHENFOLGE: erst board_init(), dann init(). fx_mem() selbst ist reine
    // Zeigerarbeit, aber Instrument::init() laeuft bis in TapeEcho::Init und
    // BbdLine::Init durch, und die nullen ihre Puffer -- also echte Schreiber
    // in SDRAM, das vor board_init() noch keinen FMC hinter sich hat.
    inst.init(shell::kSampleRate, shell::fx_mem());

#if SHELL_XTALK_PROBE
    // The board under test is the coupon, and the question is whether the
    // board's own digital side moves a settled pot reading. No engine, no
    // audio: StartAudio is deliberately never called here, because the codec
    // is round two's aggressor and an image that runs it cannot measure
    // round one's floor.
    shell::run_xtalk_probe(hw);   // never returns
#endif

#if SHELL_TONE_PROBE
    // The board under test is the coupon, and the aggressor is the codec.
    // Unlike every other probe image here, this one DOES start audio -- with
    // a callback that writes a tone and nothing else. No engine: the
    // operating point has to be "the codec, and only the codec".
    shell::run_tone_probe(hw);   // never returns
#endif

#if SHELL_SETTLE_PROBE
    // The board under test is the coupon, and the question is time, not
    // wiring. No engine, no audio: this image exists to say how long a mux
    // channel takes to settle to within half an LSB.
    shell::run_settle_probe(hw);   // never returns
#endif

#if SHELL_COUPON_PROBE
    // The board under test is the coupon, not an instrument. No engine, no
    // audio, no operating point: this image exists to say whether the thing
    // is wired the way the netlist claims.
    shell::run_coupon_bringup(hw);   // never returns
#endif

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

#if SHELL_MUX_PROBE
    // Vier Pins als Ausgang, einer als Eingang -- mehr passiert hier nicht.
    // Die Werte, die der Scan liest, gehen ABSICHTLICH nicht in die Engine:
    // der Betriebspunkt muss derselbe bleiben wie im Basis-Image, sonst
    // vergleicht die Audiomessung zwei verschiedene Instrumente.
    g_mux.init();
#endif

#if defined(SHELL_CPU_PROBE)
    // Die Blockgroesse wird NICHT angenommen, sondern beim Board erfragt und
    // mitgemeldet. Am 8. August ist genau diese Annahme einmal schiefgegangen
    // (aus Phasendauern auf 48 Samples geschlossen, tatsaechlich 96), und
    // eine Last in Prozent ist ohne die Blockgroesse, gegen die sie gerechnet
    // wurde, ohnehin bedeutungslos.
    const float  probe_sr = hw.AudioSampleRate();
    const size_t probe_bs = hw.AudioBlockSize();
    g_meter.Init(probe_sr, static_cast<int>(probe_bs));
    g_probe_limit  = static_cast<uint32_t>((kProbeSeconds * probe_sr)
                                           / static_cast<float>(probe_bs));
    g_probe_blocks = 0;
    g_probe_done   = false;

    hw.StartAudio(AudioCallback);
#if SHELL_MUX_PROBE == 2
    // Der Vordergrund-Scan, getaktet am Audioblock. NICHT freilaufend: er
    // muss pro Zeiteinheit genau so viele Schritte schaffen wie die
    // Callback-Variante, sonst vergleicht die Runde zwei Arbeitsmengen und
    // nicht zwei Platzierungen.
    uint32_t last_tick = 0;
    while(!g_probe_done)
    {
        const uint32_t t = g_block_tick;
        if(t != last_tick)
        {
            last_tick = t;
            g_mux.step(hw);
        }
    }
#else
    while(!g_probe_done) { }          // der Callback begrenzt sich selbst
#endif
    hw.StopAudio();

    // USB ERST JETZT hochfahren, nach der Messung. USB-CDC kostet auf diesem
    // Board 6370 Zyklen pro Block (0,66 %) durch den SOF-Interrupt
    // (docs/bench/2026-08-07-transport-semihost-vs-usb.md). Waere der Log
    // vorher offen, stuende dieser Aufschlag in der Zahl, die er melden soll.
    hw.StartLog(false);

    const uint32_t avg = static_cast<uint32_t>(g_meter.GetAvgCpuLoad() * 10000.f);
    const uint32_t mx  = static_cast<uint32_t>(g_meter.GetMaxCpuLoad() * 10000.f);
    const uint32_t mn  = static_cast<uint32_t>(g_meter.GetMinCpuLoad() * 10000.f);

    // Endlos wiederholt, damit ein Host, der den Port erst spaeter oeffnet,
    // das Ergebnis trotzdem bekommt -- es gibt hier keinen Handshake.
    while(1)
    {
        // steps= ist kein Beiwerk: die Vordergrund-Variante ist nur dann
        // gratis, wenn sie MITKOMMT. Ein Rueckstand gegen blocks= ist der
        // eigentliche Befund und darf nicht als Prozentzahl unsichtbar sein.
        hw.PrintLine("SHELL_CPU sr=%d block=%d blocks=%d avg=%d max=%d min=%d "
                     "mux=%d steps=%d hundredths_pct",
                     static_cast<int>(probe_sr), static_cast<int>(probe_bs),
                     static_cast<int>(g_probe_limit),
                     static_cast<int>(avg), static_cast<int>(mx),
                     static_cast<int>(mn),
                     static_cast<int>(SHELL_MUX_PROBE),
#if SHELL_MUX_PROBE
                     static_cast<int>(g_mux.steps()));
#else
                     0);
#endif
        hw.Delay(500);
    }
#endif

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
#elif SHELL_MUX_PROBE == 2
    // Ohne diese Schleife scannt das Audio-Image in Stellung 2 gar nichts --
    // und eine Aufnahme, die nichts misst, sieht aus wie eine, die nichts
    // findet.
    uint32_t idle_tick = 0;
    while(1)
    {
        const uint32_t t = g_block_tick;
        if(t != idle_tick)
        {
            idle_tick = t;
            g_mux.step(hw);
        }
    }
#else
#if SHELL_IDLE_FILL == 0
    // Sprung auf sich selbst. Der Kern fasst hier keinen Speicher an und
    // holt nach dem ersten Durchlauf nicht einmal mehr eine Instruktion --
    // das ist der leiseste Leerlauf, den dieses Board haben kann, und es
    // ist der, mit dem alle Audio-Bilder dieses Tages gemessen wurden.
    while(1) {}
#elif SHELL_IDLE_FILL == 1
    // Die Form, die das CPU-Sonden-Bild hat: pro Iteration ein Ladezugriff
    // auf ein volatile. Kein Rechenaufwand, nur Speicherverkehr. Wenn die
    // 15,5 dB Unterschied zwischen den beiden Bildern daran haengen, muss
    // DIESE Stellung sie reproduzieren -- und zwar ohne dass sich am
    // Callback ein Byte geaendert hat.
    while(!g_idle_never) { }
#else
    // Speicherverkehr plus echte ALU-Arbeit. Nicht als Betriebsmodus
    // gedacht, sondern als drittes Stuetzpunkt: liegt 2 noch tiefer als 1,
    // skaliert der Effekt mit der Leerlaufauslastung, statt nur zwischen
    // "leer" und "nicht leer" zu springen.
    uint32_t acc = 1u;
    while(!g_idle_never)
    {
        for(int i = 0; i < 64; ++i) acc = acc * 1664525u + 1013904223u;
        g_idle_sink = acc;
    }
#endif
#endif
}
