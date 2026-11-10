#include "take_policy.h"

namespace shell {

namespace {

void reset_flight(TakeSaver& s)
{
    s.deck = -1;
    s.begun = false;
    s.gen_at_begin = 0;
    s.total_bytes = 0;
    s.next_offset = 0;
    s.next_piece_at = 0;
}

bool due(const TakeSaver& s, int p, const DeckView& d, uint32_t now)
{
    if (!s.seeded[p] || d.recording || d.loading) return false;
    if (d.gen == s.saved_gen[p] && !s.force[p]) return false;
    return now - s.quiet_since[p] >= kTakeQuietBlocks;
}

} // namespace

void take_seed(TakeSaver& s, int deck, uint32_t gen, uint32_t now)
{
    s.seeded[deck]      = true;
    s.saved_gen[deck]   = gen;
    s.seen_gen[deck]    = gen;
    s.quiet_since[deck] = now;
}

TakeStep take_next(TakeSaver& s, const DeckView d[2], uint32_t now)
{
    if (s.off) return TakeStep::Off;

    // The quiet window restarts on any change, while recording and while
    // loading: "5 s unchanged" means five seconds with none of those.
    for (int p = 0; p < 2; ++p) {
        if (d[p].gen != s.seen_gen[p] || d[p].recording || d[p].loading) {
            s.seen_gen[p]    = d[p].gen;
            s.quiet_since[p] = now;
        }
    }

    if (s.deck >= 0) {
        if (!take_still_valid(s, d[s.deck])) {
            take_aborted(s, now);
            return TakeStep::Abort;
        }
        if (!s.begun) return TakeStep::Begin;   // Begin not reported yet: ask again
        if (s.next_offset >= s.total_bytes) return TakeStep::Finish;
        return now - s.next_piece_at < 0x80000000u ? TakeStep::Piece : TakeStep::Idle;
    }

    for (int p = 0; p < 2; ++p) {
        if (!due(s, p, d[p], now)) continue;
        s.deck         = p;
        s.begun        = false;
        s.gen_at_begin = d[p].gen;
        s.total_bytes  = d[p].frames * 8u;
        s.next_offset  = 0;
        s.force[p]     = false;
        return TakeStep::Begin;
    }
    return TakeStep::Idle;
}

uint32_t take_piece_len(const TakeSaver& s)
{
    const uint32_t left = s.total_bytes - s.next_offset;
    return left < kTakePieceBytes ? left : kTakePieceBytes;
}

bool take_still_valid(const TakeSaver& s, const DeckView& d)
{
    return s.deck >= 0 && d.gen == s.gen_at_begin && !d.recording && !d.loading;
}

void take_begun(TakeSaver& s, uint32_t now)
{
    s.begun = true;
    s.next_piece_at = now;   // the first piece may follow at once
}

void take_piece_done(TakeSaver& s, uint32_t now)
{
    s.next_offset += take_piece_len(s);
    s.next_piece_at = now + kTakePieceEveryBlocks;
}

void take_finished(TakeSaver& s, bool ok, uint32_t now)
{
    if (s.deck < 0) return;
    if (ok) {
        s.saved_gen[s.deck] = s.gen_at_begin;
        s.fails = 0;
    } else {
        s.quiet_since[s.deck] = now;   // retry after a full quiet window
        if (++s.fails >= kTakeFailLimit) s.off = true;
    }
    reset_flight(s);
}

void take_aborted(TakeSaver& s, uint32_t now)
{
    if (s.deck >= 0) s.quiet_since[s.deck] = now;
    reset_flight(s);
}

} // namespace shell
