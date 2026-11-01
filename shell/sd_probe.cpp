#include "sd_probe.h"

#include <cstdio>
#include <cstring>

#include "cycles.h"
#include "per/sdmmc.h"
#include "sd_schedule.h"
#include "shell_sd_probe.h"
#include "sys/fatfs.h"

// The two latency-tail modes (spec 2026-10-31-sd-use-cases-tests-design.md
// section 5, C1 rtail and C2 wtail) share one pass and one set of constants.
#if SHELL_SD_PROBE_RTAIL || SHELL_SD_PROBE_WTAIL
#define SD_PROBE_TAIL 1
#else
#define SD_PROBE_TAIL 0
#endif
// The modes that mark their transfers for the pot hold (g_sd_busy; the
// callback side, sd_hold_block()). In the tail modes the hold runs only when
// kTailBurst says the transfers are A1's bursts.
#if SHELL_SD_PROBE_BURST || SD_PROBE_TAIL
#define SD_PROBE_HOLD 1
#else
#define SD_PROBE_HOLD 0
#endif

namespace shell {

// The foreground's mark on every card transfer (sd_probe.h). Outside the
// anonymous namespace: panel_scan.cpp's callback reads it through sd_hold_block().
volatile bool g_sd_seen = false;

namespace {

// 32 KB per f_read / f_write: 64 sectors, one DMA transfer each when the
// clusters run on; 8 MiB is a sample of ~44 s mono at 48 kHz in float.
constexpr uint32_t kChunk      = 32u * 1024u;
constexpr uint32_t kFileBytes  = 8u * 1024u * 1024u;
constexpr uint32_t kPassMs     = 500;   // the report loop's Delay(500)
constexpr uint32_t kPhaseWin   = 10;    // raw windows per stream phase
constexpr uint32_t kReportEvery = 10;   // passes between the SHELL_SD lines

// libDaisy's default, 50 MHz. The fallback for a socket that does not take it
// is Speed::STANDARD (25 MHz); kMhz is only what the line prints.
constexpr daisy::SdmmcHandler::Speed kSpeed = daisy::SdmmcHandler::Speed::FAST;
constexpr int                        kMhz   = 50;
// The bus clock stops while no transfer runs (SDMMC CLKCR.PWRSAV). With it
// running (libDaisy's default), the first board run (ef52d8b, 2026-10-31)
// measured SENSE_2/3's idle raw spread at ~95 counts, against 14 on
// 2026-10-24 before the card was fitted.
constexpr bool kClockPowerSave = true;

// .DAT, not .BIN: the Daisy bootloader scans the card's root for a .bin to
// start, and an 8 MiB pattern file is one it must never find. The name an
// earlier bring-up image used, FFSDTEST.BIN, is deleted on mount.
const char kName[]    = "FFSDTEST.DAT";
const char kOldName[] = "FFSDTEST.BIN";
// The root line's names list: whole 8.3 names while they fit, so the line
// stays inside the 128-byte log buffer (prefix 46 + names 72 at worst).
constexpr size_t kRootNamesMax = 72;

// The FatFs objects in one 32-byte-aligned block in AXI SRAM (.bss). FATFS's
// sector window and FIL's sector buffer are DMA targets, and sd_diskio.c
// invalidates the D-cache over the transfer rounded out to 32-byte lines: a
// neighbour the audio interrupt wrote during the transfer would lose that
// write. alignas makes the block start and end on a line.
struct alignas(32) FsBlock
{
    daisy::FatFSInterface fsi;
    FIL                   file;
};
FsBlock             g_fs;
daisy::SdmmcHandler g_sdmmc;

// The transfer buffer in SDRAM, where the granular element's sample will be
// read to (spec 2026-10-30-cpu-engine-kernels-design.md section 1).
alignas(32) uint32_t DSY_SDRAM_BSS g_buf[kChunk / 4];

#if SHELL_SD_PROBE_STREAM
// The second transfer buffer, in AXI SRAM (.bss), for test A3: does the same
// traffic cost the audio callback more through SDRAM, which the sampler, echo
// and BBD read from, or through AXI? alignas(32) and a size that is a multiple
// of 32 keep the driver's D-cache invalidate (sd_diskio.c, over the transfer
// rounded out to 32-byte lines) on this buffer's own lines, as for FsBlock.
//
// 16 KB, not the 32 KB of the SDRAM buffer: AXI SRAM had 30 088 B free in this
// image, and a 32 KB buffer overflowed it by 2 680 B. The axi on-phases
// therefore read 16 KB chunks. So that location and chunk size are not
// confounded, the stream rotates three arms (moderator correction 2026-10-31):
// sdram-16 (g_buf, 16 KB), axi-16 (g_buf_axi, 16 KB) and sdram-32 (g_buf,
// 32 KB, the 11.2 pattern). The SHELL_SD_S line names the arm with buf= and
// chunk=.
constexpr uint32_t kChunkAxi = 16u * 1024u;
alignas(32) uint32_t g_buf_axi[kChunkAxi / 4];
static_assert(sizeof(g_buf_axi) % 32 == 0, "the invalidate rounds out to 32-byte lines");
#endif
// Both chunks divide the file, so a pass that starts on a chunk boundary of its
// own size wraps exactly at the end. The positions the arms leave behind
// differ (a 16 KB phase can stop at an odd multiple of 16 KB, which is not a
// 32 KB boundary): stream_pass() rounds the position down to its chunk at the
// start of every pass.
static_assert(kFileBytes % kChunk == 0, "the sdram chunk divides the file");
#if SHELL_SD_PROBE_STREAM
static_assert(kFileBytes % kChunkAxi == 0, "the axi chunk divides the file");
#endif

struct Step
{
    int      fr       = -1;   // FRESULT, -1: not run
    uint32_t bytes    = 0;
    uint32_t us       = 0;    // inside the f_read / f_write calls only
    uint32_t worst_us = 0;    // the worst single call
};

enum Check { kSkip, kNone, kOk, kBad };

struct Result
{
    int      mount_fr  = -1;
    int      fs_type   = 0;   // FS_FAT12 1, FS_FAT16 2, FS_FAT32 3, FS_EXFAT 4
    uint32_t total_mib = 0;
    uint32_t free_mib  = 0;
    bool     root_seen = false;   // the listing ran
    uint32_t root_n    = 0;       // entries in the root
    uint32_t root_bin  = 0;       // files there ending in .bin, any case
    char     root_names[kRootNamesMax + 1] = "";
    Check    prev      = kSkip;
    Step     prev_read;
    uint32_t prev_off  = 0;   // first bad byte, or the file's size if it is wrong
    Step     write;
    Check    verify    = kSkip;
    Step     read;
    uint32_t bad_off   = 0;
    bool     ready     = false;   // the file is open for the stream
    int      stream_fr = 0;       // the stream's first error, sticky
};
Result g_res;

char g_path[24];

#if SHELL_SD_PROBE_BURST
// The raw window counter, handed over by the pass so the callback can read
// the current phase's tail (sd_hold_tail()). Null until the first pass: no
// phase, no tail.
const volatile uint32_t* g_win = nullptr;
#endif

#if SD_PROBE_TAIL
// ---- Build constants of the tail modes, set from A1's result row (spec
// section 5: C runs after A, in the transfer pattern A settles on). A1,
// 2026-11-01 (docs/hardware/sd-use-cases-measured.md section 3): bursts with
// the hold and no tail keep all 73 pots at the idle floor at 384 and
// 768 KB/s, so C runs in bursts, the hold on, tail 0.
//
// true: the transfers are A1's bursts -- one 32 KB transfer per
// kTailBurstMs (the 41 ms phases, ~799 KB/s), the pot hold on with
// kTailHoldTail blocks of tail. false: back to back, no hold. (rtail only for
// the bursts; wtail's w16 and w24 keep their own rate periods and just get the
// hold, wmax is back to back and unheld either way.)
constexpr bool     kTailBurst    = true;
constexpr uint8_t  kTailHoldTail = 0;    // A1's smallest passing tail
constexpr uint32_t kTailBurstMs  = 41;
#endif

#if SD_PROBE_HOLD
// Set around the f_read call itself. g_sd_seen alone would not cover a
// transfer longer than one block: the callback clears it at the first block
// boundary inside the call, and the blocks after that would take their pot
// readings as clean while the bus is still busy. The callback drops on either.
// g_sd_seen is set again after the call returns for the block the transfer
// ended in, whose readings were taken partly under it.
volatile bool g_sd_busy = false;

// Callback side. Counts per pass, not since boot: the foreground bumps
// g_pass_epoch after it has read the counters, and the callback starts them
// over at the first block that sees the new epoch (so the foreground never
// writes a variable the callback also writes). A block between the read and
// the bump is the one dropped from the count -- one block of 250.
SdHold              g_hold;
uint32_t            g_run       = 0;   // the open run of dropped blocks
uint32_t            g_cb_epoch  = 0;
volatile uint32_t   g_pass_epoch = 0;
volatile uint32_t   g_held_blocks = 0; // dropped blocks this pass
volatile uint32_t   g_hold_max    = 0; // the longest run of them this pass
#endif

// A hash of the word index, so a word read from the wrong place, a shifted
// sector or a stuck data line all fail the compare.
inline uint32_t pattern(uint32_t word)
{
    uint32_t x = word * 0x9E3779B1u;
    x ^= x >> 15;
    x *= 0x85EBCA77u;
    x ^= x >> 13;
    return x;
}

// The first bad byte of the chunk at word `word0`, or -1.
int32_t first_bad(const uint32_t* buf, uint32_t words, uint32_t word0)
{
    for(uint32_t i = 0; i < words; ++i)
        if(buf[i] != pattern(word0 + i))
            return static_cast<int32_t>(i * 4);
    return -1;
}

void account(Step& s, uint32_t t0, UINT n)
{
    // DWT cycles, not System::GetUs(): GetUs() wraps at ~21.47 s, not at
    // 2^32, so a call across its wrap came out as ~4.3e9 us (2026-10-31).
    // CYCCNT wraps at 2^32 (8.9 s), far longer than a call.
    const uint32_t dt = (cycles_now() - t0) / kCoreMhz;
    s.us += dt;
    s.bytes += n;
    if(dt > s.worst_us)
        s.worst_us = dt;
}

// Reads the whole open file in chunks and compares it with the pattern.
// Returns kOk or kBad; s.fr says whether the reads themselves went through.
Check read_verify(Step& s, uint32_t& bad_off)
{
    s.fr = FR_OK;
    for(uint32_t off = 0; off < kFileBytes; off += kChunk)
    {
        UINT           br = 0;
        const uint32_t t0 = cycles_now();
        s.fr              = f_read(&g_fs.file, g_buf, kChunk, &br);
        account(s, t0, br);
        if(s.fr != FR_OK || br != kChunk)
        {
            bad_off = off + br;
            return kBad;
        }
        const int32_t b = first_bad(g_buf, kChunk / 4, off / 4);
        if(b >= 0)
        {
            bad_off = off + static_cast<uint32_t>(b);
            return kBad;
        }
    }
    return kOk;
}

uint32_t kib_s(uint32_t bytes, uint32_t us)
{
    return us == 0 ? 0
                   : static_cast<uint32_t>(static_cast<uint64_t>(bytes) * 1000000u
                                           / 1024u / us);
}

const char* check_name(Check c)
{
    switch(c)
    {
        case kNone: return "none";
        case kOk: return "ok";
        case kBad: return "bad";
        default: return "skip";
    }
}

// True when `name` ends in ".bin", any case.
bool ends_bin(const char* name)
{
    const size_t n = strlen(name);
    return n >= 4 && name[n - 4] == '.' && (name[n - 3] | 0x20) == 'b'
           && (name[n - 2] | 0x20) == 'i' && (name[n - 1] | 0x20) == 'n';
}

// Lists the card's root into g_res: every entry counted, the first whole 8.3
// names (the short name when the entry has a long one) kept for the line.
void list_root(const char* root)
{
    DIR     dir;
    FILINFO fno;
    if(f_opendir(&dir, root) != FR_OK)
        return;
    g_res.root_seen = true;
    size_t used     = 0;
    bool   full     = false;
    for(;;)
    {
        if(f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == '\0')
            break;
        ++g_res.root_n;
        if(!(fno.fattrib & AM_DIR) && ends_bin(fno.fname))
            ++g_res.root_bin;
        if(g_res.root_n > 8 || full)
            continue;
        const char*  nm  = fno.altname[0] ? fno.altname : fno.fname;
        const size_t len = strlen(nm);
        const size_t add = len + (used ? 1 : 0);
        if(used + add > kRootNamesMax)
        {
            full = true;   // a later, shorter name must not jump the queue
            continue;
        }
        if(used)
            g_res.root_names[used++] = ',';
        memcpy(g_res.root_names + used, nm, len);
        used += len;
        g_res.root_names[used] = '\0';
    }
    f_closedir(&dir);
}

} // namespace

void sd_bringup_run()
{
    daisy::SdmmcHandler::Config cfg;
    cfg.Defaults();
    cfg.speed           = kSpeed;
    cfg.clock_powersave = kClockPowerSave;
    g_sdmmc.Init(cfg);
    g_fs.fsi.Init(daisy::FatFSInterface::Config::MEDIA_SD);

    const char* root = g_fs.fsi.GetSDPath();
    snprintf(g_path, sizeof g_path, "%s%s", root, kName);

    // 1: force the mount now (opt 1), so a card that does not answer says so
    // here and not at the first open.
    FATFS& fs      = g_fs.fsi.GetSDFileSystem();
    g_res.mount_fr = f_mount(&fs, root, 1);
    if(g_res.mount_fr != FR_OK)
        return;
    g_res.fs_type = fs.fs_type;
    DWORD   free_clusters = 0;
    FATFS*  fsp           = nullptr;
    if(f_getfree(root, &free_clusters, &fsp) == FR_OK)
    {
        g_res.total_mib = static_cast<uint32_t>((fsp->n_fatent - 2) * fsp->csize / 2048u);
        g_res.free_mib  = static_cast<uint32_t>(free_clusters * fsp->csize / 2048u);
    }

    // 1b: the old .bin test file goes (FR_NO_FILE is the normal case, nothing
    // to say), then the root is listed: the report line says whether any .bin
    // is left for the bootloader to find.
    char old_path[24];
    snprintf(old_path, sizeof old_path, "%s%s", root, kOldName);
    f_unlink(old_path);
    list_root(root);

    // 2: the file an earlier boot left, if any: bytes that survived a power
    // cycle.
    FRESULT fr = f_open(&g_fs.file, g_path, FA_READ);
    if(fr == FR_NO_FILE)
        g_res.prev = kNone;
    else if(fr != FR_OK)
    {
        g_res.prev         = kBad;
        g_res.prev_read.fr = fr;
    }
    else
    {
        if(f_size(&g_fs.file) != kFileBytes)
        {
            g_res.prev         = kBad;
            g_res.prev_read.fr = FR_OK;
            g_res.prev_off     = static_cast<uint32_t>(f_size(&g_fs.file));
        }
        else
            g_res.prev = read_verify(g_res.prev_read, g_res.prev_off);
        f_close(&g_fs.file);
    }

    // 3: write the file again. f_close is inside the timing: it flushes the
    // FAT and the directory entry, which a sample writer pays too.
    g_res.write.fr = f_open(&g_fs.file, g_path, FA_CREATE_ALWAYS | FA_WRITE);
    if(g_res.write.fr != FR_OK)
        return;
    for(uint32_t off = 0; off < kFileBytes; off += kChunk)
    {
        for(uint32_t i = 0; i < kChunk / 4; ++i)
            g_buf[i] = pattern(off / 4 + i);
        UINT           bw = 0;
        const uint32_t t0 = cycles_now();
        g_res.write.fr    = f_write(&g_fs.file, g_buf, kChunk, &bw);
        account(g_res.write, t0, bw);
        if(g_res.write.fr != FR_OK || bw != kChunk)
            break;
    }
    const uint32_t t0 = cycles_now();
    const FRESULT  cr = f_close(&g_fs.file);
    account(g_res.write, t0, 0);
    if(g_res.write.fr == FR_OK)
        g_res.write.fr = cr;
    if(g_res.write.fr != FR_OK || g_res.write.bytes != kFileBytes)
        return;

    // 4: read it back and compare every word. The file stays open: the
    // stream reads it from here on.
    g_res.read.fr = f_open(&g_fs.file, g_path, FA_READ);
    if(g_res.read.fr != FR_OK)
        return;
    g_res.verify = read_verify(g_res.read, g_res.bad_off);
    if(g_res.read.fr == FR_OK && f_lseek(&g_fs.file, 0) == FR_OK)
        g_res.ready = true;
}

void sd_report(bench::Board& hw, uint32_t pass)
{
    if(pass % kReportEvery != 0)
        return;
    // Each line inside libDaisy's 128-byte log buffer: the longest, the
    // verify line with every field at ten digits, is 107 bytes; the root line
    // is 46 plus its names, which list_root cuts to 72: 118.
    hw.PrintLine("SHELL_SD mount=%d fs=%d total_mib=%u free_mib=%u bus=4 mhz=%d ps=%d",
                 g_res.mount_fr, g_res.fs_type, static_cast<unsigned>(g_res.total_mib),
                 static_cast<unsigned>(g_res.free_mib), kMhz, kClockPowerSave ? 1 : 0);
    if(g_res.root_seen)
        hw.PrintLine("SHELL_SD root=%u bin=%u names=%s", static_cast<unsigned>(g_res.root_n),
                     static_cast<unsigned>(g_res.root_bin), g_res.root_names);
    hw.PrintLine("SHELL_SD prev=%s fr=%d off=%u", check_name(g_res.prev),
                 g_res.prev_read.fr, static_cast<unsigned>(g_res.prev_off));
    hw.PrintLine("SHELL_SD write kib=%u us=%u kib_s=%u worst_us=%u fr=%d",
                 static_cast<unsigned>(g_res.write.bytes / 1024u),
                 static_cast<unsigned>(g_res.write.us),
                 static_cast<unsigned>(kib_s(g_res.write.bytes, g_res.write.us)),
                 static_cast<unsigned>(g_res.write.worst_us), g_res.write.fr);
    hw.PrintLine("SHELL_SD verify=%s kib=%u us=%u kib_s=%u worst_us=%u fr=%d off=%u",
                 check_name(g_res.verify),
                 static_cast<unsigned>(g_res.read.bytes / 1024u),
                 static_cast<unsigned>(g_res.read.us),
                 static_cast<unsigned>(kib_s(g_res.read.bytes, g_res.read.us)),
                 static_cast<unsigned>(g_res.read.worst_us), g_res.read.fr,
                 static_cast<unsigned>(g_res.bad_off));
}

uint8_t sd_hold_tail()
{
#if SHELL_SD_PROBE_BURST
    const volatile uint32_t* w = g_win;
    if(w == nullptr)
        return 0;
    const BurstPhase p = burst_phase(*w);
    return p.on ? p.tail : 0;
#elif SD_PROBE_TAIL
    return kTailBurst ? kTailHoldTail : 0;
#else
    return 0;
#endif
}

bool sd_hold_block()
{
#if SD_PROBE_HOLD
#if SD_PROBE_TAIL
    if(!kTailBurst)
        return false;   // back to back: no hold, every block's readings stay
#endif
    const uint32_t epoch = g_pass_epoch;
    if(epoch != g_cb_epoch)
    {
        g_cb_epoch    = epoch;
        g_held_blocks = 0;
        g_hold_max    = g_run;   // a run that spans the pass boundary counts whole
    }
    const bool seen = g_sd_seen || g_sd_busy;
    g_sd_seen       = false;
    const bool drop = g_hold.step(seen, sd_hold_tail());
    if(drop)
    {
        ++g_run;
        g_held_blocks = g_held_blocks + 1u;
        if(g_run > g_hold_max)
            g_hold_max = g_run;
    }
    else
        g_run = 0;
    return drop;
#else
    return false;   // nothing sets g_sd_seen: every block's readings stay
#endif
}

#if SHELL_SD_PROBE_BURST
// Spec 2026-10-31-sd-use-cases-tests-design.md section 5, A1: what a sample
// player's streaming looks like to the pots. In a burst phase (burst_phase(),
// shell/sd_schedule.cpp) one verified 32 KB read of the test file every `per`
// ms, wrapping; idle phases read nothing. Between reads the pass waits with
// the bus clock stopped (kClockPowerSave), so the load is the reads and
// nothing else. A transfer, and `tail` blocks after it, cost the pots their
// readings (sd_hold_block()): the line says how many blocks that was, and
// read_sd.py judges the spread of what is left against the noise floor.
static void burst_pass(bench::Board& hw, const volatile uint32_t& win)
{
    g_win                 = &win;
    static bool    was_on = false;
    static uint32_t next  = 0;   // System::GetNow() of the next read
    const uint32_t w0     = win;
    const BurstPhase ph   = burst_phase(w0);
    const bool     on     = g_res.ready && ph.on;
    const uint32_t t0     = daisy::System::GetNow();
    uint32_t       bytes = 0, us = 0, worst = 0, bad = 0;
    if(on)
    {
        if(!was_on)
            next = t0;   // a phase starts with a read, not a catch-up
        // Until the 500 ms are up or the raw window counter leaves the
        // phase; at most one read spills into the next phase's first window,
        // which read_sd.py does not judge.
        while(daisy::System::GetNow() - t0 < kPassMs
              && win / kPhaseWin == w0 / kPhaseWin)
        {
            if(static_cast<int32_t>(daisy::System::GetNow() - next) < 0)
            {
                daisy::System::Delay(1);
                continue;
            }
            const uint32_t start = daisy::System::GetNow();
            FRESULT        fr    = FR_OK;
            if(f_tell(&g_fs.file) >= kFileBytes)
                fr = f_lseek(&g_fs.file, 0);
            const uint32_t word0 = static_cast<uint32_t>(f_tell(&g_fs.file) / 4u);
            UINT           br    = 0;
            const uint32_t u0    = cycles_now();
            if(fr == FR_OK)
            {
                g_sd_seen = true;
                g_sd_busy = true;
                fr        = f_read(&g_fs.file, g_buf, kChunk, &br);
                g_sd_seen = true;
                g_sd_busy = false;
            }
            const uint32_t dt = (cycles_now() - u0) / kCoreMhz;
            if(fr != FR_OK || br != kChunk)
            {
                // Stops the reads for good; the line keeps saying why.
                g_res.stream_fr = fr != FR_OK ? static_cast<int>(fr) : -2;
                g_res.ready     = false;
                break;
            }
            bytes += br;
            us += dt;
            if(dt > worst)
                worst = dt;
            if(first_bad(g_buf, kChunk / 4, word0) >= 0)
                ++bad;
            // The period runs from the read's start; a read slower than the
            // period does not queue a second one behind it.
            next = start + ph.period_ms;
            if(static_cast<int32_t>(daisy::System::GetNow() - next) > 0)
                next = daisy::System::GetNow();
        }
    }
    was_on = on;
    // The pass stays 500 ms long either way, so the frame rate does not
    // change with the phase.
    const uint32_t spent = daisy::System::GetNow() - t0;
    if(spent < kPassMs)
        hw.Delay(kPassMs - spent);
    // The pass's hold counters, then the callback starts them over.
    const uint32_t held = g_held_blocks;
    const uint32_t hmax = g_hold_max;
    g_pass_epoch        = g_pass_epoch + 1u;
    // Longest line: w 5 digits, kib 3, ms 3, kib_s 5, worst_us 6, held 3,
    // hmax 2 -- 120 bytes of the 128.
    hw.PrintLine("SHELL_SD_S w=%u on=%d mode=burst per=%u tail=%u kib=%u ms=%u kib_s=%u "
                 "worst_us=%u bad=%u fr=%d held=%u hmax=%u",
                 static_cast<unsigned>(w0), on ? 1 : 0, on ? ph.period_ms : 0u,
                 on ? static_cast<unsigned>(ph.tail) : 0u,
                 static_cast<unsigned>(bytes / 1024u), static_cast<unsigned>(us / 1000u),
                 static_cast<unsigned>(kib_s(bytes, us)), static_cast<unsigned>(worst),
                 static_cast<unsigned>(bad), g_res.stream_fr, static_cast<unsigned>(held),
                 static_cast<unsigned>(hmax));
}
#endif // SHELL_SD_PROBE_BURST

#if SD_PROBE_TAIL
// Spec 2026-10-31-sd-use-cases-tests-design.md section 5, C1 (rtail) and C2
// (wtail): how long can one f_read / f_write take, over minutes, so the
// ring buffers of the streaming and recording features can be sized.
//
// rtail makes sure FFSDBIG.DAT (512 MiB, the bring-up's pattern) exists, then
// reads 32 KB for 10 min each, `seq` sequentially, `rand` at random 32 KB-aligned
// offsets, `randfs` the same with FatFs's fast seek (a CLMT). wtail writes
// FFSDW.DAT, truncated at the start, 32 KB at a time: `w16` at 192 KB/s
// (16-bit stereo), `w24` at 288 KB/s (24-bit), `wmax` as fast as the card
// takes them, always back to back (kTailBurst bursts only rtail; w16 and w24 get
// the pot hold, wmax neither). One pass is the usual 500 ms; the
// phase and the 10 s lines count wall time (System::GetNow), the latencies
// DWT cycles.
//
// One line per 10 s and kind, in three prints with one seq (read_sd.py joins
// them; the 16 counts and the other fields do not fit one 128-byte line):
//   SHELL_SD_H kind=<k> seq=<n> n= worst_us= seek_worst_us= fr= bad= pat=<burst|b2b>
//   SHELL_SD_H kind=<k> seq=<n> b0=<8 counts>     buckets 0-7
//   SHELL_SD_H kind=<k> seq=<n> b1=<8 counts>     buckets 8-15
// The counts are of this interval only (not since boot). Bucket i counts the
// transfers that took [2^i, 2^(i+1)) x 64 us, bucket 0 everything under
// 128 us, bucket 15 everything from 2.1 s on. worst_us is the worst f_read /
// f_write; seek_worst_us the worst f_lseek (rand / randfs; seq's is the
// wrap-around back to 0; wtail's the wrap at the FAT32 file size limit).
// fr is the first failure, sticky (a FRESULT, -2 a short transfer, -3 no
// card test file to start from); once it is set nothing is transferred any
// more and the lines keep saying why. bad counts 32 KB reads that did not
// match the pattern (rtail). A failure before the first kind prints as
// kind=fill (the rtail fill and its early CLMT look) or kind=setup (-3).
// The CLMT lines `SHELL_SD_H clmt at=<pre|randfs> frags= dwords= cap= fr=` say
// how many fragments the file has (dwords = 2 + 2 x frags; 1 is contiguous).
namespace {

constexpr uint32_t kTailPhaseMs = 10u * 60u * 1000u;   // per kind
constexpr uint32_t kTailLineMs  = 10u * 1000u;
constexpr uint32_t kTailBuckets = 16;
constexpr uint32_t kTailBucketUs = 64;

#if SHELL_SD_PROBE_RTAIL
constexpr int         kTailPhases = 3;
const char* const     kTailKind[kTailPhases] = {"seq", "rand", "randfs"};
const char            kTailName[] = "FFSDBIG.DAT";
constexpr uint32_t    kBigBytes   = 512u * 1024u * 1024u;
constexpr uint32_t    kBigChunks  = kBigBytes / kChunk;   // 16384
static_assert(kBigChunks == (1u << 14), "the random offset is 14 bits of the xorshift");
constexpr uint32_t    kFillStep   = 64u * 1024u * 1024u;  // progress every 64 MiB
// The file is written in sequence, not pre-allocated with f_expand: libDaisy's
// ffconf.h has _USE_EXPAND 0 (a submodule, not changed here), so f_expand is not
// linked. FatFs allocates the clusters one after the next, so on a card with
// room the file is contiguous or nearly; the CLMT below does not need it to be.
// FatFs's fast seek: tbl[0] is the table's size in DWORDs, 2 + 2 per fragment of
// the file (ff.c f_lseek). 256 DWORDs (1 KB, in AXI .bss) hold 127 fragments;
// a sequentially written 512 MiB file on a card with room is one or a few.
// f_lseek(CREATE_LINKMAP) leaves the used size in tbl[0] -- the clmt line
// prints it -- and fails with FR_NOT_ENOUGH_CORE if it did not fit.
constexpr uint32_t    kClmtDwords = 256;
DWORD                 g_clmt[kClmtDwords];
#else
constexpr int         kTailPhases = 3;
const char* const     kTailKind[kTailPhases] = {"w16", "w24", "wmax"};
const char            kTailName[] = "FFSDW.DAT";
// FAT32 files end at 4 GiB - 1. wmax at the card's best would pass that inside
// its 10 min, so the position wraps to 0 before the last 32 KB chunk that fits
// and the rest of the run overwrites the file's start.
constexpr uint32_t    kWrapBytes  = 0xFFFFFFFFu - (0xFFFFFFFFu % kChunk);
#endif

struct TailInterval
{
    uint32_t b[kTailBuckets];
    uint32_t n;
    uint32_t worst_us;
    uint32_t seek_worst_us;
    uint32_t bad;
};

struct TailRun
{
    bool         begun       = false;   // the first pass has set the run up
    bool         file_open   = false;   // g_fs.file is the tail file
    int          phase       = -1;      // -1: preparing, 0..2 a kind, 3 finished
    int          fr          = 0;       // the first failure, sticky
    bool         force_line  = false;   // print at the pass's end, the 10 s not up
    uint32_t     phase_t0    = 0;       // System::GetNow() at the phase's start
    uint32_t     line_t0     = 0;       // ... at the last line
    uint32_t     seq         = 0;
    uint32_t     next        = 0;       // cycles_now() when the next transfer is due
    uint32_t     period_cyc  = 0;       // 0: back to back
    uint32_t     pos         = 0;       // the byte the next sequential transfer starts at
    uint32_t     rng         = 1;
    uint32_t     fill_off    = 0;       // rtail: bytes of the file written so far
    uint32_t     fill_t0     = 0;
    bool         filling     = false;
    TailInterval iv          = {};
};
TailRun g_t;

inline uint32_t xorshift32(uint32_t& x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

// [2^i, 2^(i+1)) x 64 us; below 128 us is bucket 0, the last bucket is open.
inline uint32_t tail_bucket(uint32_t us)
{
    const uint32_t q = us / kTailBucketUs;
    if(q < 2)
        return 0;
    const uint32_t i = 31u - static_cast<uint32_t>(__builtin_clz(q));
    return i < kTailBuckets - 1 ? i : kTailBuckets - 1;
}

void tail_fail(int fr)
{
    if(g_t.fr == 0)
    {
        g_t.fr         = fr;
        g_t.force_line = true;
    }
}

// A transfer is marked for the pot hold the way the burst pass marks its reads:
// busy for the whole call, seen again when it returns (sd_probe.h).
// EVERY card access of the tail modes goes through these (or TailMark): the
// transfers, and also the FAT walks of f_lseek, f_stat, f_open, f_close, f_sync
// and the CREATE_LINKMAP -- they are SDMMC traffic too, and with kTailBurst
// the hold has to drop the pot readings they take.
// wtail's `wmax` is the exception: it runs back to back, where a hold would drop
// nearly every block, so it is not marked (kTailBurst only holds w16 and w24).
inline bool tail_marked()
{
#if SHELL_SD_PROBE_WTAIL
    return g_t.phase != 2;
#else
    return true;
#endif
}
inline void tail_mark_begin()
{
    if(!tail_marked())
        return;
    g_sd_seen = true;
    g_sd_busy = true;
}
inline void tail_mark_end()
{
    if(!tail_marked())
        return;
    g_sd_seen = true;
    g_sd_busy = false;
}
// The same, for the length of a scope.
struct TailMark
{
    TailMark() { tail_mark_begin(); }
    ~TailMark() { tail_mark_end(); }
};

// f_lseek, timed; `us` gets the call's time.
FRESULT tail_seek(FSIZE_t ofs, uint32_t& us)
{
    const uint32_t u0 = cycles_now();
    const FRESULT  fr = f_lseek(&g_fs.file, ofs);
    us                = (cycles_now() - u0) / kCoreMhz;
    return fr;
}

void tail_record(uint32_t us)
{
    TailInterval& iv = g_t.iv;
    ++iv.n;
    ++iv.b[tail_bucket(us)];
    if(us > iv.worst_us)
        iv.worst_us = us;
}

void tail_close_file()
{
    if(g_t.file_open)
    {
        FRESULT fr;
        {
            TailMark m;
            fr = f_close(&g_fs.file);
        }
        g_t.file_open = false;
        if(fr != FR_OK)
            tail_fail(static_cast<int>(fr));
    }
}

#if SHELL_SD_PROBE_RTAIL
// The clmt line. After f_lseek(CREATE_LINKMAP) tbl[0] is 2 + 2 x fragments (the
// size the table needed, if lr is FR_NOT_ENOUGH_CORE), so frags=1 means the
// file is contiguous. `when` says which look it is: "pre" (right after the file
// is known good, before seq and rand read it) or "randfs" (the table that phase
// really uses). Any other failure leaves tbl[0] at the capacity the caller
// preset, which is not a fragment count: the line then says frags=0 dwords=0
// and fr= names the error.
void tail_clmt_line(bench::Board& hw, const char* when, FRESULT lr)
{
    const uint32_t dwords = (lr == FR_OK || lr == FR_NOT_ENOUGH_CORE) ? g_clmt[0] : 0u;
    hw.PrintLine("SHELL_SD_H clmt at=%s frags=%u dwords=%u cap=%u fr=%d", when,
                 static_cast<unsigned>(dwords >= 2 ? (dwords - 2) / 2 : 0),
                 static_cast<unsigned>(dwords), static_cast<unsigned>(kClmtDwords),
                 static_cast<int>(lr));
}
#endif

// Starts kind `p`: the interval and the schedule start over, and the file is
// opened the way the kind needs it.
void tail_begin_phase(bench::Board& hw, int p)
{
    TailRun& s = g_t;
    s.phase    = p;
    s.phase_t0 = daisy::System::GetNow();
    s.line_t0  = s.phase_t0;
    s.iv       = {};
    s.pos      = 0;
    s.next     = cycles_now();
    if(p >= kTailPhases)
    {
        tail_close_file();   // wtail: the f_close that writes the directory entry
        return;
    }
#if SHELL_SD_PROBE_RTAIL
    s.period_cyc = kTailBurst ? kTailBurstMs * 1000u * kCoreMhz : 0u;
    tail_close_file();   // every kind opens the file afresh: that also nulls cltbl
    char path[24];
    snprintf(path, sizeof path, "%s%s", g_fs.fsi.GetSDPath(), kTailName);
    FRESULT fr;
    {
        TailMark m;
        fr = f_open(&g_fs.file, path, FA_READ);
    }
    if(fr != FR_OK)
    {
        tail_fail(static_cast<int>(fr));
        return;
    }
    s.file_open = true;
    // From the cycle counter, as the brief says; a few draws first so the
    // low-entropy start of a xorshift does not show (memory: probe seeds
    // must be mixed).
    s.rng = cycles_now() | 1u;
    for(int i = 0; i < 16; ++i)
        xorshift32(s.rng);
    if(p == 2)
    {
        g_clmt[0]       = kClmtDwords;
        g_fs.file.cltbl = g_clmt;
        FRESULT lr;
        {
            TailMark m;
            lr = f_lseek(&g_fs.file, CREATE_LINKMAP);
        }
        tail_clmt_line(hw, "randfs", lr);
        if(lr != FR_OK)
            tail_fail(static_cast<int>(lr));
    }
#else
    (void)hw;
    // 192 KB/s and 288 KB/s: one 32 KB write every 32 KiB / rate.
    const uint32_t rate_kib_s = p == 0 ? 192u : 288u;
    if(p < 2)
        s.period_cyc = static_cast<uint32_t>(32000ull * 1000u * kCoreMhz / rate_kib_s);
    else
        s.period_cyc = 0;   // wmax: full speed, whatever kTailBurst says
    if(p == 0)
    {
        // Truncated here, once; the other kinds go on in the same file.
        char path[24];
        snprintf(path, sizeof path, "%s%s", g_fs.fsi.GetSDPath(), kTailName);
        for(uint32_t i = 0; i < kChunk / 4; ++i)
            g_buf[i] = pattern(i);   // the data is not verified: any bytes will do
        FRESULT fr;
        {
            TailMark m;
            fr = f_open(&g_fs.file, path, FA_CREATE_ALWAYS | FA_WRITE);
        }
        if(fr != FR_OK)
        {
            tail_fail(static_cast<int>(fr));
            return;
        }
        s.file_open = true;
    }
#endif
}

#if SHELL_SD_PROBE_RTAIL
// FFSDBIG.DAT is as long as it should be and its last chunk matches the
// pattern. FatFs writes the directory entry's size at f_sync / f_close only, so
// a file whose fill was cut off by a reset has the size of the last sync
// (kFillStep), not 512 MiB; the last chunk is a second look at the same thing.
bool big_file_ok(const char* path)
{
    TailMark m;   // the stat, the FAT walk of the lseek, the read and the close
    FILINFO fno;
    if(f_stat(path, &fno) != FR_OK || fno.fsize != kBigBytes)
        return false;
    if(f_open(&g_fs.file, path, FA_READ) != FR_OK)
        return false;
    const FSIZE_t last = kBigBytes - kChunk;
    UINT          br   = 0;
    bool          ok   = f_lseek(&g_fs.file, last) == FR_OK
                         && f_read(&g_fs.file, g_buf, kChunk, &br) == FR_OK && br == kChunk
                         && first_bad(g_buf, kChunk / 4, last / 4) < 0;
    f_close(&g_fs.file);
    return ok;
}

// Builds the file's CLMT once, early, and prints how many fragments the file
// has, so contiguity is known before seq and rand read it (the randfs phase
// builds its own, 20 min in). Not a failure when the table is too small: seq and
// rand do not need it, and randfs will say the same with fr=17.
void tail_clmt_probe(bench::Board& hw)
{
    char path[24];
    snprintf(path, sizeof path, "%s%s", g_fs.fsi.GetSDPath(), kTailName);
    FRESULT fr, lr = FR_OK;
    {
        TailMark m;
        fr = f_open(&g_fs.file, path, FA_READ);
        if(fr == FR_OK)
        {
            g_clmt[0]       = kClmtDwords;
            g_fs.file.cltbl = g_clmt;
            lr              = f_lseek(&g_fs.file, CREATE_LINKMAP);
            f_close(&g_fs.file);
            g_fs.file.cltbl = nullptr;
        }
    }
    if(fr != FR_OK)
    {
        tail_fail(static_cast<int>(fr));
        return;
    }
    tail_clmt_line(hw, "pre", lr);
}

// One chunk of the fill (phase -1). The fill is spread over the passes, so the
// report loop keeps printing while ~70 s of writing go by.
void tail_fill_step(bench::Board& hw)
{
    TailRun& s = g_t;
    if(!s.begun)
    {
        s.begun = true;
        {
            TailMark m;
            f_close(&g_fs.file);   // the bring-up's test file, open for the stream
        }
        g_res.ready = false;
        char path[24];
        snprintf(path, sizeof path, "%s%s", g_fs.fsi.GetSDPath(), kTailName);
        if(big_file_ok(path))
        {
            hw.PrintLine("SHELL_SD_H fill have=1 mib=%u", static_cast<unsigned>(kBigBytes >> 20));
            tail_clmt_probe(hw);
            if(s.fr == 0)
                tail_begin_phase(hw, 0);
            return;
        }
        FRESULT fr;
        {
            TailMark m;
            fr = f_open(&g_fs.file, path, FA_CREATE_ALWAYS | FA_WRITE);
        }
        if(fr != FR_OK)
        {
            tail_fail(static_cast<int>(fr));
            return;
        }
        s.file_open = true;
        s.filling   = true;
        s.fill_off  = 0;
        s.fill_t0   = daisy::System::GetNow();
        hw.PrintLine("SHELL_SD_H fill have=0 mib=0");
        return;
    }
    for(uint32_t i = 0; i < kChunk / 4; ++i)
        g_buf[i] = pattern(s.fill_off / 4 + i);
    UINT           bw = 0;
    tail_mark_begin();
    const FRESULT  fr = f_write(&g_fs.file, g_buf, kChunk, &bw);
    tail_mark_end();
    if(fr != FR_OK || bw != kChunk)
    {
        tail_fail(fr != FR_OK ? static_cast<int>(fr) : -2);
        return;
    }
    s.fill_off += kChunk;
    if(s.fill_off % kFillStep == 0)
    {
        // f_sync writes the size, so a reset from here on leaves a file of
        // at least this many bytes -- which big_file_ok() still calls short.
        FRESULT sr;
        {
            TailMark m;
            sr = s.fill_off == kBigBytes ? f_close(&g_fs.file) : f_sync(&g_fs.file);
        }
        if(s.fill_off == kBigBytes)
            s.file_open = false;
        hw.PrintLine("SHELL_SD_H fill mib=%u ms=%u fr=%d", static_cast<unsigned>(s.fill_off >> 20),
                     static_cast<unsigned>(daisy::System::GetNow() - s.fill_t0),
                     static_cast<int>(sr));
        if(sr != FR_OK)
        {
            tail_fail(static_cast<int>(sr));
            return;
        }
        if(s.fill_off == kBigBytes)
        {
            s.filling = false;
            tail_clmt_probe(hw);
            if(s.fr == 0)
                tail_begin_phase(hw, 0);
        }
    }
}
#endif // SHELL_SD_PROBE_RTAIL

// One transfer of the current kind, timed into the interval.
void tail_transfer()
{
    TailRun&  s = g_t;
    FRESULT   fr = FR_OK;
    UINT      n = 0;
    uint32_t  seek_us = 0, dt = 0;
    tail_mark_begin();
#if SHELL_SD_PROBE_RTAIL
    if(s.phase == 0)
    {
        if(s.pos >= kBigBytes)   // one pass of the file: back to the start
        {
            s.pos = 0;
            fr    = tail_seek(0, seek_us);
        }
    }
    else
    {
        s.pos = (xorshift32(s.rng) >> 18) * kChunk;
        fr    = tail_seek(s.pos, seek_us);   // randfs: the CLMT makes this a table walk
    }
    if(fr == FR_OK)
    {
        const uint32_t u0 = cycles_now();
        fr                = f_read(&g_fs.file, g_buf, kChunk, &n);
        dt                = (cycles_now() - u0) / kCoreMhz;
    }
#else
    if(f_tell(&g_fs.file) >= kWrapBytes)
        fr = tail_seek(0, seek_us);
    if(fr == FR_OK)
    {
        const uint32_t u0 = cycles_now();
        fr                = f_write(&g_fs.file, g_buf, kChunk, &n);
        dt                = (cycles_now() - u0) / kCoreMhz;
    }
#endif
    tail_mark_end();
    if(seek_us > s.iv.seek_worst_us)
        s.iv.seek_worst_us = seek_us;
    if(fr != FR_OK || n != kChunk)
    {
        tail_fail(fr != FR_OK ? static_cast<int>(fr) : -2);
        return;
    }
    tail_record(dt);
#if SHELL_SD_PROBE_RTAIL
    if(first_bad(g_buf, kChunk / 4, s.pos / 4) >= 0)
        ++s.iv.bad;
    s.pos += kChunk;
#endif
}

// The interval's three prints, then the interval starts over.
void tail_emit(bench::Board& hw)
{
    TailRun&           s    = g_t;
    const int          k    = s.phase < 0 ? 0 : s.phase >= kTailPhases ? kTailPhases - 1 : s.phase;
    // Before the first kind there is no kind to blame: a failure of the rtail
    // fill (or its early CLMT look) is `fill`, no card-test file to start from
    // (-3) is `setup`. read_sd.py reports them under those names.
    const char* const  kind = s.phase >= 0 ? kTailKind[k] : s.fr == -3 ? "setup" : "fill";
    const TailInterval& iv  = s.iv;
    const unsigned     sq   = static_cast<unsigned>(s.seq);
    // wtail's wmax is never in bursts, whatever kTailBurst says.
    bool burst = kTailBurst;
#if SHELL_SD_PROBE_WTAIL
    if(k == 2)
        burst = false;
#endif
    // Longest stats line: kind=randfs, seq 4 digits, n 4 (a 25 MB/s card does at
    // most 763 transfers a second), worst_us and seek_worst_us 7, fr 2, bad 4,
    // pat=burst: 102 of the 128 bytes (counted with the digits at those widths).
    // The bucket lines are 35 + 8 counts of 4 digits and 7 commas: 74 (82 with
    // 5-digit counts).
    hw.PrintLine("SHELL_SD_H kind=%s seq=%u n=%u worst_us=%u seek_worst_us=%u fr=%d bad=%u pat=%s",
                 kind, sq, static_cast<unsigned>(iv.n), static_cast<unsigned>(iv.worst_us),
                 static_cast<unsigned>(iv.seek_worst_us), s.fr, static_cast<unsigned>(iv.bad),
                 burst ? "burst" : "b2b");
    hw.PrintLine("SHELL_SD_H kind=%s seq=%u b0=%u,%u,%u,%u,%u,%u,%u,%u", kind, sq,
                 static_cast<unsigned>(iv.b[0]), static_cast<unsigned>(iv.b[1]),
                 static_cast<unsigned>(iv.b[2]), static_cast<unsigned>(iv.b[3]),
                 static_cast<unsigned>(iv.b[4]), static_cast<unsigned>(iv.b[5]),
                 static_cast<unsigned>(iv.b[6]), static_cast<unsigned>(iv.b[7]));
    hw.PrintLine("SHELL_SD_H kind=%s seq=%u b1=%u,%u,%u,%u,%u,%u,%u,%u", kind, sq,
                 static_cast<unsigned>(iv.b[8]), static_cast<unsigned>(iv.b[9]),
                 static_cast<unsigned>(iv.b[10]), static_cast<unsigned>(iv.b[11]),
                 static_cast<unsigned>(iv.b[12]), static_cast<unsigned>(iv.b[13]),
                 static_cast<unsigned>(iv.b[14]), static_cast<unsigned>(iv.b[15]));
    s.iv         = {};
    ++s.seq;
    s.line_t0    = daisy::System::GetNow();
    s.force_line = false;
}

} // namespace

static void tail_pass(bench::Board& hw, const volatile uint32_t& /*win*/)
{
    TailRun&       s  = g_t;
    const uint32_t t0 = daisy::System::GetNow();
    if(!s.begun && s.phase == -1 && s.fr == 0)
    {
        if(!g_res.ready)
            tail_fail(-3);   // the card test did not leave its file open
#if SHELL_SD_PROBE_WTAIL
        else
        {
            s.begun = true;
            {
                TailMark m;
                f_close(&g_fs.file);   // the bring-up's test file, open for the stream
            }
            g_res.ready = false;
            tail_begin_phase(hw, 0);
        }
#endif
    }
    while(s.fr == 0 && s.phase < kTailPhases && daisy::System::GetNow() - t0 < kPassMs)
    {
#if SHELL_SD_PROBE_RTAIL
        if(s.phase < 0)
        {
            tail_fill_step(hw);
            continue;
        }
#endif
        if(daisy::System::GetNow() - s.phase_t0 >= kTailPhaseMs)
        {
            tail_emit(hw);   // the phase's last, shorter interval
            tail_begin_phase(hw, s.phase + 1);
            continue;
        }
        if(s.period_cyc != 0 && static_cast<int32_t>(cycles_now() - s.next) < 0)
        {
            daisy::System::Delay(1);
            continue;
        }
        const uint32_t start = cycles_now();
        tail_transfer();
        if(s.period_cyc != 0)
        {
            // The period runs from the transfer's start; one that took longer
            // than the period does not queue a second behind it.
            s.next = start + s.period_cyc;
            if(static_cast<int32_t>(cycles_now() - s.next) > 0)
                s.next = cycles_now();
        }
    }
    // The pass stays 500 ms long either way, so the frame rate does not
    // change with the phase.
    const uint32_t spent = daisy::System::GetNow() - t0;
    if(spent < kPassMs)
        hw.Delay(kPassMs - spent);
    if(s.phase >= kTailPhases && s.fr == 0)
    {
        if(daisy::System::GetNow() - s.line_t0 >= kTailLineMs)
        {
            s.line_t0 = daisy::System::GetNow();
            hw.PrintLine("SHELL_SD_H done phases=%d", kTailPhases);
        }
        return;
    }
    // No prints while the rtail fill runs: it has its own progress lines.
    const bool preparing = s.phase < 0 && s.fr == 0;
    if(!preparing && (s.force_line || daisy::System::GetNow() - s.line_t0 >= kTailLineMs))
        tail_emit(hw);
}
#endif // SD_PROBE_TAIL

#if SHELL_SD_PROBE_STREAM
static void stream_pass(bench::Board& hw, const volatile uint32_t& win)
{
    const uint32_t w0 = win;
    const bool     on = g_res.ready && ((w0 / kPhaseWin) & 1u) != 0;
    // Test A3: the on-phases rotate through three arms, sdram-16, axi-16,
    // sdram-32. The first two differ only in where the buffer is, the last two
    // only in the chunk size (the AXI buffer cannot be 32 KB, see g_buf_axi), so
    // the location and the chunk size are each contrasted on their own
    // (moderator correction 2026-10-31; two arms alone confounded them). A phase
    // is kPhaseWin windows and on-phases are the odd ones, so w / (2 * kPhaseWin)
    // counts them. A pass stays inside its phase (the loop below ends when the
    // window counter leaves it), so one pass, one arm.
    const uint32_t   arm   = (w0 / (2u * kPhaseWin)) % 3u;
    const bool       axi   = arm == 1;
    uint32_t* const  buf   = axi ? g_buf_axi : g_buf;
    const uint32_t   chunk = arm == 2 ? kChunk : kChunkAxi;
    const uint32_t t0 = daisy::System::GetNow();
    uint32_t       bytes = 0, worst = 0, bad = 0, ms = 0;
    if(on)
    {
        // The previous on-phase may have used another chunk size and left the
        // position on a boundary of that one only (16 KB steps can stop at an
        // odd multiple, where a 32 KB read would come back short and end the
        // stream). Round down to this pass's own chunk; the words a repeated
        // read covers are checked again, and word0 below comes from f_tell.
        const FSIZE_t pos = f_tell(&g_fs.file);
        if(pos % chunk != 0)
        {
            const FRESULT sr = f_lseek(&g_fs.file, pos - pos % chunk);
            if(sr != FR_OK)
            {
                g_res.stream_fr = static_cast<int>(sr);
                g_res.ready     = false;
            }
        }
        // Until the 500 ms are up or the window counter leaves the phase; at
        // most one chunk (a few ms) spills into the next phase's first window,
        // which read_sd.py does not judge.
        while(g_res.ready && daisy::System::GetNow() - t0 < kPassMs
              && ((win / kPhaseWin) & 1u) != 0)
        {
            FRESULT fr = FR_OK;
            if(f_tell(&g_fs.file) >= kFileBytes)
                fr = f_lseek(&g_fs.file, 0);
            const uint32_t word0 = static_cast<uint32_t>(f_tell(&g_fs.file) / 4u);
            UINT           br    = 0;
            const uint32_t u0    = cycles_now();
            if(fr == FR_OK)
                fr = f_read(&g_fs.file, buf, chunk, &br);
            const uint32_t dt = (cycles_now() - u0) / kCoreMhz;
            if(fr != FR_OK || br != chunk)
            {
                // Stops the stream for good; the line keeps saying why.
                g_res.stream_fr = fr != FR_OK ? static_cast<int>(fr) : -2;
                g_res.ready     = false;
                break;
            }
            bytes += br;
            if(dt > worst)
                worst = dt;
            if(first_bad(buf, chunk / 4, word0) >= 0)
                ++bad;
        }
        ms = daisy::System::GetNow() - t0;
    }
    // The pass stays 500 ms long either way, so the frame rate does not
    // change with the phase.
    const uint32_t spent = daisy::System::GetNow() - t0;
    if(spent < kPassMs)
        hw.Delay(kPassMs - spent);
    // buf= names the buffer this pass read into and chunk= the KiB of each
    // f_read (16 or 32); an idle pass has neither buf= nor a chunk (0).
    // Longest line, with the fixed text 84 bytes (buf=sdram, chunk=32): w 10
    // digits, kib 5 (the card's 25 MB/s ceiling is 12 207 KiB a pass), ms 3,
    // kib_s 5, worst_us 7, bad 3, fr 2 -- 119 bytes of the 128.
    hw.PrintLine("SHELL_SD_S w=%u on=%d mode=stream%s chunk=%u kib=%u ms=%u kib_s=%u worst_us=%u "
                 "bad=%u fr=%d",
                 static_cast<unsigned>(w0), on ? 1 : 0, !on ? "" : axi ? " buf=axi" : " buf=sdram",
                 static_cast<unsigned>(on ? chunk / 1024u : 0u),
                 static_cast<unsigned>(bytes / 1024u),
                 static_cast<unsigned>(ms),
                 static_cast<unsigned>(ms == 0 ? 0 : static_cast<uint64_t>(bytes) * 1000u / 1024u / ms),
                 static_cast<unsigned>(worst), static_cast<unsigned>(bad), g_res.stream_fr);
}
#endif // SHELL_SD_PROBE_STREAM

void sd_stream_pass(bench::Board& hw, const volatile uint32_t& win)
{
#if SHELL_SD_PROBE_BURST
    burst_pass(hw, win);
#elif SD_PROBE_TAIL
    tail_pass(hw, win);
#elif SHELL_SD_PROBE_STREAM
    stream_pass(hw, win);
#else
#error "this SHELL_SD_PROBE mode has no pass yet (shell/sd_probe.cpp)"
#endif
}

} // namespace shell
