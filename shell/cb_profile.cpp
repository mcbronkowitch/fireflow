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
// Each deck's process() split (fault R4), summed over the block: [0]
// modulation + control (the lanes, the raster tick, the fire; on the priority
// deck also the top-of-sample centre update, on the yielding deck the duck
// stage), [1] the engine, and the FX chain in five: [2] the target
// smoothers, [3] GRIT, [4] FLUX, [5] the tap, the FX mix and COMP, [6] the
// send law. All seven sum to g_deck; [2..6] sum to the FX chain.
enum {
    SEG_MOD = 0, SEG_ENG, SEG_SMOOTH, SEG_GRIT, SEG_FLUX, SEG_COMP, SEG_SEND, SEG_COUNT
};
constexpr int SEG_FX_FIRST = SEG_SMOOTH;
uint32_t g_seg[2][SEG_COUNT] = {};
// The mix split (fault R4, the reverb), summed over the block: [0] the lamp
// observers and the dry / deck taps, [1] the MORPH blend, the reverb's dry /
// wet smoothers and the duck, [2] AmbientReverb::process (empty while the
// room sleeps), [3] the return gain and the sleep check (and a clear when it
// lands), [4] the master limiter and the output store. The five sum to g_mix.
enum { MSEG_OBS = 0, MSEG_BLEND, MSEG_REV, MSEG_RET, MSEG_LIM, MSEG_COUNT };
uint32_t g_mseg[MSEG_COUNT] = {};
// Each deck's modulation + control half split (fault R4), summed over the
// block: [0] what the Instrument does before the deck's process() (on the
// priority deck the top-of-sample centre update and the reverb's clear step,
// on the yielding deck the duck / inhibit stage), [1] the lanes
// (SuperModulator::process), [2] the engine fade, the swap and the fire
// bookkeeping, [3] the control tick, [4] the fire trigger, the gate edge and
// the audio-in tap. The five sum to g_seg[p][SEG_MOD].
enum { DSEG_PRE = 0, DSEG_LANES, DSEG_FIRE, DSEG_TICK, DSEG_TRIG, DSEG_COUNT };
uint32_t g_dseg[2][DSEG_COUNT] = {};
// Each deck's lanes split (fault R4), summed over the block: [0] the pitch
// lane (per sample), [1] the step clock and the onset ring, [2] the texture
// lanes' raster tick and the rest of SuperModulator::process. The three sum
// to g_dseg[p][DSEG_LANES].
enum { LSEG_PITCH = 0, LSEG_RING, LSEG_TEX, LSEG_COUNT };
uint32_t g_lseg[2][LSEG_COUNT] = {};
// Each deck's pitch lane split (fault R4), summed over the block: [0] the
// call into SuperModulator::process, [1] ModLane::process's counters, phase
// advance and wrap events, [2] the step search and a step entry, [3] the
// slew, RANGE, the S&H latch and the output stores. The four sum to
// g_lseg[p][LSEG_PITCH].
enum { PSEG_CALL = 0, PSEG_HEAD, PSEG_SEARCH, PSEG_TAIL, PSEG_COUNT };
uint32_t g_pseg[2][PSEG_COUNT] = {};
// The block's most expensive sample, broken down (fault R4: it is sample 95
// of every block, ~33 k against a median of ~9 k): each deck's modulation
// half, lanes, control tick, engine and FX, the mix. WSEG_* index both.
enum { WSEG_MOD = 0, WSEG_LANES, WSEG_TICK, WSEG_ENG, WSEG_FX, WSEG_COUNT };
uint32_t g_wseg[2][WSEG_COUNT] = {};
uint32_t g_wmix  = 0;
bool     g_wlast = false;   // the worst sample was the block's last one

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
    uint32_t mseg[MSEG_COUNT];
    uint32_t dseg[2][DSEG_COUNT];
    uint32_t lseg[2][LSEG_COUNT];
    uint32_t pseg[2][PSEG_COUNT];
    uint32_t wseg[2][WSEG_COUNT];
    uint32_t wmix, wlast;
};
uint64_t          g_wsum_cb = 0, g_wsum_proc = 0, g_wsum_deck[2] = {0, 0}, g_wsum_mix = 0,
                  g_wsum_law = 0, g_wsum_seg[2][SEG_COUNT] = {}, g_wsum_mseg[MSEG_COUNT] = {},
                  g_wsum_dseg[2][DSEG_COUNT] = {}, g_wsum_lseg[2][LSEG_COUNT] = {},
                  g_wsum_wseg[2][WSEG_COUNT] = {}, g_wsum_wmix = 0,
                  g_wsum_pseg[2][PSEG_COUNT] = {};
