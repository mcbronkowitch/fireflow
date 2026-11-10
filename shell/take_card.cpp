#include "take_card.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "cycles.h"
#include "per/sdmmc.h"
#include "sampler/sampler_config.h"
#include "sd_hold.h"
#include "sdram_mem.h"
#include "shell_takes.h"
#include "shell_takes_stress.h"
#include "sys/fatfs.h"
#include "take_file.h"
#include "take_policy.h"

// Sampler takes on the SD card, the firmware half (take_card.h). Spec
// docs/superpowers/specs/2026-11-06-sampler-takes-on-sd-design.md; plan
// docs/superpowers/plans/2026-11-06-sampler-takes-on-sd.md, Task 8.

#if !SHELL_TAKES
#error "take_card.cpp is linked with SHELL_TAKES=1 only (shell/Makefile)"
#endif
// The stress boot patch depends on unowned pots and measures the CPU: no
// card traffic there (spec section 4.8). It arrives through C_USER_FLAGS, so
// the Makefile cannot see it.
#if defined(SHELL_BOOT_PATCH) && SHELL_BOOT_PATCH != 0
#error "SHELL_TAKES=1 excludes SHELL_BOOT_PATCH: card traffic must not land in a measurement"
#endif

namespace shell {

static_assert(kTakeMaxFrames == kSamplerFrames, "a slot holds exactly one deck's buffer");
static_assert(kTakeMaxMarkers == spky::sampler_cfg::kMaxSlices, "a slot holds the whole slice map");
static_assert(kTakePieceBytes % 32 == 0 && kTakeHeaderBytes % 32 == 0,
              "pieces start and end on a D-cache line (spec section 4.3, D-cache)");
static_assert(kTakePieceBytes % 512 == 0 && kTakeHeaderBytes % 512 == 0,
              "pieces start on a sector, so FatFs moves them without its sector buffer");

namespace {

// ---- Constants ----

constexpr uint32_t kBlockMs        = 2;      // 96 samples at 48 kHz
constexpr uint32_t kLoadDeadlineBlocks = 10000u / kBlockMs;   // 10 s per deck (spec 4.4)
constexpr uint32_t kReportEvery    = 10;     // passes (5 s) between the latched lines
constexpr uint32_t kMailTimeoutMs  = 1000;   // the callback answers within a block
constexpr uint32_t kClmtWords      = 64;     // fast-seek map: 31 fragments
constexpr int      kSlots          = 2;

// libDaisy's default, 50 MHz, and the bus clock stopped between transfers
// (CLKCR.PWRSAV), as sd_probe.cpp kClockPowerSave: with it running, SENSE_2/3
// read ~95 counts of idle spread against 14 (2026-10-31).
constexpr daisy::SdmmcHandler::Speed kSpeed = daisy::SdmmcHandler::Speed::FAST;

#if SHELL_TAKES_STRESS
constexpr uint32_t kStressFrames      = 10u * kTakeSampleRate;   // 10 s pattern
constexpr uint32_t kStressEveryBlocks = 30000u / kBlockMs;      // a forced save per 30 s
#endif

// ---- FatFs objects ----

// One 32-byte-aligned block in AXI SRAM (.bss), as sd_probe.cpp's FsBlock:
// FATFS's sector window, FIL's sector buffer and the JUNK header are DMA
// targets, and sd_diskio.c invalidates the D-cache over a transfer rounded out
// to 32-byte lines -- a neighbour the audio interrupt wrote during it would
// lose that write.
struct alignas(32) FsBlock
{
    daisy::FatFSInterface fsi;
    FIL                   file;
    alignas(32) uint8_t   junk[32];
};
FsBlock             g_fs;
daisy::SdmmcHandler g_sdmmc;
DWORD               g_clmt[kClmtWords];
bool                g_open = false;   // g_fs.file is open
char                g_path[2][kSlots][48];

// The header block, a DMA source and target, in SDRAM: AXI SRAM is the
// SRAM_EXEC region the play image is short of, and the SDMMC moves SDRAM as
// D2 measured it (sd_probe.cpp g_buf). A plain array, no constructor
// (.sdram_bss is NOLOAD, sdram_mem.cpp); written before every read of it.
alignas(32) uint8_t DSY_SDRAM_BSS g_hdr[kTakeHeaderBytes];

// The load's meta and the save's: never both at once (the load runs at boot,
// before the first save). A restore's markers point into it until the
// callback has applied the restore.
TakeMeta g_meta;

// ---- Mailbox (foreground -> callback) ----

enum : uint32_t
{
    kMailNone    = 0,
    kMailBegin   = 1,   // sampler_begin_load
    kMailRestore = 2,   // sampler_restore(frames, markers)
    kMailAbort   = 3,   // sampler_abort_load
};

struct Mail
{
    volatile uint32_t op = kMailNone;   // the foreground sets it last, the callback clears it last
    uint32_t          frames = 0;
    int               n      = 0;
    const uint32_t*   mf     = nullptr;
    const uint8_t*    ms     = nullptr;
    volatile bool     ok     = false;
    volatile uint32_t gen    = 0;       // the deck's generation right after the request
};
Mail g_mail[2];

// ---- Published by the callback, once per block ----

// A sequence count around the four fields per deck: the foreground reads them
// between two equal counts (the callback interrupts the foreground, never the
// other way, so a torn read shows as a changed count and is retried).
volatile uint32_t g_view_seq = 0;
volatile uint32_t g_view_gen[2]    = {0, 0};
volatile bool     g_view_rec[2]    = {false, false};
volatile bool     g_view_load[2]   = {false, false};
volatile uint32_t g_view_frames[2] = {0, 0};
volatile uint32_t g_block_tick     = 0;
// For export_markers() only, which the foreground calls on an idle deck
// under the sequence lock (spec 4.3, "Abort"): the slice map changes only on
// a landed write, i.e. while recording.
const spky::Instrument* volatile g_inst_ro = nullptr;

// ---- Foreground state ----

TakeSaver g_saver;
bool      g_booted       = false;
int       g_next_slot[2] = {0, 0};
uint32_t  g_next_seq[2]  = {1, 1};

struct Flight
{
    int      slot     = -1;
    uint32_t crc      = 0;
    uint32_t block0   = 0;
    uint32_t held0    = 0;
    uint32_t worst_us = 0;
};
Flight g_fl;

// The latched lines (spec 4.6): repeated every 5 s, because a line printed
// before the host attached is never seen.
struct MountLine
{
    bool     seen  = false;
    int      fr    = -1;   // f_mount
    int      mk    = 0;    // the first failing f_mkdir / slot preparation, 0 = none
    uint32_t frags = 0;    // the most fragmented slot's fragments (1 = contiguous)
    uint32_t align = 0;    // the sampler buffer's address mod 32
};
struct LoadLine
{
    bool        seen   = false;
    int         slot   = -1;
    uint32_t    seq    = 0;
    uint32_t    frames = 0;
    uint32_t    ms     = 0;
    const char* crc    = "-";
    const char* src    = "-";
};
struct SaveLine
{
    bool        seen     = false;
    int         deck     = 0;
    int         slot     = -1;
    uint32_t    seq      = 0;
    uint32_t    frames   = 0;
    uint32_t    ms       = 0;
    uint32_t    worst_us = 0;
    uint32_t    held     = 0;
    uint32_t    blocks   = 0;
    const char* ok       = "-";
    int         fr       = 0;
    uint32_t    n_ok     = 0;
    uint32_t    n_fail   = 0;
    uint32_t    n_abort  = 0;
};
MountLine g_mount_line;
LoadLine  g_load_line[2];
SaveLine  g_save_line;
#if SHELL_TAKES_STRESS
struct StressLine
{
    bool        crc_seen = false;
    uint32_t    crc_ms   = 0;
    uint32_t    crc_kib  = 0;
    bool        cmp_seen = false;
    const char* cmp      = "-";
    uint32_t    n_ok     = 0;
    uint32_t    n_bad    = 0;
};
StressLine g_stress_line;
uint32_t   g_stress_last = 0;
#endif

inline void fence() { std::atomic_signal_fence(std::memory_order_seq_cst); }

uint8_t* deck_bytes(int p)
{
    return reinterpret_cast<uint8_t*>(fx_mem().sampler_buf[p]);
}

void read_views(DeckView d[2])
{
    uint32_t s;
    do
    {
        s = g_view_seq;
        fence();
        for(int p = 0; p < 2; ++p)
        {
            d[p].gen       = g_view_gen[p];
            d[p].recording = g_view_rec[p];
            d[p].loading   = g_view_load[p];
            d[p].frames    = g_view_frames[p];
        }
        fence();
    } while((s & 1u) != 0 || s != g_view_seq);
}

// Posts a request and waits for the callback to apply it. False when no
// block answered within kMailTimeoutMs (audio not running).
bool post(int p, uint32_t op)
{
    fence();
    g_mail[p].op     = op;
    const uint32_t t0 = daisy::System::GetNow();
    while(g_mail[p].op != kMailNone)
        if(daisy::System::GetNow() - t0 > kMailTimeoutMs)
            return false;
    fence();
    return true;
}

// ---- Card calls, each marked for the pot hold ----

FRESULT c_open(const char* path, BYTE mode)
{
    SdMark        m;
    const FRESULT fr = f_open(&g_fs.file, path, mode);
    g_open           = fr == FR_OK;
    return fr;
}
FRESULT c_close()
{
    if(!g_open) return FR_OK;
    SdMark m;
    g_open = false;
    return f_close(&g_fs.file);
}
FRESULT c_seek(FSIZE_t ofs)
{
    SdMark m;
    return f_lseek(&g_fs.file, ofs);
}
FRESULT c_read(void* buf, UINT n)
{
    UINT          br = 0;
    FRESULT       fr;
    {
        SdMark m;
        fr = f_read(&g_fs.file, buf, n, &br);
    }
    return fr == FR_OK && br != n ? FR_DISK_ERR : fr;
}
// Writes n bytes; `us` gets the call's time (DWT, wraps at 8.9 s).
FRESULT c_write(const void* buf, UINT n, uint32_t* us = nullptr)
{
    UINT           bw = 0;
    FRESULT        fr;
    const uint32_t c0 = cycles_now();
    {
        SdMark m;
        fr = f_write(&g_fs.file, buf, n, &bw);
    }
    if(us != nullptr) *us = (cycles_now() - c0) / kCoreMhz;
    return fr == FR_OK && bw != n ? FR_DENIED : fr;
}
FRESULT c_sync()
{
    SdMark m;
    return f_sync(&g_fs.file);
}

// ---- Boot: mount, slots, load ----

// Opens (or creates) one slot file at its full size and checks its cluster
// map; a file created here, or one whose size was wrong, gets an all-zero
// header block, which decodes as invalid (no RIFF, seq 0) -- an old take
// left in the clusters of a deleted file must never load.
FRESULT prepare_slot(const char* path, uint32_t& frags)
{
    FRESULT fr    = c_open(path, FA_READ | FA_WRITE);
    bool    fresh = false;
    if(fr == FR_NO_FILE)
    {
        fr    = c_open(path, FA_READ | FA_WRITE | FA_CREATE_NEW);
        fresh = true;
    }
    if(fr != FR_OK) return fr;
    if(f_size(&g_fs.file) != kTakeFileBytes)
    {
        // Grow by seeking past the end in write mode (FatFs allocates the
        // clusters; f_expand is compiled out, spec 3.3), or cut a longer one.
        fr = c_seek(kTakeFileBytes);
        if(fr == FR_OK && f_tell(&g_fs.file) != kTakeFileBytes) fr = FR_DENIED;   // card full
        if(fr == FR_OK)
        {
            SdMark m;
            fr = f_truncate(&g_fs.file);
        }
        fresh = true;
    }
    if(fr == FR_OK && fresh)
    {
        std::memset(g_hdr, 0, sizeof g_hdr);
        fr = c_seek(0);
        if(fr == FR_OK) fr = c_write(g_hdr, kTakeHeaderBytes);
    }
    if(fr == FR_OK) fr = c_sync();
    if(fr == FR_OK)
    {
        // Contiguity, as C1 measured it: tbl[0] is 2 + 2 x fragments after
        // CREATE_LINKMAP (also on FR_NOT_ENOUGH_CORE, then the size needed).
        g_fs.file.cltbl = g_clmt;
        g_clmt[0]       = kClmtWords;
        const FRESULT lr = c_seek(CREATE_LINKMAP);
        g_fs.file.cltbl = nullptr;
        const uint32_t n = (lr == FR_OK || lr == FR_NOT_ENOUGH_CORE) && g_clmt[0] >= 2
                               ? (g_clmt[0] - 2u) / 2u : 0u;
        if(n > frags) frags = n;
    }
    const FRESULT cr = c_close();
    return fr == FR_OK ? cr : fr;
}

bool mount()
{
    daisy::SdmmcHandler::Config cfg;
    cfg.Defaults();
    cfg.speed           = kSpeed;
    cfg.clock_powersave = true;
    g_sdmmc.Init(cfg);
    g_fs.fsi.Init(daisy::FatFSInterface::Config::MEDIA_SD);
    const char* root = g_fs.fsi.GetSDPath();

    MountLine& L = g_mount_line;
    L.seen       = true;
    L.align      = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fx_mem().sampler_buf[0]) % 32u);
    {
        SdMark m;
        L.fr = f_mount(&g_fs.fsi.GetSDFileSystem(), root, 1);
    }
    if(L.fr != FR_OK) return false;

