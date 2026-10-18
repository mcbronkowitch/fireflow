#include "cb_profile.h"

#if defined(SPKY_PROBE_MARKS)

#include <atomic>
#include "cycles.h"
#include "util/probe_mark.h"

namespace {

using spky::PSEG_COUNT;

// Written by the audio interrupt only. The foreground reads the ring and the
// summaries; a torn read is cosmetic (this is a log for a human).
uint32_t g_acc[PSEG_COUNT];
int      g_cur    = spky::PSEG_IDLE;
uint32_t g_t_last = 0;
uint32_t g_ev     = 0;

uint32_t g_block = 0;            // blocks since StartAudio
uint32_t g_mark_cost = 0;        // cycles per mark, measured at init

// A block whose callback took more than kHot cycles.
constexpr uint32_t kHot = 930000u;
struct Hot {
    uint32_t blk, cb, ev;
    uint32_t seg[PSEG_COUNT];
};
constexpr int     kRing = 32;
Hot               g_ring[kRing];
volatile uint32_t g_ring_w = 0;  // entries written, ever
uint32_t          g_ring_r = 0;  // foreground: entries printed

// Per-segment sum (mean = sum / n) and max over warm blocks, plus the
// histogram of the whole callback: 10 000-cycle bins from 850 000, bin 0
// below, the last bin at and above 1 060 000.
uint64_t          g_sum[PSEG_COUNT];
uint32_t          g_max[PSEG_COUNT];
volatile uint32_t g_n = 0;
constexpr uint32_t kHistLo = 850000u, kHistStep = 10000u;
constexpr int      kHist   = 23;
uint32_t           g_hist[kHist];

} // namespace

namespace spky {
void probe_mark(int seg)
{
    const uint32_t t = DWT->CYCCNT;
    g_acc[g_cur] += t - g_t_last;
    g_t_last = t;
    g_cur    = seg;
}
void probe_event(int bit) { g_ev |= 1u << bit; }
} // namespace spky

namespace shell {

void cb_profile_init()
{
    cycles_init();
    g_t_last = cycles_now();
    const uint32_t t0 = cycles_now();
    for(int i = 0; i < 1000; ++i) spky::probe_mark(spky::PSEG_IDLE);
    g_mark_cost = (cycles_now() - t0) / 1000u;
    for(auto& a : g_acc) a = 0;
}

void cb_profile_block_begin()
{
    for(auto& a : g_acc) a = 0;
    g_ev     = 0;
    g_t_last = cycles_now();
    g_cur    = spky::PSEG_SCAN;
}

void cb_profile_block_end(uint32_t cb, bool warm)
{
    spky::probe_mark(spky::PSEG_IDLE);
    const uint32_t blk = g_block++;
    if(!warm) return;
    for(int s = 0; s < PSEG_COUNT; ++s)
    {
        g_sum[s] += g_acc[s];
        if(g_acc[s] > g_max[s]) g_max[s] = g_acc[s];
    }
    g_n = g_n + 1u;
    int b = cb < kHistLo ? 0 : 1 + static_cast<int>((cb - kHistLo) / kHistStep);
    if(b > kHist - 1) b = kHist - 1;
    ++g_hist[b];
    if(cb > kHot)
    {
        Hot& h = g_ring[g_ring_w % kRing];
        h.blk = blk;
        h.cb  = cb;
        h.ev  = g_ev;
        for(int s = 0; s < PSEG_COUNT; ++s) h.seg[s] = g_acc[s];
        std::atomic_signal_fence(std::memory_order_seq_cst);
        g_ring_w = g_ring_w + 1u;
    }
}

namespace {
// Nine values per line: 18 segments in two lines, each under libDaisy's
// 128-byte log buffer ("CBP_xxx n=9999999 h=1" plus nine " 9999999").
void print_segs(bench::Board& hw, const char* tag, uint32_t n, const uint32_t* v)
{
    for(int h = 0; h < 2; ++h)
    {
        const uint32_t* p = v + h * 9;
        hw.PrintLine("%s n=%d h=%d %d %d %d %d %d %d %d %d %d", tag,
                     static_cast<int>(n), h,
                     static_cast<int>(p[0]), static_cast<int>(p[1]),
                     static_cast<int>(p[2]), static_cast<int>(p[3]),
                     static_cast<int>(p[4]), static_cast<int>(p[5]),
                     static_cast<int>(p[6]), static_cast<int>(p[7]),
                     static_cast<int>(p[8]));
    }
}
uint32_t g_calls = 0;
} // namespace

void cb_profile_report(bench::Board& hw)
{
    static_assert(PSEG_COUNT == 18, "print_segs prints exactly 18 segments");
    std::atomic_signal_fence(std::memory_order_seq_cst);
    const uint32_t w = g_ring_w;
    if(w - g_ring_r > static_cast<uint32_t>(kRing))
    {
        hw.PrintLine("CBP_LOST %d", static_cast<int>(w - g_ring_r - kRing));
        g_ring_r = w - kRing;
    }
    for(; g_ring_r != w; ++g_ring_r)
    {
        const Hot h = g_ring[g_ring_r % kRing];
        hw.PrintLine("CBP_HOT n=%d blk=%d cb=%d ev=%08x", static_cast<int>(g_ring_r),
                     static_cast<int>(h.blk), static_cast<int>(h.cb),
                     static_cast<unsigned>(h.ev));
        print_segs(hw, "CBP_HSEG", g_ring_r, h.seg);
    }
    if(++g_calls % 20u != 0u) return;
    const uint32_t n = g_n;
    if(n == 0) return;
    uint32_t mean[PSEG_COUNT];
    for(int s = 0; s < PSEG_COUNT; ++s) mean[s] = static_cast<uint32_t>(g_sum[s] / n);
    hw.PrintLine("CBP_INFO blocks=%d warm=%d mark_cost=%d hot=%d", static_cast<int>(g_block),
                 static_cast<int>(n), static_cast<int>(g_mark_cost), static_cast<int>(w));
    print_segs(hw, "CBP_MEAN", n, mean);
    print_segs(hw, "CBP_MAX", n, g_max);
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
