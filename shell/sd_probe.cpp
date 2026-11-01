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
// The operation modes (spec section 5, D1 bank, D2 buffer, D4 patch): one card
// operation now and then, one line per operation.
#if SHELL_SD_PROBE_BANK || SHELL_SD_PROBE_BUFFER || SHELL_SD_PROBE_PATCH
#define SD_PROBE_OPS 1
#else
#define SD_PROBE_OPS 0
#endif
// The modes that mark their transfers for the pot hold (g_sd_busy; the
// callback side, sd_hold_block()). In the tail modes the hold runs only when
// kTailBurst says the transfers are A1's bursts; the operation modes and the
// duplex mode always hold, with A1's tail of 0 (sd_hold_tail()).
#if SHELL_SD_PROBE_BURST || SD_PROBE_TAIL || SD_PROBE_OPS || SHELL_SD_PROBE_DUPLEX
#define SD_PROBE_HOLD 1
#else
#define SD_PROBE_HOLD 0
#endif

#if SHELL_SD_PROBE_BANK || SHELL_SD_PROBE_DUPLEX
#include <atomic>
#endif
#if SHELL_SD_PROBE_BANK
#include "synth/wt_bank.h"
#endif
#if SHELL_SD_PROBE_BUFFER
#include <cmath>
#include "sampler/sample_buffer.h"
#include "sdram_mem.h"
#endif
#if SHELL_SD_PROBE_PATCH
#include "persist_flash.h"
#if !SHELL_PERSIST_ON
#error "SHELL_SD_PROBE=patch copies the persisted record: it needs SHELL_PERSIST_ON (persist_flash.h)"
#endif
#endif