    char dir[32];
    std::snprintf(dir, sizeof dir, "%sFIREFLOW", root);
    FRESULT fr;
    {
        SdMark m;
        fr = f_mkdir(dir);
    }
    if(fr != FR_OK && fr != FR_EXIST)
    {
        L.mk = fr;
        return false;
    }
    for(int p = 0; p < 2; ++p)
        for(int s = 0; s < kSlots; ++s)
        {
            std::snprintf(dir, sizeof dir, "%sFIREFLOW/%c%d", root, 'A' + p, s);
            {
                SdMark m;
                fr = f_mkdir(dir);
            }
            if(fr != FR_OK && fr != FR_EXIST)
            {
                L.mk = fr;
                return false;
            }
            std::snprintf(g_path[p][s], sizeof g_path[p][s], "%s/TAKE.WAV", dir);
            fr = prepare_slot(g_path[p][s], L.frags);
            if(fr != FR_OK)
            {
                L.mk = fr;
                return false;
            }
        }
    return true;
}

// Reads slot s's header block into g_hdr and decodes it for deck p.
bool read_header(int p, int s, TakeMeta& m)
{
    FRESULT fr = c_open(g_path[p][s], FA_READ);
    if(fr == FR_OK) fr = c_read(g_hdr, kTakeHeaderBytes);
    c_close();
    return fr == FR_OK && take_header_decode(g_hdr, static_cast<uint8_t>(p), m);
}

