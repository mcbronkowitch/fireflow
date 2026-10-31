#include "sd_probe.h"

#include <cstdio>
#include <cstring>

#include "cycles.h"
#include "per/sdmmc.h"
#include "sd_schedule.h"
#include "shell_sd_probe.h"
#include "sys/fatfs.h"

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
// therefore read 16 KB chunks, the sdram ones keep 32 KB (the 11.2 pattern);
// the SHELL_SD_S line says which with chunk=.
constexpr uint32_t kChunkAxi = 16u * 1024u;
alignas(32) uint32_t g_buf_axi[kChunkAxi / 4];
static_assert(sizeof(g_buf_axi) % 32 == 0, "the invalidate rounds out to 32-byte lines");
#endif
// Both chunks divide the file, so a pass that starts on a chunk boundary of its
// own size wraps exactly at the end. The positions the two arms leave behind
// differ (an axi phase can stop at an odd multiple of 16 KB, which is not a
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
#else
    return 0;
#endif
}

bool sd_hold_block()
{
#if SHELL_SD_PROBE_BURST
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

#if SHELL_SD_PROBE_STREAM
static void stream_pass(bench::Board& hw, const volatile uint32_t& win)
{
    const uint32_t w0 = win;
    const bool     on = g_res.ready && ((w0 / kPhaseWin) & 1u) != 0;
    // Test A3: the on-phases alternate the transfer buffer, SDRAM, AXI, SDRAM,
    // ... A phase is kPhaseWin windows and on-phases are the odd ones, so
    // w / (2 * kPhaseWin) counts them. A pass stays inside its phase (the loop
    // below ends when the window counter leaves it), so one pass, one buffer.
    const bool       axi = ((w0 / (2u * kPhaseWin)) & 1u) != 0;
    uint32_t* const  buf   = axi ? g_buf_axi : g_buf;
    const uint32_t   chunk = axi ? kChunkAxi : kChunk;   // the AXI buffer is 16 KB, see g_buf_axi
    const uint32_t t0 = daisy::System::GetNow();
    uint32_t       bytes = 0, worst = 0, bad = 0, ms = 0;
    if(on)
    {
        // The previous on-phase may have used the other chunk size and left the
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
#elif SHELL_SD_PROBE_STREAM
    stream_pass(hw, win);
#else
#error "this SHELL_SD_PROBE mode has no pass yet (shell/sd_probe.cpp)"
#endif
}

} // namespace shell
