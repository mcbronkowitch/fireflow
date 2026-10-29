// The remote command channel's pure half: the line parser, the packet-tolerant
// line assembler and the ISR -> audio-callback ring (shell/remote_cmd.*).
#include <doctest/doctest.h>
#include <cstring>
#include "../shell/remote_cmd.h"

using shell::CmdKind;
using shell::Command;

namespace {

bool parse(const char* s, Command* c)
{
    return shell::parse_command_line(s, (int)std::strlen(s), c);
}

int feed(shell::LineAssembler& la, const char* s, Command* out, int max_out)
{
    return la.feed(reinterpret_cast<const uint8_t*>(s), (uint32_t)std::strlen(s), out, max_out);
}

} // namespace

TEST_CASE("remote_cmd: every verb parses") {
    Command c{};
    CHECK(parse("PING", &c));   CHECK(c.kind == CmdKind::Ping);
    CHECK(parse("BOOT", &c));   CHECK(c.kind == CmdKind::Boot);
    CHECK(parse("HOME", &c));   CHECK(c.kind == CmdKind::Home);
    CHECK(parse("RST", &c));    CHECK(c.kind == CmdKind::ResetMax);
    CHECK(parse("LOAD 7", &c)); CHECK(c.kind == CmdKind::Load);    CHECK(c.a == 7);
    CHECK(parse("GEST 12", &c)); CHECK(c.kind == CmdKind::Gesture); CHECK(c.a == 12);
    CHECK(parse("TOG 3 1", &c)); CHECK(c.kind == CmdKind::Toggle);
    CHECK(c.a == 3); CHECK(c.b == 1);
    CHECK(parse("TOG 31 0", &c)); CHECK(c.a == 31); CHECK(c.b == 0);
    CHECK(parse("LATE 40", &c)); CHECK(c.kind == CmdKind::Late);   CHECK(c.a == 40);
    CHECK(parse("LATE 1", &c));    CHECK(c.a == 1);
    CHECK(parse("LATE 2000", &c)); CHECK(c.a == 2000);
}

TEST_CASE("remote_cmd: out of range is rejected") {
    Command c{};
    CHECK_FALSE(parse("TOG 32 1", &c));
    CHECK_FALSE(parse("TOG 3 2", &c));
    CHECK_FALSE(parse("LATE 0", &c));
    CHECK_FALSE(parse("LATE 2001", &c));
    CHECK_FALSE(parse("LOAD -1", &c));
    CHECK_FALSE(parse("FOO", &c));
    CHECK_FALSE(parse("", &c));
    // Shape errors: missing / extra / garbled arguments, lowercase verbs.
    CHECK_FALSE(parse("LOAD", &c));
    CHECK_FALSE(parse("LOAD x", &c));
    CHECK_FALSE(parse("LOAD 1 2", &c));
    CHECK_FALSE(parse("TOG 3", &c));
    CHECK_FALSE(parse("PING 1", &c));
    CHECK_FALSE(parse("ping", &c));
    CHECK_FALSE(parse("LOAD 99999999999", &c));
}

TEST_CASE("remote_cmd: torn packets") {
    shell::LineAssembler la;
    Command out[4]{};
    CHECK(feed(la, "LO", out, 4) == 0);
    CHECK(feed(la, "AD 3\r", out, 4) == 0);
    int n = feed(la, "\nPI", out, 4);
    REQUIRE(n == 1);
    CHECK(out[0].kind == CmdKind::Load);
    CHECK(out[0].a == 3);
    n = feed(la, "NG\n", out, 4);
    REQUIRE(n == 1);
    CHECK(out[0].kind == CmdKind::Ping);
}

TEST_CASE("remote_cmd: several lines in one packet, and max_out bounds the output") {
    shell::LineAssembler la;
    Command out[4]{};
    CHECK(feed(la, "PING\nBOOT\nBAD\nHOME\n", out, 4) == 3);
    CHECK(out[0].kind == CmdKind::Ping);
    CHECK(out[1].kind == CmdKind::Boot);
    CHECK(out[2].kind == CmdKind::Home);
    // A full output array never overruns: the excess commands are dropped.
    Command two[2]{};
    CHECK(feed(la, "PING\nPING\nPING\n", two, 2) == 2);
}

TEST_CASE("remote_cmd: overlong line is discarded to its newline") {
    shell::LineAssembler la;
    Command out[4]{};
    char junk[41];
    std::memset(junk, 'A', 40);
    junk[40] = 0;
    CHECK(feed(la, junk, out, 4) == 0);
    int n = feed(la, "\nPING\n", out, 4);
    REQUIRE(n == 1);
    CHECK(out[0].kind == CmdKind::Ping);

    // The tail of an overlong line must not be mistaken for a command: 32
    // junk characters, then "PING" on the same line, then a real PING line.
    std::memset(junk, 'A', 32);
    junk[32] = 0;
    CHECK(feed(la, junk, out, 4) == 0);
    n = feed(la, "PING\nPING\n", out, 4);
    CHECK(n == 1);
}

TEST_CASE("remote_cmd: a line of exactly 31 characters still parses") {
    shell::LineAssembler la;
    Command out[2]{};
    // "LOAD " + 26 digits-with-leading-zeros = 31 chars, value 5.
    char line[40];
    std::memcpy(line, "LOAD ", 5);
    std::memset(line + 5, '0', 25);
    line[30] = '5';
    line[31] = '\n';
    line[32] = 0;
    REQUIRE(std::strlen(line) == 32);
    int n = feed(la, line, out, 2);
    REQUIRE(n == 1);
    CHECK(out[0].a == 5);
}

TEST_CASE("remote_cmd: ring keeps order and refuses the ninth") {
    shell::CmdRing ring;
    Command c{};
    CHECK_FALSE(ring.pop(&c));
    for(int i = 0; i < 8; ++i) {
        Command p{CmdKind::Load, i, 0};
        CHECK(ring.push(p));
    }
    CHECK_FALSE(ring.push(Command{CmdKind::Ping, 0, 0}));
    for(int i = 0; i < 8; ++i) {
        REQUIRE(ring.pop(&c));
        CHECK(c.a == i);
    }
    CHECK_FALSE(ring.pop(&c));
    // Wrap-around: the indices keep working past the first lap.
    for(int lap = 0; lap < 5; ++lap) {
        CHECK(ring.push(Command{CmdKind::Gesture, lap, 0}));
        REQUIRE(ring.pop(&c));
        CHECK(c.a == lap);
    }
}