enum class Loaded { Ok, Bad, Deadline };

// Reads slot s of deck p into the deck's buffer, back to back in 16 KB pieces
// under the hold, the CRC folded as it goes. The deck is loading: nothing
// else reads or writes its buffer meanwhile.
Loaded load_slot(int p, int s, uint32_t b0)
{
    if(!read_header(p, s, g_meta)) return Loaded::Bad;
    FRESULT fr = c_open(g_path[p][s], FA_READ);
    if(fr == FR_OK) fr = c_seek(kTakeHeaderBytes);
    const uint32_t bytes = g_meta.frames * kTakeFrameBytes;
    uint8_t* const dst   = deck_bytes(p);
    uint32_t       crc   = crc32_begin();
    for(uint32_t off = 0; fr == FR_OK && off < bytes; off += kTakePieceBytes)
    {
        if(g_block_tick - b0 > kLoadDeadlineBlocks)
        {
            c_close();
            return Loaded::Deadline;
        }
        const uint32_t len = bytes - off < kTakePieceBytes ? bytes - off : kTakePieceBytes;
        fr                 = c_read(dst + off, len);
        if(fr == FR_OK) crc = crc32_update(crc, dst + off, len);
    }
    c_close();
    return fr == FR_OK && take_crc_ok(crc, g_meta) ? Loaded::Ok : Loaded::Bad;
}

