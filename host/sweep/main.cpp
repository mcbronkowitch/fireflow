// The law-driven sweep (plan 2026-10-29-cpu-hunt-remote Task 4, spec section
// 5.1): Rev A knob vectors through the firmware's own control path, ranked
// by the desktop cost of process().
//
//   sweep --pair SYNTH,WAVE (--random N --seed S | --corners | --vector FILE)
//         [--seconds 100] [--reps 1] [--input noise|silence] --out CSV
//   sweep --list-engines | --list-rows
//
// --seconds is simulated audio per vector (timed blocks, after the settle).
// --reps renders every vector that many times, REP-MAJOR (all vectors once,
// then all again), and keeps each block's fastest time (see stats()).
// Timings assume ONE sweep process on an otherwise idle machine: parallel
// sweeps share caches and arrive in bursts, and a 10 s run then reported
// higher maxima than the 100 s run it is an exact prefix of (Task 4 review).
// CSV: vector_id, eng_a, eng_b, p50_ns, p99_ns, max_ns, silent_frac (blocks
// with output peak below -60 dBFS), top8_mean_ns (mean of the 8 slowest
// blocks: the ranking statistic), rep_spread (noise gauge), the load's
// material excite_a, excite_b, rec_a, rec_b, settle_blocks, input, then
// row0..row72.
//
// A vector is one value per row of the Rev A panel table (kRevaTable,
// shell/generated_panel_map.h), x1000 of the normalised pot value -- what
// SHELL_PLAY_V prints and shell/workloads.json stores. It is loaded the way
// SHELL_BOOT_PATCH and a remote LOAD load one: the init patch
// (kInitParamDefaults), apply_boot_patch_rows() over it, the law re-armed with
// on_restore(). The ENGINE rows are always overwritten by --pair.
//
// Per block, in the firmware's order (shell/panel_scan.cpp, control_tick.cpp):
// the law ticks once over the whole vector, apply_excite_events() /
// apply_excite_hold() / apply_law_events() (shell/panel_step.cpp) apply its
// events, then process(..., 96) runs and is timed with steady_clock. Only
// process() is timed. Not modelled: the clock law (no CLOCK jack: measured_bpm
// is 0, as on a board with nothing patched), the LED law, the pot scan.
//
// Material (spec 5.2, "a worst case measured on silence does not count"):
//  - a deck whose ENGINE row asks for BBD gets excite_other_deck, held across
//    the engine switch -- what a live turn onto BBD (bbd_edge) and the boot
//    take (excite_after_take) give it;
//  - a SAMPLER deck has REC latched on through the settle and released at its
//    end, so the timed part plays back what it recorded;
//  - the audio input carries a deterministic low-passed noise at about
//    -12 dBFS (--input noise, the default), or zeros (--input silence). Only
//    the sampler and the BBD read it.
// Every vector starts from a fresh Instrument::init(), so two runs of one
// vector see the same transport and the same seeds.
//
// Desktop nanoseconds do not size board cycles (memory fireflow-daisy-control-
// cost); this ranks vectors within a pairing, nothing more. Task 9 checks the
// ranking against the board.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "instrument.h"
#include "control/control_law.h"
#include "control/params.hpp"
#include "sampler/sampler_config.h"
#include "shell/boot_patch.h"
#include "shell/generated_panel_map.h"
#include "shell/panel_step.h"

using namespace spky;

