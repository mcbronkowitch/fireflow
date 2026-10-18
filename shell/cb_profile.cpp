#include "cb_profile.h"

#if defined(SPKY_PROBE_MARKS)

#include <atomic>
#include "cycles.h"
#include "util/probe_mark.h"

// The engine's stamp clock and its once-per-block hand-over
// (engine/util/probe_mark.h).
namespace spky {
volatile uint32_t* probe_clock = &DWT->CYCCNT;
} // namespace spky

namespace {

// This block's process() figures, written by probe_block() inside process()
// and read by cb_profile_block_end() after it, both in the audio interrupt.
uint32_t g_proc  = 0;    // process(), first stamp to last
uint32_t g_maxs  = 0;    // the most expensive single sample
int      g_maxi  = 0;    // its index in the block
uint32_t g_med   = 0;    // the mean of the other samples of this block

uint32_t g_block = 0;    // blocks since StartAudio

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
};
constexpr int     kRing = 64;
Rec               g_ring[kRing];
volatile uint32_t g_ring_w = 0;   // entries written, ever
uint32_t          g_ring_r = 0;   // foreground: entries printed

// Summary over warm blocks, and the histogram of the whole callback:
// 10 000-cycle bins from 850 000, bin 0 below, the last at 1 060 000 and up.
uint64_t           g_sum_cb = 0, g_sum_proc = 0;
uint32_t           g_max_cb = 0, g_max_proc = 0, g_max_maxs = 0;
volatile uint32_t  g_n = 0;
constexpr uint32_t kHistLo = 850000u, kHistStep = 10000u;
constexpr int      kHist   = 23;
uint32_t           g_hist[kHist];

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
void probe_block(const uint32_t* ts, int n)
{
    g_proc = ts[n] - ts[0];
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
    if(!warm) return;
    g_sum_cb += cb;
    g_sum_proc += g_proc;
    if(cb > g_max_cb) g_max_cb = cb;
    if(g_proc > g_max_proc) g_max_proc = g_proc;
    if(g_maxs > g_max_maxs) g_max_maxs = g_maxs;
    g_n = g_n + 1u;
    int b = cb < kHistLo ? 0 : 1 + static_cast<int>((cb - kHistLo) / kHistStep);
    if(b > kHist - 1) b = kHist - 1;
    ++g_hist[b];
    const uint32_t wrap_mask = 0x1Fu | (0x1Fu << 10);
    if(cb > kHot || (ev & wrap_mask) != 0u)
    {
        Rec& r = g_ring[g_ring_w % kRing];
        r = Rec{blk, cb, g_proc, law, led, g_maxs, g_med, ev, g_maxi};
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
    }
    if(++g_calls % 20u != 0u) return;
    const uint32_t n = g_n;
    if(n == 0) return;
    hw.PrintLine("CBP_SUM blocks=%d warm=%d cb_mean=%d cb_max=%d proc_mean=%d proc_max=%d maxs_max=%d",
                 static_cast<int>(g_block), static_cast<int>(n),
                 static_cast<int>(g_sum_cb / n), static_cast<int>(g_max_cb),
                 static_cast<int>(g_sum_proc / n), static_cast<int>(g_max_proc),
                 static_cast<int>(g_max_maxs));
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