void load_deck(int p)
{
    LoadLine& L = g_load_line[p];
    L.seen      = true;
    if(!post(p, kMailBegin))
    {
        L.src = "nomail";
        g_saver.off = true;
        return;
    }
    bool     valid[kSlots];
    uint32_t seq[kSlots];
    uint32_t seq_max = 0;
    for(int s = 0; s < kSlots; ++s)
    {
        valid[s] = read_header(p, s, g_meta);
        seq[s]   = valid[s] ? g_meta.seq : 0u;
        if(seq[s] > seq_max) seq_max = seq[s];
    }
    const SlotOrder order = take_slot_order(valid, seq);
    const int       cand[2] = {order.first, order.second};
    const uint32_t  b0     = g_block_tick;
    int             loaded = -1;
    bool            bad    = false;
    bool            late   = false;
    for(int c : cand)
    {
        if(c < 0) continue;
        const Loaded r = load_slot(p, c, b0);
        if(r == Loaded::Ok)
        {
            loaded = c;
            break;
        }
        if(r == Loaded::Deadline)
        {
            late = true;
            break;
        }
        bad = true;
    }
    L.ms = (g_block_tick - b0) * kBlockMs;
    g_next_seq[p] = seq_max + 1u;

    if(loaded >= 0)
    {
        g_mail[p].frames = g_meta.frames;
        g_mail[p].n      = g_meta.n_markers;
        g_mail[p].mf     = g_meta.marker_frame;
        g_mail[p].ms     = g_meta.marker_strength;
        if(!post(p, kMailRestore))
        {
            L.src = "nomail";
            g_saver.off = true;
            return;
        }
        L.slot   = loaded;
        L.seq    = g_meta.seq;
        L.frames = g_meta.frames;
        L.crc    = "ok";
        L.src    = g_mail[p].ok ? "card" : "rejected";
        g_next_slot[p] = take_next_slot(loaded);
    }
    else
    {
        post(p, kMailAbort);
        L.crc = bad ? "bad" : "-";
        L.src = late ? "deadline" : "empty";
        g_next_slot[p] = 0;
        if(late)
        {
            // Spec 4.4: a load that does not finish leaves the deck empty and
            // saves off for the session -- a card that slow would hold the
            // pots for every save too.
            g_saver.off = true;
            return;
        }
    }
    take_seed(g_saver, p, g_mail[p].gen, g_block_tick);
}