namespace {

constexpr float  kSampleRate   = 48000.f;
constexpr size_t kBlock        = 96;      // the shell's audio block (shell/main.cpp)
constexpr int    kSettleBlocks = 500;     // Task 5's settle before counting starts
constexpr int    kRecBlocks    = 2500;    // a sampler deck's settle: 5 s recorded
constexpr int    kRows         = shell::kRevaTable.count;

// FX memory, injected per the engine's no-heap contract -- host/render/main.cpp's idiom.
float              s_echo[PART_COUNT][2][Flux::kMaxSamples];
float              s_bbd[PART_COUNT][2][BbdEngine::kCells];
AmbientReverb      s_reverb;
std::vector<SampleBuffer::Frame> s_sampler[PART_COUNT];

FxMem make_fx_mem()
{
    FxMem m;
    const size_t frames = static_cast<size_t>(sampler_cfg::kSizeCeilS * kSampleRate);
    for(int p = 0; p < PART_COUNT; ++p)
    {
        m.echo[p][0] = s_echo[p][0];
        m.echo[p][1] = s_echo[p][1];
        m.bbd[p][0]  = s_bbd[p][0];
        m.bbd[p][1]  = s_bbd[p][1];
        s_sampler[p].assign(frames, SampleBuffer::Frame{0.f, 0.f});
        m.sampler_buf[p] = s_sampler[p].data();
    }
    m.sampler_frames = frames;
    m.reverb         = &s_reverb;
    return m;
}

// EngineId's enumerator names (engine/parts/engine_iface.h), for printing.
const char* engine_name(EngineId e)
{
    switch(e)
    {
        case ENGINE_TEST_TONE: return "TEST_TONE";
        case ENGINE_SYNTH: return "SYNTH";
        case ENGINE_SAMPLER: return "SAMPLER";
        case ENGINE_WAVE: return "WAVE";
        case ENGINE_BODY: return "BODY";
        case ENGINE_BBD: return "BBD";
        case ENGINE_FEED: return "FEED";
        default: return "?";
    }
}

int row_of(int param)
{
    for(int r = 0; r < kRows; ++r)
        if(shell::kRevaTable.entries[r].param == param) return r;
    return -1;
}

const int kEngineRow[2] = {row_of(ffctl::ENGINE_A), row_of(ffctl::ENGINE_B)};

// The ENGINE pot's row value for law slot s: knob_from_pot() maps v to
// round(lo + v * (hi - lo)), so the slot's own fraction lands on it.
int slot_x1000(int slot)
{
    const ffctl::ParamRange& r = ffctl::kParamRange[ffctl::ENGINE_A];
    return static_cast<int>(std::lround(1000.0 * (slot - r.lo) / (r.hi - r.lo)));
}

struct Engine { int slot; EngineId id; };

// Which engine each ENGINE pot position plays, asked of the law and the
// instrument themselves rather than written down here: every slot of the
// ENGINE knob's range is loaded through the panel path and run until the
// deck's engine switch has landed.
std::vector<Engine> discover_engines()
{
    std::vector<Engine> out;
    const ffctl::ParamRange& r = ffctl::kParamRange[ffctl::ENGINE_A];
    float in[kBlock] = {}, l[kBlock], rr[kBlock];
    for(int slot = static_cast<int>(r.lo); slot <= static_cast<int>(r.hi); ++slot)
    {
        static Instrument inst;
        inst.init(kSampleRate);
        static control::ControlLaw law;
        law = control::ControlLaw{};
        float knobs[ffctl::NUM_PARAMS];
        for(int i = 0; i < ffctl::NUM_PARAMS; ++i) knobs[i] = ffctl::kInitParamDefaults[i];
        knobs[ffctl::ENGINE_A] = static_cast<float>(slot);
        law.on_restore();
        control::Options opt;
        EngineId last = inst.engine_id(0);
        int      same = 0;
        for(int b = 0; b < 2000 && same < 50; ++b)
        {
            (void)law.tick(knobs, opt, inst);
            inst.process(in, in, l, rr, kBlock);
            const EngineId now = inst.engine_id(0);
            same = (now == last) ? same + 1 : 0;
            last = now;
            if(b < 200) same = 0;   // past any fade-and-swap before it may count as landed
        }
        out.push_back({slot, last});
    }
    return out;
}

bool deck_of(int param, int deck)
{
    using namespace ffctl;
    // The deck's strided block, then its appended pairs (control/params.hpp).
    const int lo = deck * PART_STRIDE;
    if(param >= lo && param < lo + PART_STRIDE) return true;
    static const int kAppended[][2] = {
        {FILT_A, FILT_B},       {FLUXRATE_A, FLUXRATE_B}, {FLUXFB_A, FLUXFB_B},
        {COLOR_A, COLOR_B},     {LINK_A, LINK_B},         {STAGES_A, STAGES_B},
        {REV_MIX_A, REV_MIX_B}, {DEPTH_A, DEPTH_B},       {PAN_A, PAN_B},
        {ROOT_A, ROOT_B},
    };
    for(const auto& pr : kAppended)
        if(param == pr[deck]) return true;
    return false;
}

// 'A', 'B', or 'G' (global) for a row.
char row_deck(int r)
{
    const int p = shell::kRevaTable.entries[r].param;
    if(deck_of(p, 0)) return 'A';
    if(deck_of(p, 1)) return 'B';
    return 'G';
}

struct Vec { std::string id; std::vector<int> rows; };

struct Result { double p50, p99, max, top8, spread, silent; };

uint64_t splitmix64(uint64_t& s)
{
    uint64_t z = (s += 0x9e3779b97f4a7c15ull);
    z          = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z          = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// The input material: white noise through a one-pole low-pass, about
// -12 dBFS peak. Restarted per vector from the same mixed seed (memory
// fireflow-probe-seed-warmup: never a bare small seed).
struct InputNoise
{
    uint32_t x = 0x1234abcdu + 1u * 2654435761u;
    float    y = 0.f;
    float next()
    {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        const float w = static_cast<float>(x) * (2.f / 4294967296.f) - 1.f;
        y += 0.2f * (w - y);
        return 0.6f * y;
    }
};

// What a load gives each deck besides the knob vector (file comment,
// "Material"); written into the CSV so the board can apply the same.
struct Material
{
    bool excite[2];   // excite_other_deck at load, held across the switch (BBD)
    bool rec[2];      // REC latched through the settle, released at its end (SAMPLER)
    int  settle;      // untimed blocks after the load
};

Material material_for(const EngineId want[2])
{
    Material m;
    for(int p = 0; p < 2; ++p)
    {
        m.excite[p] = want[p] == ENGINE_BBD;
        m.rec[p]    = want[p] == ENGINE_SAMPLER;
    }
    m.settle = (m.rec[0] || m.rec[1]) ? kRecBlocks : kSettleBlocks;
    return m;
}

// One render of one vector: load, settle, then `timed_blocks` timed blocks
// into ns. Returns the number of silent timed blocks.
int render_once(Instrument& inst, const FxMem& mem, const std::vector<int>& rows,
                const int slot[2], const EngineId want[2], int timed_blocks, bool noise,
                std::vector<double>& ns)
{
    inst.init(kSampleRate, mem);
    static control::ControlLaw law;
    law = control::ControlLaw{};
    float knobs[ffctl::NUM_PARAMS];
    for(int i = 0; i < ffctl::NUM_PARAMS; ++i) knobs[i] = ffctl::kInitParamDefaults[i];
    std::vector<int> r = rows;
    for(int p = 0; p < 2; ++p) r[kEngineRow[p]] = slot_x1000(slot[p]);
    if(shell::apply_boot_patch_rows(shell::kRevaTable, r.data(), kRows, knobs) < 0)
    {
        std::fprintf(stderr, "vector has %zu rows, the table %d\n", r.size(), kRows);
        std::exit(2);
    }
    law.on_restore();
    control::Options opt;
    const Material mat = material_for(want);
    bool hold[2];
    bool sampler[2];
    for(int p = 0; p < 2; ++p)
    {
        hold[p]                       = mat.excite[p];
        opt.deck[p].excite_other_deck = hold[p];
        sampler[p]                    = mat.rec[p];
    }
    const int  rec_ids[2] = {ffctl::REC_A, ffctl::REC_B};
    const int  settle = mat.settle;
    const int  flux_idx[2] = {-1, -1};

    InputNoise in_noise;
    float inL[kBlock], inR[kBlock], outL[kBlock], outR[kBlock];
    auto block = [&](bool timed, double* dt_ns, float* peak) {
        const control::Events ev = law.tick(knobs, opt, inst);
        shell::apply_excite_events(ev, opt);
        shell::apply_excite_hold(ev, knobs, hold, opt);
        shell::apply_law_events(ev, knobs, nullptr, flux_idx);
        for(size_t i = 0; i < kBlock; ++i)
        {
            const float v = noise ? in_noise.next() : 0.f;
            inL[i] = v;
            inR[i] = v;
        }
        const auto t0 = std::chrono::steady_clock::now();
        inst.process(inL, inR, outL, outR, kBlock);
        const auto t1 = std::chrono::steady_clock::now();
        if(timed)
        {
            *dt_ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
            float pk = 0.f;
            for(size_t i = 0; i < kBlock; ++i)
                pk = std::max(pk, std::max(std::fabs(outL[i]), std::fabs(outR[i])));
            *peak = pk;
        }
    };

    for(int p = 0; p < 2; ++p)
        if(sampler[p]) knobs[rec_ids[p]] = 1.f;
    for(int b = 0; b < settle; ++b) block(false, nullptr, nullptr);
    for(int p = 0; p < 2; ++p)
    {
        if(sampler[p]) knobs[rec_ids[p]] = 0.f;
        if(inst.engine_id(p) != want[p])
            std::fprintf(stderr, "warning: deck %c runs %s after the settle, asked %s\n",
                         'A' + p, engine_name(inst.engine_id(p)), engine_name(want[p]));
    }

    ns.assign(static_cast<size_t>(timed_blocks), 0.0);
    int silent = 0;
    for(int b = 0; b < timed_blocks; ++b)
    {
        float pk = 0.f;
        block(true, &ns[static_cast<size_t>(b)], &pk);
        if(!(pk >= 0.001f)) ++silent;   // -60 dBFS; a NaN block counts as silent
    }
    return silent;
}

// The statistics of one vector from its per-block minimum over the renders
// (see main(): the renders are rep-major). The render is deterministic, so
// block b does the same work in every rep, and the minimum drops what the
// desktop OS added to one of them. top8_mean_ns -- the mean of the 8 slowest
// blocks -- is what pick_workloads.py ranks on: one block's max is a single
// sample of the noise. rep_spread: (largest - smallest) / smallest of the
// per-rep max_ns, a noise gauge for the vector (0 with --reps 1).
Result stats(const std::vector<double>& best, const std::vector<double>& rep_max, int silent)
{
    std::vector<double> s = best;
    std::sort(s.begin(), s.end());
    const size_t n = s.size();
    Result res;
    res.p50    = s[n / 2];
    res.p99    = s[static_cast<size_t>(std::floor(0.99 * static_cast<double>(n - 1)))];
    res.max    = s[n - 1];
    const size_t k = std::min<size_t>(8, n);
    double sum = 0.0;
    for(size_t i = n - k; i < n; ++i) sum += s[i];
    res.top8 = sum / static_cast<double>(k);
    const auto mm = std::minmax_element(rep_max.begin(), rep_max.end());
    res.spread = *mm.first > 0.0 ? (*mm.second - *mm.first) / *mm.first : 0.0;
    res.silent = static_cast<double>(silent) / static_cast<double>(n);
    return res;
}

std::vector<std::string> split(const std::string& s, char c)
{
    std::vector<std::string> out;
    std::string cur;
    std::istringstream is(s);
    while(std::getline(is, cur, c)) out.push_back(cur);
    if(!s.empty() && s.back() == c) out.push_back("");
    return out;
}

// A CSV this tool wrote (or any CSV with vector_id and row0..rowN columns).
std::vector<Vec> read_vectors(const std::string& path)
{
    std::ifstream f(path);
    if(!f)
    {
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        std::exit(2);
    }
    std::string line;
    std::getline(f, line);
    if(!line.empty() && line.back() == '\r') line.pop_back();
    const std::vector<std::string> head = split(line, ',');
    int id_col = -1;
    std::vector<int> row_col(kRows, -1);
    for(size_t i = 0; i < head.size(); ++i)
    {
        if(head[i] == "vector_id") id_col = static_cast<int>(i);
        for(int r = 0; r < kRows; ++r)
            if(head[i] == "row" + std::to_string(r)) row_col[r] = static_cast<int>(i);
    }
    if(id_col < 0 || std::count(row_col.begin(), row_col.end(), -1) > 0)
    {
        std::fprintf(stderr, "%s: needs vector_id and row0..row%d columns\n", path.c_str(), kRows - 1);
        std::exit(2);
    }
    std::vector<Vec> out;
    while(std::getline(f, line))
    {
        if(!line.empty() && line.back() == '\r') line.pop_back();
        if(line.empty()) continue;
        const std::vector<std::string> c = split(line, ',');
        Vec v;
        v.id = c.at(static_cast<size_t>(id_col));
        for(int r = 0; r < kRows; ++r) v.rows.push_back(std::atoi(c.at(static_cast<size_t>(row_col[r])).c_str()));
        out.push_back(v);
    }
    return out;
}

std::vector<Vec> corner_vectors()
{
    std::vector<Vec> out;
    for(int deck = 0; deck < 2; ++deck)
        for(int val : {0, 1000})
        {
            Vec v;
            v.id = std::string("corner_") + char('A' + deck) + std::to_string(val);
            for(int r = 0; r < kRows; ++r)
                v.rows.push_back(row_deck(r) == char('A' + deck) ? val : 500);
            out.push_back(v);
        }
    return out;
}

std::vector<Vec> random_vectors(int n, uint64_t seed)
{
    // Mixed the way the instrument mixes its lane seeds (memory
    // fireflow-probe-seed-warmup); splitmix64 has no cold start besides.
    uint64_t s = 0x1234abcdull + seed * 2654435761ull;
    std::vector<Vec> out;
    for(int i = 0; i < n; ++i)
    {
        Vec v;
        v.id = "rnd" + std::to_string(seed) + "_" + std::to_string(i);
        for(int r = 0; r < kRows; ++r) v.rows.push_back(static_cast<int>(splitmix64(s) % 1001u));
        out.push_back(v);
    }
    return out;
}

int usage()
{
    std::fprintf(stderr,
                 "usage: sweep --pair ENGA,ENGB (--random N --seed S | --corners | --vector FILE)...\n"
                 "             [--seconds 100] [--reps 1] [--input noise|silence] --out CSV\n"
                 "       sweep --list-engines | --list-rows\n"
                 "Timings assume ONE sweep process on an otherwise idle machine; --reps is rep-major.\n");
    return 1;
}

} // namespace

int main(int argc, char** argv)
{
    std::string pair, vector_file, out_path, input = "noise";
    int         n_random = 0;
    uint64_t    seed     = 1;
    bool        corners = false, list_engines = false, list_rows = false;
    double      seconds  = 100.0;
    int         reps     = 1;
    for(int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto val = [&]() -> std::string {
            if(i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", a.c_str()); std::exit(1); }
            return argv[++i];
        };
        if(a == "--pair") pair = val();
        else if(a == "--random") n_random = std::atoi(val().c_str());
        else if(a == "--seed") seed = std::strtoull(val().c_str(), nullptr, 10);
        else if(a == "--corners") corners = true;
        else if(a == "--vector") vector_file = val();
        else if(a == "--seconds") seconds = std::atof(val().c_str());
        else if(a == "--reps") reps = std::atoi(val().c_str());
        else if(a == "--out") out_path = val();
        else if(a == "--input") input = val();
        else if(a == "--list-engines") list_engines = true;
        else if(a == "--list-rows") list_rows = true;
        else return usage();
    }

    if(list_rows)
    {
        std::printf("row,param,deck\n");
        for(int r = 0; r < kRows; ++r)
            std::printf("%d,%d,%c\n", r, shell::kRevaTable.entries[r].param, row_deck(r));
        return 0;
    }

    const std::vector<Engine> engines = discover_engines();
    if(list_engines)
    {
        std::printf("slot,row_x1000,engine_id,engine\n");
        for(const Engine& e : engines)
            std::printf("%d,%d,%d,%s\n", e.slot, slot_x1000(e.slot), static_cast<int>(e.id),
                        engine_name(e.id));
        return 0;
    }

    if(pair.empty() || out_path.empty() || (input != "noise" && input != "silence") ||
       (n_random == 0 && !corners && vector_file.empty()) || seconds <= 0.0 || reps < 1)
        return usage();
    const std::vector<std::string> names = split(pair, ',');
    if(names.size() != 2) return usage();
    int      slot[2];
    EngineId want[2];
    for(int p = 0; p < 2; ++p)
    {
        slot[p] = -1;
        for(const Engine& e : engines)
            if(names[static_cast<size_t>(p)] == engine_name(e.id)) { slot[p] = e.slot; want[p] = e.id; }
        if(slot[p] < 0)
        {
            std::fprintf(stderr, "engine %s is not on the ENGINE knob; it has:", names[static_cast<size_t>(p)].c_str());
            for(const Engine& e : engines) std::fprintf(stderr, " %s", engine_name(e.id));
            std::fprintf(stderr, "\n");
            return 1;
        }
    }

    std::vector<Vec> vecs;
    if(corners) for(Vec& v : corner_vectors()) vecs.push_back(v);
    if(n_random > 0) for(Vec& v : random_vectors(n_random, seed)) vecs.push_back(v);
    if(!vector_file.empty()) for(Vec& v : read_vectors(vector_file)) vecs.push_back(v);

    FILE* f = std::fopen(out_path.c_str(), "wb");
    if(!f)
    {
        std::fprintf(stderr, "cannot write %s\n", out_path.c_str());
        return 2;
    }
    std::fprintf(stderr, "sweep: timings assume ONE sweep process on an otherwise idle machine; "
                         "parallel runs or other load corrupt the ranking\n");

    static Instrument inst;
    const FxMem       mem    = make_fx_mem();
    const int         timed  = static_cast<int>(std::lround(seconds * kSampleRate / kBlock));
    const auto        start  = std::chrono::steady_clock::now();
    // Rep-major: every vector once, then every vector again. A burst of
    // outside load then lands in one rep of a vector, not in all of them,
    // and the per-block minimum removes it.
    std::vector<std::vector<double>> best(vecs.size());
    std::vector<std::vector<double>> rep_max(vecs.size());
    std::vector<int>                 silent(vecs.size(), 0);
    std::vector<double>              ns;
    for(int rep = 0; rep < reps; ++rep)
        for(size_t k = 0; k < vecs.size(); ++k)
        {
            const int sil = render_once(inst, mem, vecs[k].rows, slot, want, timed, input == "noise", ns);
            if(rep == 0)
            {
                best[k]   = ns;
                silent[k] = sil;
            }
            else
                for(size_t b = 0; b < ns.size(); ++b) best[k][b] = std::min(best[k][b], ns[b]);
            rep_max[k].push_back(*std::max_element(ns.begin(), ns.end()));
            const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::fprintf(stderr, "[rep %d/%d, %zu/%zu] %s %s,%s max=%.0f (%.1f s)\n", rep + 1, reps,
                         k + 1, vecs.size(), vecs[k].id.c_str(), engine_name(want[0]),
                         engine_name(want[1]), rep_max[k].back(), el);
        }

    const Material mat = material_for(want);
    std::fprintf(f, "vector_id,eng_a,eng_b,p50_ns,p99_ns,max_ns,silent_frac,top8_mean_ns,rep_spread,"
                    "excite_a,excite_b,rec_a,rec_b,settle_blocks,input");
    for(int r = 0; r < kRows; ++r) std::fprintf(f, ",row%d", r);
    std::fprintf(f, "\n");
    for(size_t k = 0; k < vecs.size(); ++k)
    {
        const Vec&   v   = vecs[k];
        const Result res = stats(best[k], rep_max[k], silent[k]);
        std::fprintf(f, "%s,%s,%s,%.0f,%.0f,%.0f,%.4f,%.0f,%.4f,%d,%d,%d,%d,%d,%s", v.id.c_str(),
                     engine_name(want[0]), engine_name(want[1]), res.p50, res.p99, res.max, res.silent,
                     res.top8, res.spread, mat.excite[0], mat.excite[1], mat.rec[0], mat.rec[1],
                     mat.settle, input.c_str());
        for(int r = 0; r < kRows; ++r)
            std::fprintf(f, ",%d", r == kEngineRow[0] ? slot_x1000(slot[0])
                                   : r == kEngineRow[1] ? slot_x1000(slot[1])
                                                        : v.rows[static_cast<size_t>(r)]);
        std::fprintf(f, "\n");
    }
    std::fclose(f);
    return 0;
}