namespace shell {

#if SHELL_SD_PROBE_BANK
int16_t* wt_bank_slot(int i);   // wt_bank_d2.cpp
#endif

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

#if SD_PROBE_OPS
// Spec 2026-10-31-sd-use-cases-tests-design.md section 5, D1 (bank), D2
// (buffer) and D4 (patch): what the features that load from and save to the
// card cost the instrument while it plays, and whether the bytes come back.
//
// One operation runs inside one pass and prints its line when it is done; the
// pass is padded to kPassMs as in the other modes. The buffer's saves and
// loads take seconds, longer than a pass, and still run in one: read_sd.py's
// ops_by_phase() prices the callback by the frame after an operation line,
// so an operation must not be split across frames. (The report loop is held
// for those seconds; the listener's command poll runs in the callback.)
//
// Every card access of these modes, f_open to f_close and f_sync, runs inside
// an OpMark: g_sd_busy for the whole operation, g_sd_seen again when it ends,
// so the pot hold drops the readings of every block it touched (A1, 2026-11-01:
// the hold with tail 0 keeps the pots clean; sd_hold_tail() is 0 here). held=
// counts those blocks: op_epoch() starts the callback's count over as the
// operation starts, and the line reads it after the pass's pad.
//
// Transfers are kOpChunk, 16 KB: A3 (2026-11-01, docs/hardware/
// sd-use-cases-measured.md) measured 32 KB reads costing the audio callback
// more than 16 KB reads on every workload; where the buffer sat (SDRAM or AXI)
// had no consistent effect.
//
// fr is the first failure, sticky (a FRESULT, -2 a short transfer, -3 no card
// test to start from or, patch, no record to copy, -4 a bank or buffer load
// that did not compare); the operation's line says it (a compare failure as
// cmp=bad with fr=0), nothing more runs, and `SHELL_SD_<K|B|P> stop fr=`
// follows every 10 s. (patch counts a bad compare and goes on: 100 of 100.)
namespace {

constexpr uint32_t kOpChunk  = 16u * 1024u;
constexpr uint32_t kOpLineMs = 10u * 1000u;   // the stop and done lines
static_assert(kOpChunk <= kChunk, "g_buf holds one piece");

struct OpMark
{
    OpMark()
    {
        g_sd_seen = true;
        g_sd_busy = true;
    }
    ~OpMark()
    {
        g_sd_seen = true;
        g_sd_busy = false;
    }
};

struct OpRun
{
    bool     begun   = false;
    int      fr      = 0;   // the first failure, sticky
    uint32_t t0      = 0;   // System::GetNow() at the first pass
    uint32_t next    = 0;   // ... when the next operation is due
    uint32_t line_t0 = 0;   // ... at the last stop or done line
};
OpRun g_op;

void op_fail(int fr)
{
    if(g_op.fr == 0)
        g_op.fr = fr;
}

// The hold's count starts over at the callback's next block (sd_hold_block()).
void op_epoch() { g_pass_epoch = g_pass_epoch + 1u; }

uint32_t us_since(uint32_t c0) { return (cycles_now() - c0) / kCoreMhz; }

const char* op_path(const char* name)
{
    snprintf(g_path, sizeof g_path, "%s%s", g_fs.fsi.GetSDPath(), name);
    return g_path;
}

// The first pass: the bring-up left its test file open for the stream; these
// modes need the FIL. No card test to start from is fr -3, as in the tail modes.
void op_begin()
{
    g_op.begun   = true;
    g_op.t0      = daisy::System::GetNow();
    g_op.next    = g_op.t0;
    g_op.line_t0 = g_op.t0;
    if(!g_res.ready)
    {
        op_fail(-3);
        return;
    }
    OpMark m;
    f_close(&g_fs.file);
    g_res.ready = false;
}

#if SHELL_SD_PROBE_BANK || SHELL_SD_PROBE_BUFFER
// n bytes from src into the open file in kOpChunk pieces; worst gets the
// slowest f_write. A FRESULT, or -2 for a short write. (patch moves one
// record, below a piece, in single calls.)
int op_write(const uint8_t* src, uint32_t n, uint32_t& worst)
{
    for(uint32_t off = 0; off < n; off += kOpChunk)
    {
        const UINT     len = n - off < kOpChunk ? n - off : kOpChunk;
        UINT           bw  = 0;
        const uint32_t c0  = cycles_now();
        const FRESULT  fr  = f_write(&g_fs.file, src + off, len, &bw);
        const uint32_t dt  = us_since(c0);
        if(dt > worst)
            worst = dt;
        if(fr != FR_OK)
            return static_cast<int>(fr);
        if(bw != len)
            return -2;
    }
    return 0;
}

// One kOpChunk piece (or the file's shorter last one) into dst, timed into
// worst. A FRESULT, or -2 for a short read.
int op_read_piece(uint8_t* dst, UINT len, uint32_t& worst)
{
    UINT           br = 0;
    const uint32_t c0 = cycles_now();
    const FRESULT  fr = f_read(&g_fs.file, dst, len, &br);
    const uint32_t dt = us_since(c0);
    if(dt > worst)
        worst = dt;
    if(fr != FR_OK)
        return static_cast<int>(fr);
    return br == len ? 0 : -2;
}
#endif

// The pass stays kPassMs long when its operation was shorter, so the frame
// rate does not change with the schedule.
void op_pad(bench::Board& hw, uint32_t t0)
{
    const uint32_t spent = daisy::System::GetNow() - t0;
    if(spent < kPassMs)
        hw.Delay(kPassMs - spent);
}

// Every 10 s once a failure stopped the mode.
void op_stop_line(bench::Board& hw, char tag)
{
    if(g_op.fr != 0 && daisy::System::GetNow() - g_op.line_t0 >= kOpLineMs)
    {
        g_op.line_t0 = daisy::System::GetNow();
        hw.PrintLine("SHELL_SD_%c stop fr=%d", tag, g_op.fr);
    }
}

#if SHELL_SD_PROBE_BANK
// D1: phases of 10 s, idle and loading in turn, from the first pass. In a
// loading phase FFBANK.WTB (the factory bank, raw int16 LE in kBankSamples'
// layout, written once on the first pass) is read every 2 s into the slot
// the engine does not play (wt_bank_d2.cpp), compared with kBankSamples, and
// handed to the callback, which swaps at the top of its next block
// (sd_bank_block()). A load does not start while a swap is pending, so the
// slot it writes is never one the engine reads. Both slots hold the factory
// bank: whether a swap between different banks clicks is a feature question
// (crossfade or not) and not tested here.
//
// Lines (longest: load_us and worst_us 7 digits, held 4, fr 2 -- 74 bytes):
//   SHELL_SD_K export kib= us= worst_us= fr=            once, the first pass
//   SHELL_SD_K load_us= worst_us= cmp=<ok|bad> held= fr= slot=
// load_us is f_open to f_close, the copies from g_buf into the slot included,
// the compare not; worst_us the slowest f_read; slot the slot loaded (0, 1).
const char         kBankName[]  = "FFBANK.WTB";
constexpr uint32_t kBankBytes   = sizeof(spky::wt::kBankSamples);   // 65 024
constexpr uint32_t kBankPhaseMs = 10u * 1000u;
constexpr uint32_t kBankEveryMs = 2u * 1000u;

// A verified slot, set by the foreground, taken (and cleared) by the callback.
const int16_t* volatile g_bank_pending = nullptr;

// Through g_buf: the flash constant is neither 32-byte aligned nor in RAM,
// and the driver's DMA and its cache maintenance want both.
void bank_export(bench::Board& hw)
{
    uint32_t       worst = 0;
    int            fr;
    const uint32_t c0 = cycles_now();
    {
        OpMark m;
        fr = f_open(&g_fs.file, op_path(kBankName), FA_CREATE_ALWAYS | FA_WRITE);
        if(fr == FR_OK)
        {
            const uint8_t* src = reinterpret_cast<const uint8_t*>(spky::wt::kBankSamples);
            for(uint32_t off = 0; off < kBankBytes && fr == 0; off += kOpChunk)
            {
                const uint32_t len = kBankBytes - off < kOpChunk ? kBankBytes - off : kOpChunk;
                std::memcpy(g_buf, src + off, len);
                fr = op_write(reinterpret_cast<const uint8_t*>(g_buf), len, worst);
            }
            const FRESULT cr = f_close(&g_fs.file);
            if(fr == 0)
                fr = static_cast<int>(cr);
        }
    }
    const uint32_t us = us_since(c0);
    if(fr != 0)
        op_fail(fr);
    hw.PrintLine("SHELL_SD_K export kib=%u us=%u worst_us=%u fr=%d",
                 static_cast<unsigned>(kBankBytes / 1024u), static_cast<unsigned>(us),
                 static_cast<unsigned>(worst), fr);
}

struct BankLoad
{
    uint32_t us    = 0;
    uint32_t worst = 0;
    bool     ok    = false;
    int      fr    = 0;
    int      slot  = 0;
};

BankLoad bank_load()
{
    BankLoad r;
    // Read once: nothing else writes the bank pointer while no swap is pending.
    r.slot             = spky::wt::bank() == wt_bank_slot(0) ? 1 : 0;
    uint8_t* const dst = reinterpret_cast<uint8_t*>(wt_bank_slot(r.slot));
    const uint32_t c0  = cycles_now();
    {
        OpMark m;
        r.fr = f_open(&g_fs.file, op_path(kBankName), FA_READ);
        if(r.fr == FR_OK)
        {
            for(uint32_t off = 0; off < kBankBytes && r.fr == 0; off += kOpChunk)
            {
                const UINT len = kBankBytes - off < kOpChunk ? kBankBytes - off : kOpChunk;
                r.fr = op_read_piece(reinterpret_cast<uint8_t*>(g_buf), len, r.worst);
                if(r.fr == 0)
                    std::memcpy(dst + off, g_buf, len);
            }
            const FRESULT cr = f_close(&g_fs.file);
            if(r.fr == 0)
                r.fr = static_cast<int>(cr);
        }
    }
    r.us = us_since(c0);
    r.ok = r.fr == 0 && std::memcmp(dst, spky::wt::kBankSamples, kBankBytes) == 0;
    if(r.ok)
    {
        // The slot's bytes before the pointer that publishes them.
        std::atomic_signal_fence(std::memory_order_seq_cst);
        g_bank_pending = reinterpret_cast<const int16_t*>(dst);
    }
    return r;
}

void bank_pass(bench::Board& hw)
{
    OpRun&         s  = g_op;
    const uint32_t t0 = daisy::System::GetNow();
    bool           ran = false;
    BankLoad       r;
    if(!s.begun)
    {
        op_begin();
        if(s.fr == 0)
            bank_export(hw);
    }
    else if(s.fr == 0)
    {
        const bool loading = ((t0 - s.t0) / kBankPhaseMs) % 2u == 1u;
        if(loading && g_bank_pending == nullptr && static_cast<int32_t>(t0 - s.next) >= 0)
        {
            op_epoch();
            r      = bank_load();
            ran    = true;
            s.next = t0 + kBankEveryMs;
            if(r.fr != 0)
                op_fail(r.fr);
            else if(!r.ok)
                op_fail(-4);   // a bank that does not compare is never swapped in
        }
    }
    op_pad(hw, t0);
    if(ran)
        hw.PrintLine("SHELL_SD_K load_us=%u worst_us=%u cmp=%s held=%u fr=%d slot=%d",
                     static_cast<unsigned>(r.us), static_cast<unsigned>(r.worst),
                     r.ok ? "ok" : "bad", static_cast<unsigned>(g_held_blocks), r.fr, r.slot);
    op_stop_line(hw, 'K');
}
#endif // SHELL_SD_PROBE_BANK

#if SHELL_SD_PROBE_BUFFER
// D2: one part's sampler buffer (sdram_mem.cpp's g_sampler[p], kSamplerFrames
// stereo float frames, 16.1 MB) saved and loaded while the instrument plays.
// The cycle, one operation per pass and 10 s idle after each:
//   f32 save  the scratch filled with pattern() (as float bits), written raw
//             to FFBUF.F32 (16 128 000 B)
//   f32 load  the scratch cleared, the file read back into it, every word
//             compared
//   s16 save  the scratch filled with pattern() mapped to [-1, 1), written as
//             int16 per channel, lrintf(x * 32767), to FFBUF.S16 (8 064 000 B)
//   s16 load  the scratch cleared, the file read and converted back to float
//             (s / 32767), every sample within 1 LSB of the quantised source
// The saves read from and the loads write to the scratch, never a live
// buffer. f32 goes straight from and into the scratch in kOpChunk pieces; s16
// through g_buf, converted piece by piece (the conversion is in ms=, as a
// sampler's loader pays it).
//
// Line (longest: ms 6 digits, worst_us 7, held 5, fr 2, off 8 -- 91 bytes):
//   SHELL_SD_B fmt=<f32|s16> op=<save|load> ms= worst_us= held= fr= cmp=<ok|bad|-> off=
// ms is f_open to f_close (System::GetNow(): an operation may outlast the
// cycle counter's 8.9 s wrap), worst_us the slowest single f_read / f_write;
// cmp is - for a save and for a load that failed; off is the first bad byte
// of the file (0 when none).
//
// The scratch is declared as 32-bit words, the size of one part's
// SampleBuffer::Frame array: every access is a word (floats go in and out
// as bits through memcpy), so nothing reads it through a float lvalue.
constexpr uint32_t kBufWords = static_cast<uint32_t>(kSamplerFrames) * 2u;
constexpr uint32_t kF32Bytes = kBufWords * 4u;
constexpr uint32_t kS16Bytes = kBufWords * 2u;
alignas(32) uint32_t DSY_SDRAM_BSS g_scratch[kBufWords];
static_assert(sizeof(g_scratch) == sizeof(spky::SampleBuffer::Frame) * kSamplerFrames,
              "the scratch is one part's sampler buffer");
// The driver invalidates the D-cache over a transfer rounded out to 32-byte
// lines: every piece, the last one too, starts and ends on a line.
static_assert(kF32Bytes % 32u == 0 && kOpChunk % 32u == 0, "pieces on 32-byte lines");
static_assert(kOpChunk % 4u == 0 && kS16Bytes % 4u == 0, "s16 pieces hold whole word pairs");

const char         kBufF32[]    = "FFBUF.F32";
const char         kBufS16[]    = "FFBUF.S16";
constexpr uint32_t kBufIdleMs   = 10u * 1000u;

// Word i of the s16 source: pattern()'s top 24 bits, mapped to [-1, 1)
// exactly (a 24-bit integer times a power of two needs no rounding).
inline float s16_source(uint32_t i)
{
    return static_cast<float>(pattern(i) >> 8) * (1.f / 8388608.f) - 1.f;
}
inline int32_t s16_quant(float x) { return static_cast<int32_t>(std::lrint(x * 32767.f)); }
inline uint32_t f2w(float x)
{
    uint32_t w;
    std::memcpy(&w, &x, 4);
    return w;
}
inline float w2f(uint32_t w)
{
    float x;
    std::memcpy(&x, &w, 4);
    return x;
}

struct BufOp
{
    const char* fmt   = "f32";
    const char* op    = "save";
    uint32_t    ms    = 0;
    uint32_t    worst = 0;
    int         fr    = 0;
    Check       cmp   = kSkip;   // kSkip prints as "-"
    uint32_t    off   = 0;
};

// Opens the file, runs `body` (which returns 0 or the failure), closes; all
// marked, the time from the open to the close's return in ms.
template <typename Body>
void buf_file(BufOp& r, const char* name, BYTE mode, Body body)
{
    const uint32_t t0 = daisy::System::GetNow();
    OpMark         m;
    r.fr = f_open(&g_fs.file, op_path(name), mode);
    if(r.fr == FR_OK)
    {
        r.fr             = body();
        const FRESULT cr = f_close(&g_fs.file);
        if(r.fr == 0)
            r.fr = static_cast<int>(cr);
    }
    r.ms = daisy::System::GetNow() - t0;
}

void buf_f32_save(BufOp& r)
{
    for(uint32_t i = 0; i < kBufWords; ++i)
        g_scratch[i] = pattern(i);
    op_epoch();
    buf_file(r, kBufF32, FA_CREATE_ALWAYS | FA_WRITE, [&r] {
        return op_write(reinterpret_cast<const uint8_t*>(g_scratch), kF32Bytes, r.worst);
    });
}

void buf_f32_load(BufOp& r)
{
    std::memset(g_scratch, 0, sizeof g_scratch);
    op_epoch();
    buf_file(r, kBufF32, FA_READ, [&r] {
        uint8_t* const dst = reinterpret_cast<uint8_t*>(g_scratch);
        for(uint32_t off = 0; off < kF32Bytes; off += kOpChunk)
        {
            const UINT len = kF32Bytes - off < kOpChunk ? kF32Bytes - off : kOpChunk;
            const int  fr  = op_read_piece(dst + off, len, r.worst);
            if(fr != 0)
                return fr;
        }
        return 0;
    });
    if(r.fr != 0)
        return;
    const int32_t b = first_bad(g_scratch, kBufWords, 0);
    r.cmp           = b < 0 ? kOk : kBad;
    r.off           = b < 0 ? 0u : static_cast<uint32_t>(b);
}

void buf_s16_save(BufOp& r)
{
    for(uint32_t i = 0; i < kBufWords; ++i)
        g_scratch[i] = f2w(s16_source(i));
    op_epoch();
    buf_file(r, kBufS16, FA_CREATE_ALWAYS | FA_WRITE, [&r] {
        // Two samples per g_buf word, little-endian, the first in the low half.
        for(uint32_t off = 0; off < kS16Bytes; off += kOpChunk)
        {
            const uint32_t len = kS16Bytes - off < kOpChunk ? kS16Bytes - off : kOpChunk;
            const uint32_t w0  = off / 2u;   // the scratch word of the piece's first sample
            for(uint32_t j = 0; j < len / 4u; ++j)
            {
                const uint32_t lo = static_cast<uint16_t>(s16_quant(w2f(g_scratch[w0 + 2u * j])));
                const uint32_t hi = static_cast<uint16_t>(s16_quant(w2f(g_scratch[w0 + 2u * j + 1u])));
                g_buf[j]          = lo | (hi << 16);
            }
            const int fr = op_write(reinterpret_cast<const uint8_t*>(g_buf), len, r.worst);
            if(fr != 0)
                return fr;
        }
        return 0;
    });
}

void buf_s16_load(BufOp& r)
{
    std::memset(g_scratch, 0, sizeof g_scratch);
    op_epoch();
    buf_file(r, kBufS16, FA_READ, [&r] {
        for(uint32_t off = 0; off < kS16Bytes; off += kOpChunk)
        {
            const UINT len = kS16Bytes - off < kOpChunk ? kS16Bytes - off : kOpChunk;
            const int  fr  = op_read_piece(reinterpret_cast<uint8_t*>(g_buf), len, r.worst);
            if(fr != 0)
                return fr;
            const uint32_t w0 = off / 2u;
            for(uint32_t j = 0; j < len / 4u; ++j)
            {
                const int16_t lo = static_cast<int16_t>(g_buf[j] & 0xFFFFu);
                const int16_t hi = static_cast<int16_t>(g_buf[j] >> 16);
                g_scratch[w0 + 2u * j]      = f2w(lo * (1.f / 32767.f));
                g_scratch[w0 + 2u * j + 1u] = f2w(hi * (1.f / 32767.f));
            }
        }
        return 0;
    });
    if(r.fr != 0)
        return;
    r.cmp = kOk;
    for(uint32_t i = 0; i < kBufWords; ++i)
    {
        const int32_t d = s16_quant(w2f(g_scratch[i])) - s16_quant(s16_source(i));
        if(d > 1 || d < -1)
        {
            r.cmp = kBad;
            r.off = i * 2u;
            break;
        }
    }
}

uint32_t g_buf_step = 0;   // the cycle's next operation, 0..3

void buffer_pass(bench::Board& hw)
{
    OpRun&         s   = g_op;
    const uint32_t t0  = daisy::System::GetNow();
    bool           ran = false;
    BufOp          r;
    if(!s.begun)
        op_begin();
    if(s.fr == 0 && static_cast<int32_t>(t0 - s.next) >= 0)
    {
        const uint32_t step = g_buf_step;
        r.fmt = step < 2 ? "f32" : "s16";
        r.op  = step % 2 == 0 ? "save" : "load";
        switch(step)
        {
            case 0: buf_f32_save(r); break;
            case 1: buf_f32_load(r); break;
            case 2: buf_s16_save(r); break;
            default: buf_s16_load(r); break;
        }
        ran        = true;
        g_buf_step = (step + 1u) % 4u;
        s.next     = daisy::System::GetNow() + kBufIdleMs;
        if(r.fr != 0)
            op_fail(r.fr);
        else if(r.cmp == kBad)
            op_fail(-4);   // the bytes did not come back: the cycle stops on it
    }
    op_pad(hw, t0);
    if(ran)
        hw.PrintLine("SHELL_SD_B fmt=%s op=%s ms=%u worst_us=%u held=%u fr=%d cmp=%s off=%u",
                     r.fmt, r.op, static_cast<unsigned>(r.ms), static_cast<unsigned>(r.worst),
                     static_cast<unsigned>(g_held_blocks), r.fr,
                     r.cmp == kSkip ? "-" : check_name(r.cmp), static_cast<unsigned>(r.off));
    op_stop_line(hw, 'B');
}
#endif // SHELL_SD_PROBE_BUFFER

#if SHELL_SD_PROBE_PATCH
// D4: the persisted panel record (persist_latest(), the newest valid one in
// the QSPI sectors, read memory-mapped) copied to PATCH.FFP with f_sync, read
// back and compared, kPatchRuns times, one per second. It reads the QSPI
// record and never writes it: this is a listener image, persist_write() is not
// linked (check_images_build.py holds that), and the copy stays on the card as
// the backup D5 needs. After the last run `SHELL_SD_P done n= ok=` every 10 s.
//
// Line (longest: i 3 digits, the three times 7, seq 10, held 4, fr 2 -- 104
// bytes; 119 with every number but i at ten digits):
//   SHELL_SD_P i= write_us= sync_us= read_us= cmp=<ok|bad|-> seq= held= fr=
// write_us is f_open (create) and f_write, sync_us the f_sync, read_us the
// second f_open, the f_read and its f_close; seq the record's. -3: no valid
// record to copy.
const char         kPatchName[]  = "PATCH.FFP";
constexpr uint32_t kPatchRuns    = 100;
constexpr uint32_t kPatchEveryMs = 1000;
constexpr uint32_t kRecBytes     = sizeof(PersistRecord);
// The record at g_buf's start, the read-back kPatchBack words in: both on
// 32-byte lines, neither overlapping the other.
constexpr uint32_t kPatchBack    = 4096u / 4u;
static_assert(kRecBytes <= kPatchBack * 4u && kPatchBack * 4u + kRecBytes <= kChunk,
              "the record and its read-back fit g_buf");

uint32_t g_patch_i  = 0;
uint32_t g_patch_ok = 0;

struct PatchRun
{
    uint32_t write_us = 0, sync_us = 0, read_us = 0, seq = 0;
    int      fr       = 0;
    Check    cmp      = kSkip;
};

PatchRun patch_once()
{
    PatchRun       r;
    const SlotPick pick = persist_latest();   // memory-mapped read, never a write
    if(pick.sector < 0)
    {
        r.fr = -3;
        return r;
    }
    r.seq = pick.seq;
    const uint32_t at = (pick.sector == 0 ? kPersistSector0 : kPersistSector1)
                        + static_cast<uint32_t>(pick.slot) * kPersistStride;
    std::memcpy(g_buf, reinterpret_cast<const void*>(at), kRecBytes);
    uint8_t* const back = reinterpret_cast<uint8_t*>(g_buf + kPatchBack);
    std::memset(back, 0, kRecBytes);
    op_epoch();
    {
        OpMark   m;
        uint32_t c0 = cycles_now();
        r.fr        = f_open(&g_fs.file, op_path(kPatchName), FA_CREATE_ALWAYS | FA_WRITE);
        if(r.fr == FR_OK)
        {
            UINT bw = 0;
            r.fr    = f_write(&g_fs.file, g_buf, kRecBytes, &bw);
            if(r.fr == FR_OK && bw != kRecBytes)
                r.fr = -2;
            r.write_us = us_since(c0);
            if(r.fr == FR_OK)
            {
                c0        = cycles_now();
                r.fr      = f_sync(&g_fs.file);
                r.sync_us = us_since(c0);
            }
            const FRESULT cr = f_close(&g_fs.file);
            if(r.fr == FR_OK)
                r.fr = cr;
        }
        if(r.fr == FR_OK)
        {
            c0   = cycles_now();
            r.fr = f_open(&g_fs.file, g_path, FA_READ);
            if(r.fr == FR_OK)
            {
                UINT br = 0;
                r.fr    = f_read(&g_fs.file, back, kRecBytes, &br);
                if(r.fr == FR_OK && br != kRecBytes)
                    r.fr = -2;
                const FRESULT cr = f_close(&g_fs.file);
                if(r.fr == FR_OK)
                    r.fr = cr;
            }
            r.read_us = us_since(c0);
        }
    }
    if(r.fr == FR_OK)
        r.cmp = std::memcmp(g_buf, back, kRecBytes) == 0 ? kOk : kBad;
    return r;
}

void patch_pass(bench::Board& hw)
{
    OpRun&         s   = g_op;
    const uint32_t t0  = daisy::System::GetNow();
    bool           ran = false;
    PatchRun       r;
    uint32_t       i = 0;
    if(!s.begun)
        op_begin();
    if(s.fr == 0 && g_patch_i < kPatchRuns && static_cast<int32_t>(t0 - s.next) >= 0)
    {
        i      = g_patch_i++;
        r      = patch_once();
        ran    = true;
        s.next = t0 + kPatchEveryMs;
        if(r.fr != 0)
            op_fail(r.fr);
        else if(r.cmp == kOk)
            ++g_patch_ok;
    }
    op_pad(hw, t0);
    if(ran)
        hw.PrintLine("SHELL_SD_P i=%u write_us=%u sync_us=%u read_us=%u cmp=%s seq=%u held=%u fr=%d",
                     static_cast<unsigned>(i), static_cast<unsigned>(r.write_us),
                     static_cast<unsigned>(r.sync_us), static_cast<unsigned>(r.read_us),
                     r.cmp == kSkip ? "-" : check_name(r.cmp), static_cast<unsigned>(r.seq),
                     static_cast<unsigned>(g_held_blocks), r.fr);
    if(s.fr == 0 && g_patch_i >= kPatchRuns
       && daisy::System::GetNow() - s.line_t0 >= kOpLineMs)
    {
        s.line_t0 = daisy::System::GetNow();
        hw.PrintLine("SHELL_SD_P done n=%u ok=%u", static_cast<unsigned>(g_patch_i),
                     static_cast<unsigned>(g_patch_ok));
    }
    op_stop_line(hw, 'P');
}
#endif // SHELL_SD_PROBE_PATCH

} // namespace

#if SHELL_SD_PROBE_BANK
void sd_bank_block()
{
    const int16_t* const p = g_bank_pending;
    if(p != nullptr)
    {
        spky::wt::set_bank(p);
        g_bank_pending = nullptr;
    }
}
#endif
#endif // SD_PROBE_OPS

#if SHELL_SD_PROBE_DUPLEX
// Spec 2026-10-31-sd-use-cases-tests-design.md section 5, D3: idea 2 (a
// sample streamed from the card) and master recording, end to end and at
// once, through two rings in SDRAM. Every block the audio callback
// (sd_duplex_block(), from panel_scan_gate()) takes 96 stereo float frames
// (768 B, 384 KB/s) out of the read ring and sums them into a volatile
// checksum -- not played -- and puts the master output, after the boot gate, as
// 24-bit packed stereo (576 B, 288 KB/s) into the write ring. A block the read
// ring cannot feed in full is an underrun, one the write ring has no room for
// an overrun; either is skipped whole, not done in part.
//
// The foreground keeps both rings going for the whole pass: it refills the
// read ring from FFSDBIG.DAT (512 MiB, left on the card by rtail's fill; it
// is not written here, and a missing file stops the mode with fr=4,
// FR_NO_FILE) in 16 KB reads while the ring is below half -- one read per
// turn of the pass's loop, as many as it takes to get back to half, each
// ahead of any write -- and drains the write ring to FFREC.DAT (truncated at
// the start) in 16 KB writes while at least 16 KB is queued and the read ring
// is at half or above; an underrun is the one heard.
// Both files wrap: the read at the file's last whole chunk, the record before
// the FAT32 4 GiB limit. One FatFs and one bus serve both, so a stalled write
// stops the refills too: the read ring is sized for that (kReadRing). The
// first pass opens the files and fills the read ring (72 reads) before the
// callback starts taking (read_sd.py does not judge that pass's window and
// frame).
//
// Every card access, f_open, f_read, f_write, f_lseek and f_close, is marked
// for the pot hold (DxMark, as OpMark and TailMark) with A1's tail of 0 (A1
// passed: the hold with tail 0, not back to back).
//
// Line, every pass (longest realistic: w 5 digits, under and over 4 -- a pass
// that a 2 s card stall stretched counts ~1 000 blocks --, rfill and wfill 7,
// the worst calls 7, held 4, fr 2 -- 121 of the 128 bytes):
//   SHELL_SD_D w= under= over= rfill= wfill= worst_r_us= worst_w_us= held= fr=
// w: the raw window counter at the pass's start; under / over: blocks of this
// pass; rfill / wfill: the read ring's lowest and the write ring's highest
// fill this pass in bytes, as the callback left them after its take and put;
// worst_r_us / worst_w_us: the slowest f_read / f_write of the pass; held: the
// blocks the pot hold dropped in it; fr: the first failure, sticky (a FRESULT,
// -2 a short transfer, -3 no card test to start from). From then on nothing is
// transferred, the callback stops taking and counting, and the line keeps
// saying why.
namespace {

// 16 KB per f_read and f_write, not 32 KB: A3 (docs/hardware/
// sd-use-cases-measured.md section 4) measured 32 KB transfers costing the
// audio callback more than 16 KB ones on every workload.
constexpr uint32_t kDxChunk = 16u * 1024u;
// The rings, from C1/C2, board session 2, 2026-11-01 (docs/hardware/
// sd-use-cases-measured.md, C1/C2 rows). Read: one foreground thread serves
// both rings, so the read ring has to outlast the longest blocking call of
// either stream -- C2's worst write stall (1.97 s; 2 x 1.97 s x 288 KB/s
// rounds to 1152 KB, and at 384 KB/s 1152 KB still holds 3.0 s) as well as
// C1's read tail (a 32 KB ring at 384 KB/s, worst f_read 24.5 ms) -- and the
// report loop's prints between two passes, when nothing refills it.
constexpr uint32_t kReadRing = 1152u * 1024u;
// Write: the largest ring C2 printed over all write patterns and runs -- one
// ~2 s card stall in a 16-bit run (worst f_write 1 969 428 us). A card's
// garbage-collection stall does not depend on the sample format, so it can hit
// these 24-bit writes too.
constexpr uint32_t kWriteRing = 1152u * 1024u;
// One block: 96 frames (48 kHz / 500 blocks a second), float stereo out of
// the read ring, 24-bit stereo into the write ring.
constexpr uint32_t kDxFrames     = 96;
constexpr uint32_t kDxReadBytes  = kDxFrames * 2u * 4u;   // 768
constexpr uint32_t kDxWriteBytes = kDxFrames * 2u * 3u;   // 576
// Refills and drains move whole chunks, so neither ever wraps inside a ring.
static_assert(kReadRing % kDxChunk == 0 && kWriteRing % kDxChunk == 0,
              "a ring holds whole chunks");
static_assert(kReadRing / 2u >= kDxChunk, "a refill below half fits");
static_assert(kDxChunk <= kChunk, "g_buf holds one chunk");
// FAT32 files end at 4 GiB - 1: the record wraps to 0 before the last chunk
// that fits (about 4 h at 288 KB/s; the 10 min run never gets there).
constexpr uint32_t kRecWrap = 0xFFFFFFFFu - (0xFFFFFFFFu % kDxChunk);

const char kDxReadName[] = "FFSDBIG.DAT";
const char kDxRecName[]  = "FFREC.DAT";

// One byte ring, one producer and one consumer, on one core: the callback is
// an interrupt of the foreground, so a compiler fence between the bytes and
// the position that publishes or frees them is the whole ordering. put and
// take run over [0, 2 x size) (the mirror trick), so a full ring and an empty
// one differ; each is written by its own side only.
struct DxRing
{
    uint8_t*          buf;
    uint32_t          size;
    volatile uint32_t put;    // the producer's
    volatile uint32_t take;   // the consumer's
};

alignas(32) uint8_t DSY_SDRAM_BSS g_rring_buf[kReadRing];
alignas(32) uint8_t DSY_SDRAM_BSS g_wring_buf[kWriteRing];
DxRing g_rring = {g_rring_buf, kReadRing, 0, 0};    // foreground puts, callback takes
DxRing g_wring = {g_wring_buf, kWriteRing, 0, 0};   // callback puts, foreground takes

inline uint32_t dx_fill(const DxRing& r)
{
    const uint32_t p = r.put, t = r.take;
    return p >= t ? p - t : p + 2u * r.size - t;
}
inline uint32_t dx_adv(const DxRing& r, uint32_t x, uint32_t n)
{
    x += n;
    return x >= 2u * r.size ? x - 2u * r.size : x;
}
inline uint32_t dx_at(const DxRing& r, uint32_t x) { return x >= r.size ? x - r.size : x; }

// n bytes in at the producer's end; the caller has checked the room.
void dx_put(DxRing& r, const void* src, uint32_t n)
{
    const uint32_t at = dx_at(r, r.put);
    const uint32_t a  = r.size - at < n ? r.size - at : n;
    std::memcpy(r.buf + at, src, a);
    std::memcpy(r.buf, static_cast<const uint8_t*>(src) + a, n - a);
    std::atomic_signal_fence(std::memory_order_seq_cst);   // the bytes, then the position
    r.put = dx_adv(r, r.put, n);
}
// n bytes out at the consumer's end, not yet freed; the caller has checked
// the fill.
void dx_peek(const DxRing& r, void* dst, uint32_t n)
{
    const uint32_t at = dx_at(r, r.take);
    const uint32_t a  = r.size - at < n ? r.size - at : n;
    std::memcpy(dst, r.buf + at, a);
    std::memcpy(static_cast<uint8_t*>(dst) + a, r.buf, n - a);
}
void dx_release(DxRing& r, uint32_t n)
{
    std::atomic_signal_fence(std::memory_order_seq_cst);   // read out, then freed
    r.take = dx_adv(r, r.take, n);
}

// The record file's FIL beside g_fs.file (which reads FFSDBIG.DAT), in AXI
// SRAM and on its own 32-byte lines for the same reason as FsBlock: its
// sector buffer is a DMA target.
struct alignas(32) DxRec
{
    FIL fil;
};
DxRec g_rec;

// Every card access of this mode, as OpMark: busy for the call, seen again
// when it returns.
struct DxMark
{
    DxMark()
    {
        g_sd_seen = true;
        g_sd_busy = true;
    }
    ~DxMark()
    {
        g_sd_seen = true;
        g_sd_busy = false;
    }
};

struct DxRun
{
    bool     begun      = false;
    int      fr         = 0;   // the first failure, sticky
    FSIZE_t  read_end   = 0;   // FFSDBIG.DAT's size, down to a whole chunk
    uint32_t under_said = 0;   // g_dx_under at the last line
    uint32_t over_said  = 0;
};
DxRun g_dx;

// Set by the foreground once the files are open and the read ring is full;
// cleared on a failure, after which the callback neither takes nor counts.
volatile bool g_dx_run = false;
// The callback's: since the start; the line prints the difference.
volatile uint32_t g_dx_under = 0;
volatile uint32_t g_dx_over  = 0;
// The callback's, per pass: started over at the first block that sees a new
// g_dx_epoch (bumped by the foreground after it has read them, as the hold's
// counters). The start value differs from every epoch's, so the first block
// starts them from its own fills, not from 0.
volatile uint32_t g_dx_rmin     = 0;
volatile uint32_t g_dx_wmax     = 0;
volatile uint32_t g_dx_epoch    = 0;
uint32_t          g_dx_cb_epoch = 0xFFFFFFFFu;
// The frames taken, summed as words: the file holds the bring-up's pattern,
// whose words read as floats include NaNs and denormals, and the point is the
// read out of SDRAM, not the add.
volatile uint32_t g_dx_sum = 0;
// The callback's block buffers (in .bss, not on the interrupt's stack).
uint32_t g_dx_frames[kDxReadBytes / 4u];
uint8_t  g_dx_pack[kDxWriteBytes];

// One sample as 24-bit little-endian, clamped to [-1, 1] first (a NaN goes
// to +1); returns the next byte.
inline uint8_t* dx_pack24(uint8_t* p, float x)
{
    x               = x < 1.f ? x : 1.f;
    x               = x > -1.f ? x : -1.f;
    const int32_t v = static_cast<int32_t>(x * 8388607.f);
    p[0]            = static_cast<uint8_t>(v);
    p[1]            = static_cast<uint8_t>(v >> 8);
    p[2]            = static_cast<uint8_t>(v >> 16);
    return p + 3;
}

void dx_fail(int fr)
{
    if(g_dx.fr == 0)
        g_dx.fr = fr;
    g_dx_run = false;
}

// One chunk of FFSDBIG.DAT into the read ring, through g_buf (the driver's
// DMA and cache maintenance want a 32-byte-aligned buffer of its own).
void dx_refill(uint32_t& worst)
{
    FRESULT  fr = FR_OK;
    UINT     br = 0;
    uint32_t dt = 0;
    {
        DxMark m;
        if(f_tell(&g_fs.file) + kDxChunk > g_dx.read_end)
            fr = f_lseek(&g_fs.file, 0);
        if(fr == FR_OK)
        {
            const uint32_t c0 = cycles_now();
            fr                = f_read(&g_fs.file, g_buf, kDxChunk, &br);
            dt                = (cycles_now() - c0) / kCoreMhz;
        }
    }
    if(dt > worst)
        worst = dt;
    if(fr != FR_OK || br != kDxChunk)
    {
        dx_fail(fr != FR_OK ? static_cast<int>(fr) : -2);
        return;
    }
    dx_put(g_rring, g_buf, kDxChunk);
}

// The write ring's oldest chunk to FFREC.DAT, through g_buf. The chunk is
// freed only when the f_write has returned, as a recorder that writes straight
// out of the ring would free it.
void dx_drain(uint32_t& worst)
{
    dx_peek(g_wring, g_buf, kDxChunk);
    FRESULT  fr = FR_OK;
    UINT     bw = 0;
    uint32_t dt = 0;
    {
        DxMark m;
        if(f_tell(&g_rec.fil) >= kRecWrap)
            fr = f_lseek(&g_rec.fil, 0);
        if(fr == FR_OK)
        {
            const uint32_t c0 = cycles_now();
            fr                = f_write(&g_rec.fil, g_buf, kDxChunk, &bw);
            dt                = (cycles_now() - c0) / kCoreMhz;
        }
    }
    if(dt > worst)
        worst = dt;
    if(fr != FR_OK || bw != kDxChunk)
    {
        dx_fail(fr != FR_OK ? static_cast<int>(fr) : -2);
        return;
    }
    dx_release(g_wring, kDxChunk);
}

// The first pass: the bring-up's test file closed, both files opened, the
// read ring filled, then the callback starts. No card test to start from is
// fr -3, as in the other modes.
void dx_begin(uint32_t& worst_r)
{
    g_dx.begun = true;
    if(!g_res.ready)
    {
        dx_fail(-3);
        return;
    }
    char    path[24];
    FRESULT fr;
    snprintf(path, sizeof path, "%s%s", g_fs.fsi.GetSDPath(), kDxReadName);
    {
        DxMark m;
        f_close(&g_fs.file);   // the bring-up's test file, open for the stream
        g_res.ready = false;
        fr          = f_open(&g_fs.file, path, FA_READ);
    }
    if(fr != FR_OK)
    {
        dx_fail(static_cast<int>(fr));   // FR_NO_FILE: rtail never filled this card
        return;
    }
    const FSIZE_t size = f_size(&g_fs.file);
    g_dx.read_end      = size - size % kDxChunk;
    snprintf(path, sizeof path, "%s%s", g_fs.fsi.GetSDPath(), kDxRecName);
    {
        DxMark m;
        fr = f_open(&g_rec.fil, path, FA_CREATE_ALWAYS | FA_WRITE);
    }
    if(fr != FR_OK)
    {
        dx_fail(static_cast<int>(fr));
        return;
    }
    while(g_dx.fr == 0 && dx_fill(g_rring) < kReadRing)
        dx_refill(worst_r);
    if(g_dx.fr == 0)
        g_dx_run = true;
}

} // namespace

void sd_duplex_block(const float* l, const float* r, size_t n)
{
    if(!g_dx_run)
        return;
    if(n > kDxFrames)
        n = kDxFrames;
    // The stream: n frames out of the read ring, or an underrun.
    const uint32_t need_r = static_cast<uint32_t>(n) * 8u;
    if(dx_fill(g_rring) >= need_r)
    {
        dx_peek(g_rring, g_dx_frames, need_r);
        dx_release(g_rring, need_r);
        uint32_t s = g_dx_sum;
        for(uint32_t i = 0; i < need_r / 4u; ++i)
            s += g_dx_frames[i];
        g_dx_sum = s;
    }
    else
        g_dx_under = g_dx_under + 1u;
    // The record: n frames of the output as 24-bit little-endian, left then
    // right (dx_pack24()), or an overrun.
    const uint32_t need_w = static_cast<uint32_t>(n) * 6u;
    if(g_wring.size - dx_fill(g_wring) >= need_w)
    {
        uint8_t* p = g_dx_pack;
        for(size_t i = 0; i < n; ++i)
        {
            p = dx_pack24(p, l[i]);
            p = dx_pack24(p, r[i]);
        }
        dx_put(g_wring, g_dx_pack, need_w);
    }
    else
        g_dx_over = g_dx_over + 1u;
    const uint32_t rf    = dx_fill(g_rring);
    const uint32_t wf    = dx_fill(g_wring);
    const uint32_t epoch = g_dx_epoch;
    if(epoch != g_dx_cb_epoch)
    {
        g_dx_cb_epoch = epoch;
        g_dx_rmin     = rf;
        g_dx_wmax     = wf;
    }
    else
    {
        if(rf < g_dx_rmin)
            g_dx_rmin = rf;
        if(wf > g_dx_wmax)
            g_dx_wmax = wf;
    }
}

static void duplex_pass(bench::Board& hw, const volatile uint32_t& win)
{
    const uint32_t w0      = win;
    const uint32_t t0      = daisy::System::GetNow();
    uint32_t       worst_r = 0, worst_w = 0;
    if(!g_dx.begun)
        dx_begin(worst_r);
    // Until the 500 ms are up; a transfer that stalls makes the pass longer.
    while(g_dx.fr == 0 && daisy::System::GetNow() - t0 < kPassMs)
    {
        if(dx_fill(g_rring) < kReadRing / 2u)
            dx_refill(worst_r);
        else if(dx_fill(g_wring) >= kDxChunk)
            dx_drain(worst_w);
        else
            daisy::System::Delay(1);
    }
    // The pass stays 500 ms long when it stopped early (a failure).
    const uint32_t spent = daisy::System::GetNow() - t0;
    if(spent < kPassMs)
        hw.Delay(kPassMs - spent);
    // The pass's figures, then the callback starts the per-pass ones over.
    const uint32_t under = g_dx_under;
    const uint32_t over  = g_dx_over;
    const uint32_t rmin  = g_dx_rmin;
    const uint32_t wmax  = g_dx_wmax;
    const uint32_t held  = g_held_blocks;
    g_dx_epoch           = g_dx_epoch + 1u;
    g_pass_epoch         = g_pass_epoch + 1u;
    hw.PrintLine("SHELL_SD_D w=%u under=%u over=%u rfill=%u wfill=%u worst_r_us=%u "
                 "worst_w_us=%u held=%u fr=%d",
                 static_cast<unsigned>(w0), static_cast<unsigned>(under - g_dx.under_said),
                 static_cast<unsigned>(over - g_dx.over_said), static_cast<unsigned>(rmin),
                 static_cast<unsigned>(wmax), static_cast<unsigned>(worst_r),
                 static_cast<unsigned>(worst_w), static_cast<unsigned>(held), g_dx.fr);
    g_dx.under_said = under;
    g_dx.over_said  = over;
}
#endif // SHELL_SD_PROBE_DUPLEX

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
#elif SHELL_SD_PROBE_DUPLEX
    duplex_pass(hw, win);
#elif SD_PROBE_OPS
    // The operation modes keep their schedule in wall time, not raw windows.
    static_cast<void>(&win);
#if SHELL_SD_PROBE_BANK
    bank_pass(hw);
#elif SHELL_SD_PROBE_BUFFER
    buffer_pass(hw);
#else
    patch_pass(hw);
#endif
#else
#error "this SHELL_SD_PROBE mode has no pass yet (shell/sd_probe.cpp)"
#endif
}

} // namespace shell
