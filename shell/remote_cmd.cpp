#include "remote_cmd.h"

#include <atomic>

namespace shell {

namespace {

// True when [p, p+n) is exactly the verb (case sensitive).
bool word_is(const char* p, int n, const char* verb)
{
    int i = 0;
    for(; i < n; ++i) {
        if(verb[i] == 0 || p[i] != verb[i]) return false;
    }
    return verb[i] == 0;
}

// Parses an unsigned decimal token [p, p+n) into *v; false on empty, a
// non-digit (so no sign), or a value past what int32 holds.
bool parse_uint(const char* p, int n, int32_t* v)
{
    if(n <= 0) return false;
    int64_t acc = 0;
    for(int i = 0; i < n; ++i) {
        if(p[i] < '0' || p[i] > '9') return false;
        acc = acc * 10 + (p[i] - '0');
        if(acc > 0x7fffffff) return false;
    }
    *v = (int32_t)acc;
    return true;
}

} // namespace

bool parse_command_line(const char* line, int len, Command* out)
{
    // Split on single spaces into at most three tokens; anything more, or an
    // empty token (double space, leading/trailing space), is a bad line.
    const char* tok[3];
    int tlen[3];
    int nt = 0;
    int start = 0;
    for(int i = 0; i <= len; ++i) {
        if(i == len || line[i] == ' ') {
            if(nt == 3 || i == start) return false;
            tok[nt] = line + start;
            tlen[nt] = i - start;
            ++nt;
            start = i + 1;
        }
    }
    if(nt == 0) return false;

    Command c{CmdKind::None, 0, 0};
    const char* v = tok[0];
    const int vn = tlen[0];

    if(word_is(v, vn, "PING"))      { if(nt != 1) return false; c.kind = CmdKind::Ping; }
    else if(word_is(v, vn, "BOOT")) { if(nt != 1) return false; c.kind = CmdKind::Boot; }
    else if(word_is(v, vn, "HOME")) { if(nt != 1) return false; c.kind = CmdKind::Home; }
    else if(word_is(v, vn, "RST"))  { if(nt != 1) return false; c.kind = CmdKind::ResetMax; }
    else if(word_is(v, vn, "LOAD") || word_is(v, vn, "GEST")) {
        if(nt != 2 || !parse_uint(tok[1], tlen[1], &c.a)) return false;
        c.kind = word_is(v, vn, "LOAD") ? CmdKind::Load : CmdKind::Gesture;
    }
    else if(word_is(v, vn, "TOG")) {
        if(nt != 3 || !parse_uint(tok[1], tlen[1], &c.a) || !parse_uint(tok[2], tlen[2], &c.b)) return false;
        if(c.a > 31 || c.b > 1) return false;
        c.kind = CmdKind::Toggle;
    }
    else if(word_is(v, vn, "LATE")) {
        if(nt != 2 || !parse_uint(tok[1], tlen[1], &c.a)) return false;
        if(c.a < 1 || c.a > 2000) return false;
        c.kind = CmdKind::Late;
    }
    else return false;

    *out = c;
    return true;
}

int LineAssembler::feed(const uint8_t* buf, uint32_t len, Command* out, int max_out)
{
    int n = 0;
    for(uint32_t i = 0; i < len; ++i) {
        const char ch = (char)buf[i];
        if(ch == '\r') continue;
        if(ch == '\n') {
            if(!discarding_ && n < max_out && parse_command_line(line_, len_, &out[n])) ++n;
            len_ = 0;
            discarding_ = false;
            continue;
        }
        if(discarding_) continue;
        if(len_ == kMaxLine) { // the 32nd character: this line is too long
            discarding_ = true;
            len_ = 0;
            continue;
        }
        line_[len_++] = ch;
    }
    return n;
}

bool CmdRing::push(const Command& c)
{
    const uint32_t h = head_;
    if(h - tail_ >= kCapacity) return false;
    slots_[h % kCapacity] = c;
    std::atomic_signal_fence(std::memory_order_seq_cst); // slot written before it is published
    head_ = h + 1;
    return true;
}

bool CmdRing::pop(Command* c)
{
    const uint32_t t = tail_;
    if(head_ == t) return false;
    std::atomic_signal_fence(std::memory_order_seq_cst); // read the head before the slot
    *c = slots_[t % kCapacity];
    std::atomic_signal_fence(std::memory_order_seq_cst); // slot read before it is released
    tail_ = t + 1;
    return true;
}

} // namespace shell
