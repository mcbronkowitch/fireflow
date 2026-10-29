#pragma once

// The remote command channel's pure half (CPU hunt, 2026-10-29): a text
// protocol over USB CDC so the board can be measured and re-flashed without a
// hand on the panel. Three small pieces, host tested (tests/test_remote_cmd.cpp):
//   parse_command_line  one ASCII line -> a Command
//   LineAssembler       USB packets (torn at any byte) -> whole lines -> Commands
//   CmdRing             the hand-over from the USB ISR (producer) to the audio
//                       callback (consumer)
// No heap, no <string>, no exceptions: the same file builds for the Daisy.
//
// Syntax, one command per line, '\n' ends a line, '\r' is ignored, at most 31
// characters per line:
//   PING             liveness
//   BOOT             reset into the Daisy bootloader (waits for DFU)
//   HOME             back to the booted patch
//   LOAD <id>        load patch <id>            (id >= 0)
//   GEST <id>        play gesture <id>          (id >= 0)
//   TOG <bit> <0|1>  set workload bit 0..31
//   RST              reset the maxima
//   LATE <kcycles>   busy-wait load, 1..2000 kcycles
// Anything else is not a command.
#include <stdint.h>

namespace shell {

enum class CmdKind : uint8_t { None, Ping, Boot, Home, Load, Gesture, Toggle, ResetMax, Late };

struct Command {
    CmdKind kind;
    int32_t a;
    int32_t b;
};

// Parses one line (no terminator needed; len bytes at line). True and *out
// filled on a valid command; false (out untouched) on anything else.
bool parse_command_line(const char* line, int len, Command* out);

// Packet-tolerant line splitter. Feed it each USB packet as it arrives; a
// command may span any number of packets and a packet may hold any number of
// lines. A line longer than 31 characters is discarded up to its newline, so
// garbage cannot glue itself to the next command. A line that does not parse
// is dropped silently.
class LineAssembler {
public:
    // Appends the commands completed by buf[0..len) to out[0..max_out) and
    // returns how many it wrote. Completed commands beyond max_out are dropped.
    int feed(const uint8_t* buf, uint32_t len, Command* out, int max_out);

private:
    static constexpr int kMaxLine = 31;
    char line_[kMaxLine] = {};
    int len_ = 0;
    bool discarding_ = false;
};

// Single producer (USB ISR), single consumer (audio callback), capacity 8,
// lock-free. The indices are free-running counters; only the producer writes
// head_, only the consumer writes tail_. On the single-core M7 a compiler
// barrier orders the slot write against the index publish (ISR vs thread
// share one core, so no hardware barrier is needed); the host build uses the
// same code.
class CmdRing {
public:
    static constexpr uint32_t kCapacity = 8;
    bool push(const Command& c); // false when full
    bool pop(Command* c);        // false when empty

private:
    Command slots_[kCapacity] = {};
    volatile uint32_t head_ = 0;
    volatile uint32_t tail_ = 0;
};

} // namespace shell