uint32_t          g_wsum_wlast = 0;
uint64_t          g_sum_seg[2][SEG_COUNT] = {}, g_sum_mseg[MSEG_COUNT] = {},
                  g_sum_dseg[2][DSEG_COUNT] = {}, g_sum_lseg[2][LSEG_COUNT] = {},
                  g_sum_pseg[2][PSEG_COUNT] = {};
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
                 const ProbeMixSlots* ms, int n, int pri)
{
    g_proc = ts[n] - ts[0];
    uint32_t a = 0, b = 0, m = 0;
    uint32_t sa[SEG_COUNT] = {}, sb[SEG_COUNT] = {}, sm[MSEG_COUNT] = {};
    uint32_t da[DSEG_COUNT] = {}, db[DSEG_COUNT] = {};
    uint32_t la[LSEG_COUNT] = {}, lb[LSEG_COUNT] = {};
    uint32_t qa[PSEG_COUNT] = {}, qb[PSEG_COUNT] = {};
    for(int i = 0; i < n; ++i)
    {
        a += d1[i] - ts[i];        // top of the sample + the priority deck
        b += d2[i] - d1[i];        // the yielding deck
        m += ts[i + 1] - d2[i];    // mix, reverb, limiter
        // Slot k of a deck ends segment k; segment 0 starts at the deck's
        // start, the last one ends at the deck's end stamp.
        sa[SEG_MOD] += p1[i][0] - ts[i];
        sb[SEG_MOD] += p2[i][0] - d1[i];
        for(int s = 1; s < SEG_COUNT - 1; ++s)
        {
            sa[s] += p1[i][s] - p1[i][s - 1];
            sb[s] += p2[i][s] - p2[i][s - 1];
        }
        sa[SEG_SEND] += d1[i] - p1[i][SEG_COUNT - 2];
        sb[SEG_SEND] += d2[i] - p2[i][SEG_COUNT - 2];
        // The mix the same way: slot k ends segment k, the limiter ends at
        // the next sample's top stamp.
        sm[MSEG_OBS] += ms[i][0] - d2[i];
        for(int s = 1; s < MSEG_COUNT - 1; ++s) sm[s] += ms[i][s] - ms[i][s - 1];
        sm[MSEG_LIM] += ts[i + 1] - ms[i][MSEG_COUNT - 2];
        // The modulation half: slots 6..9 come before slot 0 (probe_mark.h).
        da[DSEG_PRE]   += p1[i][6] - ts[i];
        db[DSEG_PRE]   += p2[i][6] - d1[i];
        da[DSEG_LANES] += p1[i][7] - p1[i][6];
        db[DSEG_LANES] += p2[i][7] - p2[i][6];
        da[DSEG_FIRE]  += p1[i][8] - p1[i][7];
        db[DSEG_FIRE]  += p2[i][8] - p2[i][7];
        da[DSEG_TICK]  += p1[i][9] - p1[i][8];
        db[DSEG_TICK]  += p2[i][9] - p2[i][8];
        da[DSEG_TRIG]  += p1[i][0] - p1[i][9];
        db[DSEG_TRIG]  += p2[i][0] - p2[i][9];
        // The lanes: 6 -> 10 -> 11 -> 7 (probe_mark.h).
        la[LSEG_PITCH] += p1[i][10] - p1[i][6];
        lb[LSEG_PITCH] += p2[i][10] - p2[i][6];
        la[LSEG_RING]  += p1[i][11] - p1[i][10];
        lb[LSEG_RING]  += p2[i][11] - p2[i][10];
        la[LSEG_TEX]   += p1[i][7] - p1[i][11];
        lb[LSEG_TEX]   += p2[i][7] - p2[i][11];
        // The pitch lane: 6 -> 12 -> 13 -> 14 -> 10 (probe_mark.h).
        qa[PSEG_CALL]   += p1[i][12] - p1[i][6];
        qb[PSEG_CALL]   += p2[i][12] - p2[i][6];
        qa[PSEG_HEAD]   += p1[i][13] - p1[i][12];
        qb[PSEG_HEAD]   += p2[i][13] - p2[i][12];
        qa[PSEG_SEARCH] += p1[i][14] - p1[i][13];
        qb[PSEG_SEARCH] += p2[i][14] - p2[i][13];
        qa[PSEG_TAIL]   += p1[i][10] - p1[i][14];
        qb[PSEG_TAIL]   += p2[i][10] - p2[i][14];
    }
    for(int s = 0; s < PSEG_COUNT; ++s)
    {
        g_pseg[pri][s]     = qa[s];
        g_pseg[1 - pri][s] = qb[s];
    }
    for(int s = 0; s < DSEG_COUNT; ++s)
    {
        g_dseg[pri][s]     = da[s];
        g_dseg[1 - pri][s] = db[s];
    }
    for(int s = 0; s < LSEG_COUNT; ++s)
    {
        g_lseg[pri][s]     = la[s];
        g_lseg[1 - pri][s] = lb[s];
    }
    for(int s = 0; s < MSEG_COUNT; ++s) g_mseg[s] = sm[s];
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
    // The worst sample's breakdown. The priority deck starts at the sample's
    // top stamp, the yielding one at the priority deck's end stamp.
    {
        const uint32_t st[2] = {ts[mi], d1[mi]}, en[2] = {d1[mi], d2[mi]};
        const uint32_t* ps[2] = {p1[mi], p2[mi]};
        for(int k = 0; k < 2; ++k)
        {
            const int p = k == 0 ? pri : 1 - pri;
            g_wseg[p][WSEG_MOD]   = ps[k][0] - st[k];
            g_wseg[p][WSEG_LANES] = ps[k][7] - ps[k][6];
            g_wseg[p][WSEG_TICK]  = ps[k][9] - ps[k][8];
            g_wseg[p][WSEG_ENG]   = ps[k][1] - ps[k][0];
            g_wseg[p][WSEG_FX]    = en[k] - ps[k][1];
        }
        g_wmix  = ts[mi + 1] - d2[mi];
        g_wlast = mi == n - 1;
    }
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
    for(int s = 0; s < MSEG_COUNT; ++s)
    {
        g_wsum_mseg[s] += g_mseg[s];
        g_sum_mseg[s] += g_mseg[s];
    }
    for(int p = 0; p < 2; ++p)
        for(int s = 0; s < DSEG_COUNT; ++s)
        {
            g_wsum_dseg[p][s] += g_dseg[p][s];
            g_sum_dseg[p][s] += g_dseg[p][s];
        }
    for(int p = 0; p < 2; ++p)
    {
        for(int s = 0; s < LSEG_COUNT; ++s)
        {
            g_wsum_lseg[p][s] += g_lseg[p][s];
            g_sum_lseg[p][s] += g_lseg[p][s];
        }
        for(int s = 0; s < PSEG_COUNT; ++s)
        {
            g_wsum_pseg[p][s] += g_pseg[p][s];
            g_sum_pseg[p][s] += g_pseg[p][s];
        }
        for(int s = 0; s < WSEG_COUNT; ++s) g_wsum_wseg[p][s] += g_wseg[p][s];
    }
    g_wsum_wmix += g_wmix;
    if(g_wlast) ++g_wsum_wlast;
    if(cb > g_wmax_cb) g_wmax_cb = cb;
    if(++g_wn == kWinBlocks)
    {
        for(int p = 0; p < 2; ++p)
            for(int s = 0; s < SEG_COUNT; ++s)
            {
                g_win_pub.seg[p][s] = static_cast<uint32_t>(g_wsum_seg[p][s] / kWinBlocks);
                g_wsum_seg[p][s]    = 0;
            }
        for(int s = 0; s < MSEG_COUNT; ++s)
        {
            g_win_pub.mseg[s] = static_cast<uint32_t>(g_wsum_mseg[s] / kWinBlocks);
            g_wsum_mseg[s]    = 0;
        }
        for(int p = 0; p < 2; ++p)
            for(int s = 0; s < DSEG_COUNT; ++s)
            {
                g_win_pub.dseg[p][s] = static_cast<uint32_t>(g_wsum_dseg[p][s] / kWinBlocks);
                g_wsum_dseg[p][s]    = 0;
            }
        for(int p = 0; p < 2; ++p)
        {
            for(int s = 0; s < LSEG_COUNT; ++s)
            {
                g_win_pub.lseg[p][s] = static_cast<uint32_t>(g_wsum_lseg[p][s] / kWinBlocks);
                g_wsum_lseg[p][s]    = 0;
            }
            for(int s = 0; s < PSEG_COUNT; ++s)
            {
                g_win_pub.pseg[p][s] = static_cast<uint32_t>(g_wsum_pseg[p][s] / kWinBlocks);
                g_wsum_pseg[p][s]    = 0;
            }
            for(int s = 0; s < WSEG_COUNT; ++s)
            {
                g_win_pub.wseg[p][s] = static_cast<uint32_t>(g_wsum_wseg[p][s] / kWinBlocks);
                g_wsum_wseg[p][s]    = 0;
            }
        }
        g_win_pub.wmix  = static_cast<uint32_t>(g_wsum_wmix / kWinBlocks);
        g_win_pub.wlast = g_wsum_wlast;
        g_wsum_wmix     = 0;
        g_wsum_wlast    = 0;
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
    // The sleeping reverb's spread clear (fault R4): n counts clear steps
    // (32 per sleep), last / max their cycles. Printed when n has moved since
    // the last report, copied first, as CBP_SWAP above.
    static uint32_t clears_seen = 0;
    if(spky::probe_rev_clear.n != clears_seen)
    {
        const spky::ProbeRevClear c = spky::probe_rev_clear;
        clears_seen                 = c.n;
        hw.PrintLine("CBP_RVCLR n=%d last=%d max=%d", static_cast<int>(c.n),
                     static_cast<int>(c.last), static_cast<int>(c.max));
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
        for(int s = 0; s < MSEG_COUNT; ++s) wv.mseg[s] = g_win_pub.mseg[s];
        for(int p = 0; p < 2; ++p)
            for(int s = 0; s < DSEG_COUNT; ++s) wv.dseg[p][s] = g_win_pub.dseg[p][s];
        for(int p = 0; p < 2; ++p)
        {
            for(int s = 0; s < LSEG_COUNT; ++s) wv.lseg[p][s] = g_win_pub.lseg[p][s];
            for(int s = 0; s < PSEG_COUNT; ++s) wv.pseg[p][s] = g_win_pub.pseg[p][s];
            for(int s = 0; s < WSEG_COUNT; ++s) wv.wseg[p][s] = g_win_pub.wseg[p][s];
        }
        wv.wmix  = g_win_pub.wmix;
        wv.wlast = g_win_pub.wlast;
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
        uint32_t fx[2] = {0, 0};
        for(int p = 0; p < 2; ++p)
            for(int s = SEG_FX_FIRST; s < SEG_COUNT; ++s) fx[p] += wv.seg[p][s];
        hw.PrintLine("CBP_WIN2 w=%d am=%d ae=%d af=%d bm=%d be=%d bf=%d",
                     static_cast<int>(wn), static_cast<int>(wv.seg[0][SEG_MOD]),
                     static_cast<int>(wv.seg[0][SEG_ENG]), static_cast<int>(fx[0]),
                     static_cast<int>(wv.seg[1][SEG_MOD]), static_cast<int>(wv.seg[1][SEG_ENG]),
                     static_cast<int>(fx[1]));
        // The FX chain of each deck in five: smoothers (s), GRIT (g), FLUX
        // (x), tap + FX mix + COMP (c), send law (n); the five sum to af / bf.
        // At most ~100 bytes.
        hw.PrintLine("CBP_WIN3 w=%d as=%d ag=%d ax=%d ac=%d an=%d bs=%d bg=%d bx=%d bc=%d bn=%d",
                     static_cast<int>(wn), static_cast<int>(wv.seg[0][SEG_SMOOTH]),
                     static_cast<int>(wv.seg[0][SEG_GRIT]), static_cast<int>(wv.seg[0][SEG_FLUX]),
                     static_cast<int>(wv.seg[0][SEG_COMP]), static_cast<int>(wv.seg[0][SEG_SEND]),
                     static_cast<int>(wv.seg[1][SEG_SMOOTH]), static_cast<int>(wv.seg[1][SEG_GRIT]),
                     static_cast<int>(wv.seg[1][SEG_FLUX]), static_cast<int>(wv.seg[1][SEG_COMP]),
                     static_cast<int>(wv.seg[1][SEG_SEND]));
        // The mix in five: observers + taps (o), blend + dry / wet smoothers
        // + duck (d), reverb (r), return + sleep (t), limiter (l); the five
        // sum to mix. At most ~70 bytes.
        hw.PrintLine("CBP_WIN4 w=%d xo=%d xd=%d xr=%d xt=%d xl=%d",
                     static_cast<int>(wn), static_cast<int>(wv.mseg[MSEG_OBS]),
                     static_cast<int>(wv.mseg[MSEG_BLEND]), static_cast<int>(wv.mseg[MSEG_REV]),
                     static_cast<int>(wv.mseg[MSEG_RET]), static_cast<int>(wv.mseg[MSEG_LIM]));
        // Each deck's modulation + control half in five: before the deck (p),
        // lanes (l), fade / swap / fire bookkeeping (i), control tick (t),
        // trigger + gate edge + tap (r); the five sum to am / bm. At most
        // ~100 bytes.
        hw.PrintLine("CBP_WIN5 w=%d ap=%d al=%d ai=%d at=%d ar=%d bp=%d bl=%d bi=%d bt=%d br=%d",
                     static_cast<int>(wn), static_cast<int>(wv.dseg[0][DSEG_PRE]),
                     static_cast<int>(wv.dseg[0][DSEG_LANES]), static_cast<int>(wv.dseg[0][DSEG_FIRE]),
                     static_cast<int>(wv.dseg[0][DSEG_TICK]), static_cast<int>(wv.dseg[0][DSEG_TRIG]),
                     static_cast<int>(wv.dseg[1][DSEG_PRE]), static_cast<int>(wv.dseg[1][DSEG_LANES]),
                     static_cast<int>(wv.dseg[1][DSEG_FIRE]), static_cast<int>(wv.dseg[1][DSEG_TICK]),
                     static_cast<int>(wv.dseg[1][DSEG_TRIG]));
        // Each deck's lanes in three: pitch lane (q), step clock + onset ring
        // (o), texture tick (u); the three sum to al / bl. At most ~70 bytes.
        hw.PrintLine("CBP_WIN6 w=%d aq=%d ao=%d au=%d bq=%d bo=%d bu=%d",
                     static_cast<int>(wn), static_cast<int>(wv.lseg[0][LSEG_PITCH]),
                     static_cast<int>(wv.lseg[0][LSEG_RING]), static_cast<int>(wv.lseg[0][LSEG_TEX]),
                     static_cast<int>(wv.lseg[1][LSEG_PITCH]), static_cast<int>(wv.lseg[1][LSEG_RING]),
                     static_cast<int>(wv.lseg[1][LSEG_TEX]));
        // Each deck's pitch lane in four: the call in (qc), counters + phase
        // advance + wrap (qh), step search (qs), slew + RANGE + latch + stores
        // (qt); the four sum to aq / bq. At most ~100 bytes.
        hw.PrintLine("CBP_WIN7 w=%d aqc=%d aqh=%d aqs=%d aqt=%d bqc=%d bqh=%d bqs=%d bqt=%d",
                     static_cast<int>(wn), static_cast<int>(wv.pseg[0][PSEG_CALL]),
                     static_cast<int>(wv.pseg[0][PSEG_HEAD]), static_cast<int>(wv.pseg[0][PSEG_SEARCH]),
                     static_cast<int>(wv.pseg[0][PSEG_TAIL]), static_cast<int>(wv.pseg[1][PSEG_CALL]),
                     static_cast<int>(wv.pseg[1][PSEG_HEAD]), static_cast<int>(wv.pseg[1][PSEG_SEARCH]),
                     static_cast<int>(wv.pseg[1][PSEG_TAIL]));
        // Each block's worst sample, broken down and averaged over the window:
        // per deck modulation half (m), its lanes (l) and control tick (t),
        // engine (e), FX (f); the mix (x); and in how many of the window's
        // blocks the worst sample was the last one (last). At most ~110 bytes.
        hw.PrintLine("CBP_WORST w=%d last=%d am=%d al=%d at=%d ae=%d af=%d bm=%d bl=%d bt=%d be=%d bf=%d x=%d",
                     static_cast<int>(wn), static_cast<int>(wv.wlast),
                     static_cast<int>(wv.wseg[0][WSEG_MOD]), static_cast<int>(wv.wseg[0][WSEG_LANES]),
                     static_cast<int>(wv.wseg[0][WSEG_TICK]), static_cast<int>(wv.wseg[0][WSEG_ENG]),
                     static_cast<int>(wv.wseg[0][WSEG_FX]), static_cast<int>(wv.wseg[1][WSEG_MOD]),
                     static_cast<int>(wv.wseg[1][WSEG_LANES]), static_cast<int>(wv.wseg[1][WSEG_TICK]),
                     static_cast<int>(wv.wseg[1][WSEG_ENG]), static_cast<int>(wv.wseg[1][WSEG_FX]),
                     static_cast<int>(wv.wmix));
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
    // Since boot, the deck split's means (CBP_WIN2's and CBP_WIN3's fields,
    // as CBP_SUM2 is CBP_WIN's).
    uint64_t fxs[2] = {0, 0};
    for(int p = 0; p < 2; ++p)
        for(int s = SEG_FX_FIRST; s < SEG_COUNT; ++s) fxs[p] += g_sum_seg[p][s];
    hw.PrintLine("CBP_SUM3 am=%d ae=%d af=%d bm=%d be=%d bf=%d",
                 static_cast<int>(g_sum_seg[0][SEG_MOD] / n), static_cast<int>(g_sum_seg[0][SEG_ENG] / n),
                 static_cast<int>(fxs[0] / n), static_cast<int>(g_sum_seg[1][SEG_MOD] / n),
                 static_cast<int>(g_sum_seg[1][SEG_ENG] / n), static_cast<int>(fxs[1] / n));
    hw.PrintLine("CBP_SUM4 as=%d ag=%d ax=%d ac=%d an=%d bs=%d bg=%d bx=%d bc=%d bn=%d",
                 static_cast<int>(g_sum_seg[0][SEG_SMOOTH] / n), static_cast<int>(g_sum_seg[0][SEG_GRIT] / n),
                 static_cast<int>(g_sum_seg[0][SEG_FLUX] / n), static_cast<int>(g_sum_seg[0][SEG_COMP] / n),
                 static_cast<int>(g_sum_seg[0][SEG_SEND] / n), static_cast<int>(g_sum_seg[1][SEG_SMOOTH] / n),
                 static_cast<int>(g_sum_seg[1][SEG_GRIT] / n), static_cast<int>(g_sum_seg[1][SEG_FLUX] / n),
                 static_cast<int>(g_sum_seg[1][SEG_COMP] / n), static_cast<int>(g_sum_seg[1][SEG_SEND] / n));
    hw.PrintLine("CBP_SUM5 xo=%d xd=%d xr=%d xt=%d xl=%d",
                 static_cast<int>(g_sum_mseg[MSEG_OBS] / n), static_cast<int>(g_sum_mseg[MSEG_BLEND] / n),
                 static_cast<int>(g_sum_mseg[MSEG_REV] / n), static_cast<int>(g_sum_mseg[MSEG_RET] / n),
                 static_cast<int>(g_sum_mseg[MSEG_LIM] / n));
    hw.PrintLine("CBP_SUM6 ap=%d al=%d ai=%d at=%d ar=%d bp=%d bl=%d bi=%d bt=%d br=%d",
                 static_cast<int>(g_sum_dseg[0][DSEG_PRE] / n), static_cast<int>(g_sum_dseg[0][DSEG_LANES] / n),
                 static_cast<int>(g_sum_dseg[0][DSEG_FIRE] / n), static_cast<int>(g_sum_dseg[0][DSEG_TICK] / n),
                 static_cast<int>(g_sum_dseg[0][DSEG_TRIG] / n), static_cast<int>(g_sum_dseg[1][DSEG_PRE] / n),
                 static_cast<int>(g_sum_dseg[1][DSEG_LANES] / n), static_cast<int>(g_sum_dseg[1][DSEG_FIRE] / n),
                 static_cast<int>(g_sum_dseg[1][DSEG_TICK] / n), static_cast<int>(g_sum_dseg[1][DSEG_TRIG] / n));
    hw.PrintLine("CBP_SUM7 aq=%d ao=%d au=%d bq=%d bo=%d bu=%d",
                 static_cast<int>(g_sum_lseg[0][LSEG_PITCH] / n), static_cast<int>(g_sum_lseg[0][LSEG_RING] / n),
                 static_cast<int>(g_sum_lseg[0][LSEG_TEX] / n), static_cast<int>(g_sum_lseg[1][LSEG_PITCH] / n),
                 static_cast<int>(g_sum_lseg[1][LSEG_RING] / n), static_cast<int>(g_sum_lseg[1][LSEG_TEX] / n));
    hw.PrintLine("CBP_SUM8 aqc=%d aqh=%d aqs=%d aqt=%d bqc=%d bqh=%d bqs=%d bqt=%d",
                 static_cast<int>(g_sum_pseg[0][PSEG_CALL] / n), static_cast<int>(g_sum_pseg[0][PSEG_HEAD] / n),
                 static_cast<int>(g_sum_pseg[0][PSEG_SEARCH] / n), static_cast<int>(g_sum_pseg[0][PSEG_TAIL] / n),
                 static_cast<int>(g_sum_pseg[1][PSEG_CALL] / n), static_cast<int>(g_sum_pseg[1][PSEG_HEAD] / n),
                 static_cast<int>(g_sum_pseg[1][PSEG_SEARCH] / n), static_cast<int>(g_sum_pseg[1][PSEG_TAIL] / n));
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