#if SHELL_TAKES_STRESS
// Deck A loaded empty: a 10 s deterministic pattern (xorshift32, +-0.5) goes
// into its buffer, no markers, and the CRC is timed over it once.
void stress_fill()
{
    if(g_saver.off || g_load_line[0].src == nullptr
       || std::strcmp(g_load_line[0].src, "card") == 0)
        return;
    if(!post(0, kMailBegin)) return;
    spky::SampleBuffer::Frame* f = fx_mem().sampler_buf[0];
    uint32_t                   x = 0x9E3779B9u;
    for(uint32_t i = 0; i < kStressFrames; ++i)
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        f[i].l = static_cast<float>(x >> 8) * (1.0f / 16777216.0f) - 0.5f;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        f[i].r = static_cast<float>(x >> 8) * (1.0f / 16777216.0f) - 0.5f;
    }
    const uint32_t c0  = cycles_now();
    const uint32_t crc = crc32_update(crc32_begin(), deck_bytes(0), kStressFrames * kTakeFrameBytes);
    g_stress_line.crc_ms   = (cycles_now() - c0) / (kCoreMhz * 1000u);
    g_stress_line.crc_kib  = kStressFrames * kTakeFrameBytes / 1024u;
    g_stress_line.crc_seen = true;
    (void)crc;
    g_mail[0].frames = kStressFrames;
    g_mail[0].n      = 0;
    g_mail[0].mf     = nullptr;
    g_mail[0].ms     = nullptr;
    if(!post(0, kMailRestore)) return;
    take_seed(g_saver, 0, g_mail[0].gen, g_block_tick);
    g_stress_last = g_block_tick;
}

