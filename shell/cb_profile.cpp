#include "cb_profile.h"

#if defined(SPKY_PROBE_MARKS)

#include <atomic>
#include "cycles.h"
#include "util/probe_mark.h"

// The engine's stamp clock and its once-per-block hand-over
// (engine/util/probe_mark.h).
namespace spky {
volatile uint32_t* probe_clock = &DWT->CYCCNT;
ProbePartSlots     probe_part_scratch;
} // namespace spky

namespace {

// This block's process() figures, written by probe_block() inside process()
// and read by cb_profile_block_end() after it, both in the audio interrupt.
uint32_t g_proc  = 0;    // process(), first stamp to last
uint32_t g_maxs  = 0;    // the most expensive single sample
int      g_maxi  = 0;    // its index in the block
uint32_t g_med   = 0;    // the mean of the other samples of this block
uint32_t g_deck[2] = {0, 0};  // deck A / deck B process(), summed over the block
uint32_t g_mix   = 0;    // the rest of process(): CHOKE, mix, reverb, limiter
// Each deck's process() split three ways (fault R4), summed over the block:
// [0] modulation + control (the lanes, the raster tick, the fire; on the
// priority deck also the top-of-sample centre update, on the yielding deck
// the duck stage), [1] the engine, [2] the FX chain. The three sum to g_deck.
enum { SEG_MOD = 0, SEG_ENG = 1, SEG_FX = 2, SEG_COUNT = 3 };
uint32_t g_seg[2][SEG_COUNT] = {{0, 0, 0}, {0, 0, 0}};

uint32_t g_block = 0;    // blocks since StartAudio
int      g_eng[2] = {-1, -1};  // each deck's engine id at the last block end

// Lane state at the previous block end, per deck and lane: a changed phrase
// index (bit d*10+l, a wrap that advanced the song) or step index (bit
// d*10+5+l) is this block's event.
uint32_t g_wraps[2][spky::LANE_COUNT];
int      g_step[2][spky::LANE_COUNT];
bool     g_have_prev = false;

// One ring entry: a block above kHot, or any block in which a phrase advanced.
constexpr uint32_t kHot = 930000u;
struct Rec {
    uint32_t blk, cb, proc, law, led, maxs, med, ev;
    int      maxi;
    uint32_t deck[2], mix;
    int      voices[2];
};
constexpr int     kRing = 64;
Rec               g_ring[kRing];
volatile uint32_t g_ring_w = 0;   // entries written, ever
uint32_t          g_ring_r = 0;   // foreground: entries printed

// Summary over warm blocks, and the histogram of the whole callback:
// 10 000-cycle bins from 850 000, bin 0 below, the last at 1 060 000 and up.
uint64_t           g_sum_cb = 0, g_sum_proc = 0, g_sum_deck[2] = {0, 0}, g_sum_mix = 0;
uint32_t           g_max_cb = 0, g_max_proc = 0, g_max_maxs = 0;
volatile uint32_t  g_n = 0;
constexpr uint32_t kHistLo = 850000u, kHistStep = 10000u;
constexpr int      kHist   = 23;
uint32_t           g_hist[kHist];

// A window of kWinBlocks warm blocks (2 s), so a change made mid-run shows
// in the next window instead of vanishing into the since-boot means above
// (fault R4: ablating one effect at a time). The interrupt sums the open
// window and publishes it whole at its end; the foreground prints a window
// once, read between two equal g_win_n readings.
constexpr uint32_t kWinBlocks = 1000u;
struct Win {
    uint32_t cb, proc, deck[2], mix, law, cb_max;
    uint32_t seg[2][SEG_COUNT];
};
uint64_t          g_wsum_cb = 0, g_wsum_proc = 0, g_wsum_deck[2] = {0, 0}, g_wsum_mix = 0,
                  g_wsum_law = 0, g_wsum_seg[2][SEG_COUNT] = {{0, 0, 0}, {0, 0, 0}};
uint64_t          g_sum_seg[2][SEG_COUNT] = {{0, 0, 0}, {0, 0, 0}};
uint32_t          g_wmax_cb = 0, g_wn = 0;
volatile Win      g_win_pub;
volatile uint32_t g_win_n = 0;

uint32_t lane_events(const spky::Instrument& inst)
{
    uint32_t ev = 0;
    for(int p = 0; p < 2; ++p)
    {
        const spky::SuperModulator& m = inst.probe_mod(p);
        for(int l = 0; l < spky::LANE_COUNT; ++l)
        {
            const uint32_t w = m.lane_song_position(l);
            const int      s = m.lane_cur_step(l);
            if(g_have_prev && w != g_wraps[p][l]) ev |= 1u << (p * 10 + l);
            if(g_have_prev && s != g_step[p][l])  ev |= 1u << (p * 10 + 5 + l);
            g_wraps[p][l] = w;
            g_step[p][l]  = s;
        }
    }
    g_have_prev = true;
    return ev;
}

} // namespace

