#include "sd_probe.h"

#include <cstdio>

#include "cycles.h"
#include "per/sdmmc.h"
#include "sys/fatfs.h"

namespace shell {
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

const char kName[] = "FFSDTEST.BIN";

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
int32_t first_bad(uint32_t word0)
{
    for(uint32_t i = 0; i < kChunk / 4; ++i)
        if(g_buf[i] != pattern(word0 + i))
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
        const int32_t b = first_bad(off / 4);
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
    // verify line with every field at ten digits, is 107 bytes.
    hw.PrintLine("SHELL_SD mount=%d fs=%d total_mib=%u free_mib=%u bus=4 mhz=%d ps=%d",
                 g_res.mount_fr, g_res.fs_type, static_cast<unsigned>(g_res.total_mib),
                 static_cast<unsigned>(g_res.free_mib), kMhz, kClockPowerSave ? 1 : 0);
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

void sd_stream_pass(bench::Board& hw, const volatile uint32_t& win)
{
    const uint32_t w0 = win;
    const bool     on = g_res.ready && ((w0 / kPhaseWin) & 1u) != 0;
    const uint32_t t0 = daisy::System::GetNow();
    uint32_t       bytes = 0, worst = 0, bad = 0, ms = 0;
    if(on)
    {
        // Until the 500 ms are up or the window counter leaves the phase; at
        // most one chunk (a few ms) spills into the next phase's first window,
        // which read_sd.py does not judge.
        while(daisy::System::GetNow() - t0 < kPassMs && ((win / kPhaseWin) & 1u) != 0)
        {
            FRESULT fr = FR_OK;
            if(f_tell(&g_fs.file) >= kFileBytes)
                fr = f_lseek(&g_fs.file, 0);
            const uint32_t word0 = static_cast<uint32_t>(f_tell(&g_fs.file) / 4u);
            UINT           br    = 0;
            const uint32_t u0    = cycles_now();
            if(fr == FR_OK)
                fr = f_read(&g_fs.file, g_buf, kChunk, &br);
            const uint32_t dt = (cycles_now() - u0) / kCoreMhz;
            if(fr != FR_OK || br != kChunk)
            {
                // Stops the stream for good; the line keeps saying why.
                g_res.stream_fr = fr != FR_OK ? static_cast<int>(fr) : -2;
                g_res.ready     = false;
                break;
            }
            bytes += br;
            if(dt > worst)
                worst = dt;
            if(first_bad(word0) >= 0)
                ++bad;
        }
        ms = daisy::System::GetNow() - t0;
    }
    // The pass stays 500 ms long either way, so the frame rate does not
    // change with the phase.
    const uint32_t spent = daisy::System::GetNow() - t0;
    if(spent < kPassMs)
        hw.Delay(kPassMs - spent);
    hw.PrintLine("SHELL_SD_S w=%u on=%d kib=%u ms=%u kib_s=%u worst_us=%u bad=%u fr=%d",
                 static_cast<unsigned>(w0), on ? 1 : 0, static_cast<unsigned>(bytes / 1024u),
                 static_cast<unsigned>(ms),
                 static_cast<unsigned>(ms == 0 ? 0 : static_cast<uint64_t>(bytes) * 1000u / 1024u / ms),
                 static_cast<unsigned>(worst), static_cast<unsigned>(bad), g_res.stream_fr);
}

} // namespace shell