// After a save of deck A: the slot read back and compared, header and data,
// against what is in the buffer (nothing records in the stress image).
void stress_compare(int slot, uint32_t frames)
{
    TakeMeta   m;
    bool       ok = read_header(0, slot, m) && m.frames == frames;
    FRESULT    fr = ok ? c_open(g_path[0][slot], FA_READ) : FR_OK;
    if(ok && fr == FR_OK) fr = c_seek(kTakeHeaderBytes);
    const uint32_t bytes = frames * kTakeFrameBytes;
    uint32_t       crc   = crc32_begin();
    for(uint32_t off = 0; ok && fr == FR_OK && off < bytes; off += kTakeHeaderBytes)
    {
        const uint32_t len = bytes - off < kTakeHeaderBytes ? bytes - off : kTakeHeaderBytes;
        fr = c_read(g_hdr, len);
        if(fr != FR_OK) break;
        crc = crc32_update(crc, g_hdr, len);
        if(std::memcmp(g_hdr, deck_bytes(0) + off, len) != 0) ok = false;
    }
    c_close();
    ok = ok && fr == FR_OK && take_crc_ok(crc, m);
    g_stress_line.cmp_seen = true;
    g_stress_line.cmp      = ok ? "ok" : "bad";
    if(ok) ++g_stress_line.n_ok;
    else ++g_stress_line.n_bad;
}
#endif

// ---- Save: Begin, Piece, Finish (spec 4.2) ----

void save_failed(FRESULT fr)
{
    c_close();
    SaveLine& L = g_save_line;
    L.ok        = "0";
    L.fr        = fr;
    ++L.n_fail;
    take_finished(g_saver, false, g_block_tick);
}

void save_aborted()
{
    c_close();
    g_save_line.ok = "abort";
    ++g_save_line.n_abort;
}

void save_line_open(int deck)
{
    SaveLine& L = g_save_line;
    L.seen      = true;
    L.deck      = deck;
    L.slot      = g_fl.slot;
    L.seq       = 0;
    L.frames    = g_saver.total_bytes / kTakeFrameBytes;
    L.ok        = "run";
    L.fr        = 0;
}

void save_line_times()
{
    SaveLine& L = g_save_line;
    L.blocks    = g_block_tick - g_fl.block0;
    L.ms        = L.blocks * kBlockMs;
    L.held      = sd_held_blocks() - g_fl.held0;
    L.worst_us  = g_fl.worst_us;
}

// 1. The slot's header block with seq 0 (invalid), f_sync.
void save_begin(int deck)
{
    g_fl          = Flight{};
    g_fl.slot     = g_next_slot[deck];
    g_fl.crc      = crc32_begin();
    g_fl.block0   = g_block_tick;
    g_fl.held0    = sd_held_blocks();
    save_line_open(deck);

    g_meta        = TakeMeta{};
    g_meta.deck   = static_cast<uint8_t>(deck);
    take_header_encode(g_meta, g_hdr);   // seq 0: decodes as invalid
    FRESULT fr = c_open(g_path[deck][g_fl.slot], FA_READ | FA_WRITE);
    uint32_t us = 0;
    if(fr == FR_OK) fr = c_seek(0);
    if(fr == FR_OK) fr = c_write(g_hdr, kTakeHeaderBytes, &us);
    if(fr == FR_OK) fr = c_sync();
    if(us > g_fl.worst_us) g_fl.worst_us = us;
    if(fr != FR_OK)
    {
        save_line_times();
        save_failed(fr);
        return;
    }
    take_begun(g_saver, g_block_tick);
}

// 2. One paced piece straight from the deck's buffer, the CRC over the same
// bytes, then the sequence lock's second read.
void save_piece(int deck)
{
    const uint32_t off = take_piece_offset(g_saver);
    const uint32_t len = take_piece_len(g_saver);
    const uint8_t* src = deck_bytes(deck) + off;
    FRESULT        fr  = FR_OK;
    if(f_tell(&g_fs.file) != kTakeHeaderBytes + off) fr = c_seek(kTakeHeaderBytes + off);
    uint32_t us = 0;
    if(fr == FR_OK) fr = c_write(src, len, &us);
    if(us > g_fl.worst_us) g_fl.worst_us = us;
    if(fr != FR_OK)
    {
        save_line_times();
        save_failed(fr);
        return;
    }
    g_fl.crc = crc32_update(g_fl.crc, src, len);
    DeckView d[2];
    read_views(d);
    if(!take_still_valid(g_saver, d[deck]))
    {
        take_aborted(g_saver, g_block_tick);
        save_line_times();
        save_aborted();
        return;
    }
    take_piece_done(g_saver, g_block_tick);
}