namespace spky {
void probe_block(const uint32_t* ts, const uint32_t* d1, const uint32_t* d2,
                 const ProbePartSlots* p1, const ProbePartSlots* p2,
                 int n, int pri)
{
    g_proc = ts[n] - ts[0];
    uint32_t a = 0, b = 0, m = 0;
    uint32_t sa[SEG_COUNT] = {0, 0, 0}, sb[SEG_COUNT] = {0, 0, 0};
    for(int i = 0; i < n; ++i)
    {
        a += d1[i] - ts[i];        // top of the sample + the priority deck
        b += d2[i] - d1[i];        // the yielding deck
        m += ts[i + 1] - d2[i];    // mix, reverb, limiter
        sa[SEG_MOD] += p1[i][0] - ts[i];
        sa[SEG_ENG] += p1[i][1] - p1[i][0];
        sa[SEG_FX]  += d1[i] - p1[i][1];
        sb[SEG_MOD] += p2[i][0] - d1[i];
        sb[SEG_ENG] += p2[i][1] - p2[i][0];
        sb[SEG_FX]  += d2[i] - p2[i][1];
    }
    g_deck[pri]     = a;
    g_deck[1 - pri] = b;
    g_mix           = m;
    for(int s = 0; s < SEG_COUNT; ++s)
    {
        g_seg[pri][s]     = sa[s];
        g_seg[1 - pri][s] = sb[s];
    }
    uint32_t mx = 0;
    int      mi = 0;
    for(int i = 0; i < n; ++i)
    {
        const uint32_t d = ts[i + 1] - ts[i];
        if(d > mx) { mx = d; mi = i; }
    }
    g_maxs = mx;
    g_maxi = mi;
    g_med  = n > 1 ? (g_proc - mx) / static_cast<uint32_t>(n - 1) : g_proc;
}
} // namespace spky

