#pragma once

// Sampler takes on the SD card (spec 2026-11-06-sampler-takes-on-sd §4.3):
// when a deck's take is saved, how fast its pieces go out, and when a save in
// flight is abandoned. The foreground calls take_next() on every service
// tick, does the card work for the step it gets, and reports back; nothing
// here touches a card, a file or the engine.
//
// Time is counted in audio blocks (2 ms each, g_block_tick), never with
// System::GetNow(), which runs ~25 % slow while audio plays (docs/gotchas.md).
//
// Pure data logic, no hardware type, host tested (tests/test_take_policy.cpp).
#include <cstdint>

namespace shell {

inline constexpr uint32_t kTakeQuietBlocks      = 2500;    // 5 s unchanged before a save
inline constexpr uint32_t kTakePieceBytes       = 16384;   // one paced f_write (A3: 16 KB)
// 80 ms between pieces. Spec §4.3 started at 21 (42 ms); board session A,
// 2026-11-06 (docs/hardware/takes-measured.md): at 21 the pot hold dropped
// 10.8-15.8 % of the blocks of every one of 40 saves, up to 3.9 held blocks
// per 16 KB piece, against the gate of 10 %. 40 keeps 3.9 / (40 + ~1.5)
// under it.
inline constexpr uint32_t kTakePieceEveryBlocks = 40;
inline constexpr int      kTakeFailLimit        = 3;       // failed saves in a row, then saving stops

// What the foreground reads of one deck per tick (published by the callback).
struct DeckView
{
    uint32_t gen;       // Instrument::sampler_content_gen()
    bool     recording; // Instrument::sampler_is_recording()
    bool     loading;   // Instrument::sampler_loading()
    uint32_t frames;    // Instrument::sampler_rec_size()
};

enum class TakeStep : uint8_t
{
    Idle,    // nothing to do this tick
    Begin,   // write the slot's header block with seq 0, f_sync
    Piece,   // write data bytes [piece_offset, +piece_len)
    Finish,  // copy the markers, write JUNK and the final header, f_sync
    Abort,   // the save in flight is void (REC, clear, new content); the slot stays invalid
    Off,     // saving is off for this session
};

struct TakeSaver
{
    bool     seeded[2]      = {false, false};   // a deck is never due before take_seed()
    uint32_t saved_gen[2]   = {0, 0};
    uint32_t seen_gen[2]    = {0, 0};
    uint32_t quiet_since[2] = {0, 0};
    bool     force[2]       = {false, false};   // stress mode: due regardless of the generation
    // The save in flight.
    int      deck           = -1;
    bool     begun          = false;   // Begin reported done
    uint32_t gen_at_begin   = 0;
    uint32_t total_bytes    = 0;       // frames * 8 at Begin
    uint32_t next_offset    = 0;
    uint32_t next_piece_at  = 0;       // block
    int      fails          = 0;
    bool     off            = false;
};

// After a restore, or an empty boot: the deck's current content counts as saved.
void take_seed(TakeSaver& s, int deck, uint32_t gen, uint32_t now);
// Stress mode (SHELL_TAKES_STRESS): the next quiet window saves the deck anyway.
inline void take_force(TakeSaver& s, int deck) { s.force[deck] = true; }

TakeStep take_next(TakeSaver& s, const DeckView d[2], uint32_t now);

// The piece take_next() asked for.
inline uint32_t take_piece_offset(const TakeSaver& s) { return s.next_offset; }
uint32_t take_piece_len(const TakeSaver& s);

// The sequence lock's second read: after the foreground copied a piece or the
// markers, the deck must still show the generation the save began with and
// not be recording. False: call take_aborted().
bool take_still_valid(const TakeSaver& s, const DeckView& d);

void take_begun(TakeSaver& s, uint32_t now);        // Begin done
void take_piece_done(TakeSaver& s, uint32_t now);   // Piece done
// Finish done (ok), or any card error (!ok). A failure retries only after a
// new quiet window (spec §4.6), so kTakeFailLimit failures take that long.
void take_finished(TakeSaver& s, bool ok, uint32_t now);
void take_aborted(TakeSaver& s, uint32_t now);      // not a failure: the deck waits for a new quiet window

} // namespace shell