// 3. The slice map, the lock again, JUNK, the final header, f_sync.
void save_finish(int deck)
{
    const spky::Instrument* inst = g_inst_ro;
    g_meta           = TakeMeta{};
    g_meta.deck      = static_cast<uint8_t>(deck);
    g_meta.frames    = g_saver.total_bytes / kTakeFrameBytes;
    g_meta.n_markers = inst != nullptr
                           ? inst->sampler_export_markers(deck, g_meta.marker_frame,
                                                          g_meta.marker_strength, kTakeMaxMarkers)
                           : 0;
    DeckView d[2];
    read_views(d);
    if(!take_still_valid(g_saver, d[deck]))
    {
        take_aborted(g_saver, g_block_tick);
        save_line_times();
        save_aborted();
        return;
    }
    g_meta.seq = g_next_seq[deck];
    g_meta.crc = crc32_end(take_meta_crc(g_fl.crc, g_meta));
    take_header_encode(g_meta, g_hdr);
    // Self-check: a slice map the decoder would reject (not ascending, past
    // the take) would make the whole slot unloadable. The take is worth more
    // than its markers: save it without them.
    TakeMeta check;
    if(!take_header_decode(g_hdr, static_cast<uint8_t>(deck), check))
    {
        g_meta.n_markers = 0;
        g_meta.crc       = crc32_end(take_meta_crc(g_fl.crc, g_meta));
        take_header_encode(g_meta, g_hdr);
    }
    take_junk_header(g_meta.frames, g_fs.junk);
    uint32_t us = 0;
    FRESULT  fr = c_seek(take_junk_offset(g_meta.frames));
    if(fr == FR_OK) fr = c_write(g_fs.junk, 8);
    if(fr == FR_OK) fr = c_seek(0);
    if(fr == FR_OK) fr = c_write(g_hdr, kTakeHeaderBytes, &us);
    if(fr == FR_OK) fr = c_sync();
    if(us > g_fl.worst_us) g_fl.worst_us = us;
    const FRESULT cr = c_close();
    if(fr == FR_OK) fr = cr;
    save_line_times();
    if(fr != FR_OK)
    {
        save_failed(fr);
        return;
    }
    SaveLine& L = g_save_line;
    L.seq       = g_meta.seq;
    L.ok        = "1";
    ++L.n_ok;
    const int slot = g_fl.slot;
    g_next_slot[deck] = take_next_slot(slot);
    ++g_next_seq[deck];
    take_finished(g_saver, true, g_block_tick);
#if SHELL_TAKES_STRESS
    if(deck == 0) stress_compare(slot, g_meta.frames);
#endif
}

// One step of the policy; true when it did card work.
bool save_step()
{
#if SHELL_TAKES_STRESS
    if(!g_saver.off && g_block_tick - g_stress_last >= kStressEveryBlocks)
    {
        take_force(g_saver, 0);
        g_stress_last = g_block_tick;
    }
#endif
    DeckView d[2];
    read_views(d);
    const TakeStep st   = take_next(g_saver, d, g_block_tick);
    const int      deck = g_saver.deck;
    switch(st)
    {
        case TakeStep::Begin: save_begin(deck); return true;
        case TakeStep::Piece: save_piece(deck); return true;
        case TakeStep::Finish: save_finish(deck); return true;
        case TakeStep::Abort:   // take_next() has already called take_aborted()
            save_line_times();
            save_aborted();
            return true;
        default: return false;
    }
}

} // namespace

// ---- Foreground ----

void take_card_boot(bench::Board& hw)
{
    (void)hw;
    if(g_booted) return;
    g_booted = true;
    if(!mount())
    {
        g_saver.off = true;   // no card, not FAT, or no room: the board plays as today
        return;
    }
    load_deck(0);
    load_deck(1);
#if SHELL_TAKES_STRESS
    stress_fill();
#endif
}