namespace shell {

void cb_profile_init()
{
    cycles_init();
}

void cb_profile_block_end(uint32_t cb, uint32_t law, uint32_t led, bool warm,
                          const spky::Instrument& inst)
{
    const uint32_t blk = g_block++;
    const uint32_t ev  = lane_events(inst);
    g_eng[0] = static_cast<int>(inst.engine_id(0));
    g_eng[1] = static_cast<int>(inst.engine_id(1));
    if(!warm) return;
    g_sum_cb += cb;
    g_sum_proc += g_proc;
    g_sum_deck[0] += g_deck[0];
    g_sum_deck[1] += g_deck[1];
    g_sum_mix += g_mix;
    if(cb > g_max_cb) g_max_cb = cb;
    if(g_proc > g_max_proc) g_max_proc = g_proc;
    if(g_maxs > g_max_maxs) g_max_maxs = g_maxs;
    g_n = g_n + 1u;
    int b = cb < kHistLo ? 0 : 1 + static_cast<int>((cb - kHistLo) / kHistStep);
    if(b > kHist - 1) b = kHist - 1;
    ++g_hist[b];
    g_wsum_cb += cb;
    g_wsum_proc += g_proc;
    g_wsum_deck[0] += g_deck[0];
    g_wsum_deck[1] += g_deck[1];
    g_wsum_mix += g_mix;
    g_wsum_law += law;
    for(int p = 0; p < 2; ++p)
        for(int s = 0; s < SEG_COUNT; ++s)
        {
            g_wsum_seg[p][s] += g_seg[p][s];
            g_sum_seg[p][s] += g_seg[p][s];
        }
    if(cb > g_wmax_cb) g_wmax_cb = cb;
    if(++g_wn == kWinBlocks)
    {
        for(int p = 0; p < 2; ++p)
            for(int s = 0; s < SEG_COUNT; ++s)
            {
                g_win_pub.seg[p][s] = static_cast<uint32_t>(g_wsum_seg[p][s] / kWinBlocks);
                g_wsum_seg[p][s]    = 0;
            }
        g_win_pub.cb      = static_cast<uint32_t>(g_wsum_cb / kWinBlocks);
        g_win_pub.proc    = static_cast<uint32_t>(g_wsum_proc / kWinBlocks);
        g_win_pub.deck[0] = static_cast<uint32_t>(g_wsum_deck[0] / kWinBlocks);
        g_win_pub.deck[1] = static_cast<uint32_t>(g_wsum_deck[1] / kWinBlocks);
        g_win_pub.mix     = static_cast<uint32_t>(g_wsum_mix / kWinBlocks);
        g_win_pub.law     = static_cast<uint32_t>(g_wsum_law / kWinBlocks);
        g_win_pub.cb_max  = g_wmax_cb;
        g_wsum_cb = g_wsum_proc = g_wsum_deck[0] = g_wsum_deck[1] = g_wsum_mix = g_wsum_law = 0;
        g_wmax_cb = 0;
        g_wn      = 0;
        g_win_n   = g_win_n + 1u;
    }
    const uint32_t wrap_mask = 0x1Fu | (0x1Fu << 10);
    if(cb > kHot || (ev & wrap_mask) != 0u)
    {
        Rec& r = g_ring[g_ring_w % kRing];
        r = Rec{blk, cb, g_proc, law, led, g_maxs, g_med, ev, g_maxi,
                {g_deck[0], g_deck[1]}, g_mix,
                {inst.probe_voices(0), inst.probe_voices(1)}};
        std::atomic_signal_fence(std::memory_order_seq_cst);
        g_ring_w = g_ring_w + 1u;
    }
}

namespace {
uint32_t g_calls = 0;
} // namespace

void cb_profile_report(bench::Board& hw)
{
    std::atomic_signal_fence(std::memory_order_seq_cst);
    const uint32_t w = g_ring_w;
    if(w - g_ring_r > static_cast<uint32_t>(kRing))
    {
        hw.PrintLine("CBP_LOST %d", static_cast<int>(w - g_ring_r - kRing));
        g_ring_r = w - kRing;
    }
    for(; g_ring_r != w; ++g_ring_r)
    {
        const Rec r = g_ring[g_ring_r % kRing];
        // At most ~110 bytes, inside libDaisy's 128-byte log buffer.
        hw.PrintLine("CBP_REC blk=%d cb=%d proc=%d law=%d led=%d maxs=%d@%d med=%d ev=%x",
                     static_cast<int>(r.blk), static_cast<int>(r.cb),
                     static_cast<int>(r.proc), static_cast<int>(r.law),
                     static_cast<int>(r.led), static_cast<int>(r.maxs), r.maxi,
                     static_cast<int>(r.med), static_cast<unsigned>(r.ev));
        hw.PrintLine("CBP_REC2 blk=%d a=%d b=%d mix=%d va=%d vb=%d", static_cast<int>(r.blk),
                     static_cast<int>(r.deck[0]), static_cast<int>(r.deck[1]),
                     static_cast<int>(r.mix), r.voices[0], r.voices[1]);
    }
    // Every engine swap (fault R2): its whole cost and BbdEngine::reset()'s
    // share, printed once per new swap. Copied before printing; a swap landing
    // mid-copy is at worst one stale line, and the next one repeats it.
    static uint32_t swaps_seen = 0;
    if(spky::probe_swap.n != swaps_seen)
    {
        const spky::ProbeSwap s = spky::probe_swap;
        swaps_seen              = s.n;
        hw.PrintLine("CBP_SWAP n=%d eng=%d total=%d bbd=%d total_max=%d bbd_max=%d",
                     static_cast<int>(s.n), s.eng, static_cast<int>(s.total),
                     static_cast<int>(s.bbd), static_cast<int>(s.total_max),
                     static_cast<int>(s.bbd_max));
    }
    static uint32_t win_seen = 0;
    uint32_t        wn;
    Win             wv;
    do
    {
        wn           = g_win_n;
        wv.cb        = g_win_pub.cb;
        wv.proc      = g_win_pub.proc;
        wv.deck[0]   = g_win_pub.deck[0];
        wv.deck[1]   = g_win_pub.deck[1];
        wv.mix       = g_win_pub.mix;
        wv.law       = g_win_pub.law;
        wv.cb_max    = g_win_pub.cb_max;
        for(int p = 0; p < 2; ++p)
            for(int s = 0; s < SEG_COUNT; ++s) wv.seg[p][s] = g_win_pub.seg[p][s];
    } while(wn != g_win_n);
    if(wn != 0 && wn != win_seen)
    {
        win_seen = wn;
        // Means over the window's 1000 blocks, plus its worst callback.
        // At most ~100 bytes.
        hw.PrintLine("CBP_WIN w=%d cb=%d cb_max=%d proc=%d a=%d b=%d mix=%d law=%d",
                     static_cast<int>(wn), static_cast<int>(wv.cb),
                     static_cast<int>(wv.cb_max), static_cast<int>(wv.proc),
                     static_cast<int>(wv.deck[0]), static_cast<int>(wv.deck[1]),
                     static_cast<int>(wv.mix), static_cast<int>(wv.law));
        // The same window, each deck split into modulation + control (m),
        // engine (e) and FX (f): am + ae + af = a, bm + be + bf = b, up to the
        // per-block rounding. At most ~80 bytes.
        hw.PrintLine("CBP_WIN2 w=%d am=%d ae=%d af=%d bm=%d be=%d bf=%d",
                     static_cast<int>(wn), static_cast<int>(wv.seg[0][SEG_MOD]),
                     static_cast<int>(wv.seg[0][SEG_ENG]), static_cast<int>(wv.seg[0][SEG_FX]),
                     static_cast<int>(wv.seg[1][SEG_MOD]), static_cast<int>(wv.seg[1][SEG_ENG]),
                     static_cast<int>(wv.seg[1][SEG_FX]));
    }
    if(++g_calls % 20u != 0u) return;
    const uint32_t n = g_n;
    if(n == 0) return;
    hw.PrintLine("CBP_SUM blocks=%d warm=%d cb_mean=%d cb_max=%d proc_mean=%d proc_max=%d maxs_max=%d",
                 static_cast<int>(g_block), static_cast<int>(n),
                 static_cast<int>(g_sum_cb / n), static_cast<int>(g_max_cb),
                 static_cast<int>(g_sum_proc / n), static_cast<int>(g_max_proc),
                 static_cast<int>(g_max_maxs));
    // Which patch ran: the stress patches differ from the init patch only in
    // an engine (control_tick.cpp, SHELL_BOOT_PATCH).
#if defined(SHELL_BOOT_PATCH)
    const int patch = SHELL_BOOT_PATCH;
#else
    const int patch = 0;
#endif
    hw.PrintLine("CBP_BOOT patch=%d eng_a=%d eng_b=%d", patch, g_eng[0], g_eng[1]);
    hw.PrintLine("CBP_SUM2 a_mean=%d b_mean=%d mix_mean=%d", static_cast<int>(g_sum_deck[0] / n),
                 static_cast<int>(g_sum_deck[1] / n), static_cast<int>(g_sum_mix / n));
    // Since boot, the deck split's means (CBP_WIN2's fields, as CBP_SUM2 is
    // CBP_WIN's).
    hw.PrintLine("CBP_SUM3 am=%d ae=%d af=%d bm=%d be=%d bf=%d",
                 static_cast<int>(g_sum_seg[0][SEG_MOD] / n), static_cast<int>(g_sum_seg[0][SEG_ENG] / n),
                 static_cast<int>(g_sum_seg[0][SEG_FX] / n), static_cast<int>(g_sum_seg[1][SEG_MOD] / n),
                 static_cast<int>(g_sum_seg[1][SEG_ENG] / n), static_cast<int>(g_sum_seg[1][SEG_FX] / n));
    for(int h = 0; h < 3; ++h)
    {
        const uint32_t* p = g_hist + h * 8;
        const int       k = (h == 2) ? kHist - 16 : 8;
        int v[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for(int i = 0; i < k; ++i) v[i] = static_cast<int>(p[i]);
        hw.PrintLine("CBP_HIST h=%d %d %d %d %d %d %d %d %d", h, v[0], v[1], v[2], v[3],
                     v[4], v[5], v[6], v[7]);
    }
}

} // namespace shell

#endif // SPKY_PROBE_MARKS