void take_service_for(bench::Board& hw, uint32_t ms)
{
    const uint32_t blocks = ms / kBlockMs;
    const uint32_t b0     = g_block_tick;
    const uint32_t t0     = daisy::System::GetNow();
    while(g_block_tick - b0 < blocks)
    {
        // Audio not running: GetNow() runs about 25 % slow while it does
        // (docs/gotchas.md), so this bound only ends a pass without blocks.
        if(daisy::System::GetNow() - t0 >= 2u * ms) break;
        if(!g_booted || g_saver.off || !save_step()) hw.Delay(1);
    }
}

void take_report(bench::Board& hw, uint32_t pass)
{
    if(pass % kReportEvery != 0) return;
    const MountLine& M = g_mount_line;
    if(M.seen)
        hw.PrintLine("SHELL_TAKE mount fr=%d mk=%d frags=%u sdram_align=%u saves=%s fails=%d",
                     M.fr, M.mk, static_cast<unsigned>(M.frags), static_cast<unsigned>(M.align),
                     g_saver.off ? "off" : "on", g_saver.fails);
    for(int p = 0; p < 2; ++p)
    {
        const LoadLine& L = g_load_line[p];
        if(!L.seen) continue;
        hw.PrintLine("SHELL_TAKE load deck=%c slot=%d seq=%u frames=%u ms=%u crc=%s src=%s",
                     'A' + p, L.slot, static_cast<unsigned>(L.seq),
                     static_cast<unsigned>(L.frames), static_cast<unsigned>(L.ms), L.crc, L.src);
    }
    const SaveLine& S = g_save_line;
    if(S.seen)
    {
        // Two lines: one with every field at ten digits would pass libDaisy's
        // 128-byte log buffer.
        hw.PrintLine("SHELL_TAKE save deck=%c slot=%d seq=%u frames=%u ok=%s fr=%d n=%u/%u/%u",
                     'A' + S.deck, S.slot, static_cast<unsigned>(S.seq),
                     static_cast<unsigned>(S.frames), S.ok, S.fr, static_cast<unsigned>(S.n_ok),
                     static_cast<unsigned>(S.n_fail), static_cast<unsigned>(S.n_abort));
        hw.PrintLine("SHELL_TAKE save_t ms=%u worst_us=%u held=%u blocks=%u",
                     static_cast<unsigned>(S.ms), static_cast<unsigned>(S.worst_us),
                     static_cast<unsigned>(S.held), static_cast<unsigned>(S.blocks));
    }
#if SHELL_TAKES_STRESS
    const StressLine& X = g_stress_line;
    if(X.crc_seen)
        hw.PrintLine("SHELL_TAKE crc sw_ms=%u kib=%u", static_cast<unsigned>(X.crc_ms),
                     static_cast<unsigned>(X.crc_kib));
    if(X.cmp_seen)
        hw.PrintLine("SHELL_TAKE cmp=%s ok=%u bad=%u", X.cmp, static_cast<unsigned>(X.n_ok),
                     static_cast<unsigned>(X.n_bad));
#endif
}

// ---- Audio callback ----

void take_block(spky::Instrument& inst)
{
    g_inst_ro = &inst;
    for(int p = 0; p < 2; ++p)
    {
        const uint32_t op = g_mail[p].op;
        if(op == kMailNone) continue;
        fence();
        bool ok = true;
        switch(op)
        {
            case kMailBegin: inst.sampler_begin_load(p); break;
            case kMailRestore:
                ok = inst.sampler_restore(p, g_mail[p].frames, g_mail[p].mf, g_mail[p].ms,
                                          g_mail[p].n);
                break;
            case kMailAbort: inst.sampler_abort_load(p); break;
            default: ok = false; break;
        }
        g_mail[p].ok  = ok;
        g_mail[p].gen = inst.sampler_content_gen(p);
        fence();
        g_mail[p].op = kMailNone;
    }
    g_view_seq = g_view_seq + 1u;   // odd: writing
    fence();
    for(int p = 0; p < 2; ++p)
    {
        g_view_gen[p]    = inst.sampler_content_gen(p);
        g_view_rec[p]    = inst.sampler_is_recording(p);
        g_view_load[p]   = inst.sampler_loading(p);
        g_view_frames[p] = static_cast<uint32_t>(inst.sampler_rec_size(p));
    }
    fence();
    g_view_seq   = g_view_seq + 1u;
    g_block_tick = g_block_tick + 1u;
}

} // namespace shell
