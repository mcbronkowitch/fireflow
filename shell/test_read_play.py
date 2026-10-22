"""Guard for read_play.py. Plain asserts, exit code is the verdict --
pytest is not installed here. ctest runs it as read_play_guard.

Every fixture is built the way the Rev A play image prints
(panel_scan.cpp, run_panel_scan_report()), and each one must drive the
verdict it is named for.
"""
import io
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import read_play as r

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


ROWS = 73
RAIL = 63484
ZERO = 20


def frame_lines(values=None, zero=ZERO, rail=RAIL, valid=1, sweeps=100,
                keys=0, presses=(0, 0, 0, 0), law=None, drop_v=None):
    """One report frame as run_panel_scan_report() prints it for Rev A:
    eight SHELL_PLAY_V lines of up to ten values, SHELL_PLAY, SHELL_PLAY_LAW.
    Defaults: every row at 500, span valid, nothing pressed."""
    values = [500] * ROWS if values is None else list(values)
    assert len(values) == ROWS
    law = law or dict(cyc_last=11800, cyc_max=13900, led_last=1200,
                      led_max=1580, cb_last=889912, cb_max=873150)
    lines = []
    for r0 in range(0, ROWS, 10):
        if drop_v == r0:
            continue
        chunk = values[r0:r0 + 10]
        lines.append("SHELL_PLAY_V r=%d %s" % (r0, " ".join(str(v) for v in chunk)))
    lines.append("SHELL_PLAY zero=%d rail=%d valid=%d sweeps=%d keys=%d "
                 "presses=%d,%d,%d,%d"
                 % (zero, rail, valid, sweeps, keys, *presses))
    lines.append("SHELL_PLAY_LAW cyc_last=%d cyc_max=%d led_last=%d led_max=%d "
                 "cb_last=%d cb_max=%d"
                 % (law["cyc_last"], law["cyc_max"], law["led_last"],
                    law["led_max"], law["cb_last"], law["cb_max"]))
    return lines


def with_row(row, value, base=None):
    v = [500] * ROWS if base is None else list(base)
    v[row] = value
    return v


def quiet_report(frames, **kw):
    return r.report(frames, out=io.StringIO(), **kw)


def test_row_names_come_from_the_generated_header():
    names = r.row_names()
    check(len(names) == ROWS, "expected %d row names, got %d" % (ROWS, len(names)))
    check(names[0] == "ENGINE_A", "row 0 is %r, not ENGINE_A" % (names[:1],))
    check(names[50] == "TEMPO", "row 50 is %r, not TEMPO" % (names[50:51],))
    check(names[72] == "REV_MIX_B", "row 72 is %r, not REV_MIX_B" % (names[72:73],))


def test_clean_frame_parses_whole():
    frames = r.parse_frames(frame_lines(sweeps=4242, keys=5, presses=(1, 2, 3, 4)))
    check(len(frames) == 1, "expected one frame, got %d" % len(frames))
    if not frames:
        return
    f = frames[0]
    check(len(f["v"]) == ROWS and all(x == 500 for x in f["v"]),
          "values did not parse")
    check(f["play"]["sweeps"] == 4242 and f["play"]["keys"] == 5,
          "SHELL_PLAY fields did not parse")
    check(f["play"]["presses"] == [1, 2, 3, 4], "presses did not parse")
    check(f["law"]["cb_max"] == 873150, "SHELL_PLAY_LAW did not parse")


def test_a_frame_missing_a_value_line_is_not_a_frame():
    check(r.parse_frames(frame_lines(drop_v=30)) == [],
          "a frame with a missing SHELL_PLAY_V line was accepted")


def test_a_frame_without_the_law_line_is_not_a_frame():
    lines = frame_lines()[:-1]
    check(r.parse_frames(lines) == [], "a frame without SHELL_PLAY_LAW was accepted")


def test_two_frames_in_a_row_both_parse():
    lines = frame_lines(sweeps=10) + frame_lines(sweeps=20)
    frames = r.parse_frames(lines)
    check([f["play"]["sweeps"] for f in frames] == [10, 20],
          "two consecutive frames did not both parse")


def test_a_torn_frame_does_not_leak_into_the_next():
    a = frame_lines(sweeps=10)
    b = frame_lines(sweeps=20)
    # Host lost the tail of frame a: its V lines are followed by b's first V line.
    mixed = a[:5] + b
    frames = r.parse_frames(mixed)
    check(len(frames) == 1 and frames[0]["play"]["sweeps"] == 20,
          "a torn frame leaked into the next one")


def test_coupon_lines_are_refused():
    lines = ["SHELL_PLAY rv2=500 rv4=500 rv6=500 zero=20 rail=63484 valid=1 sweeps=9",
             "SHELL_PLAY_IO keys=0 presses=0 adc11=0 adc12=0"]
    check(r.parse_frames(lines) == [], "coupon SHELL_PLAY lines made a frame")
    check(r.is_coupon_capture(lines), "coupon capture not recognised")


def test_stops_per_row():
    frames = r.parse_frames(
        frame_lines(with_row(5, 0)) + frame_lines(with_row(5, 480))
        + frame_lines(with_row(5, 1000)) + frame_lines(with_row(7, 0)))
    s = r.stops(frames)
    check(s[5] == (True, True), "row 5 swept 0..1000 but stops=%r" % (s[5],))
    check(s[7] == (True, False), "row 7 reached 0 only but stops=%r" % (s[7],))
    check(s[3] == (False, False), "row 3 never moved but stops=%r" % (s[3],))


def test_never_emitted_rows_are_listed():
    frames = r.parse_frames(frame_lines(with_row(44, -1000)))
    check(r.never_emitted(frames) == [44], "row 44 at -1000 was not listed")
    # A row that emits later is not "never".
    frames = r.parse_frames(frame_lines(with_row(44, -1000))
                            + frame_lines(with_row(44, 300)))
    check(r.never_emitted(frames) == [], "a row that emitted later was listed")


def test_rest_check_names_the_flickering_row():
    quiet = r.parse_frames(frame_lines() + frame_lines() + frame_lines())
    check(r.unsteady(quiet) == {}, "a quiet board was called unsteady")
    noisy = r.parse_frames(frame_lines() + frame_lines(with_row(29, 501))
                           + frame_lines())
    u = r.unsteady(noisy)
    check(list(u) == [29] and u[29] == (500, 501),
          "row 29 flickered 500/501 but unsteady=%r" % (u,))


def test_changes_between_frames():
    a = r.parse_frames(frame_lines())[0]
    b = r.parse_frames(frame_lines(with_row(61, 620)))[0]
    ch = r.changed(a, b)
    check(ch == [(61, "MORPH", 500, 620)], "changed() gave %r" % (ch,))
    check(r.changed(a, a) == [], "an identical frame reported a change")


def test_span_verdict():
    ok, why = r.span_ok(r.parse_frames(frame_lines())[0])
    check(ok, "a valid span was refused: %s" % why)
    bad = r.parse_frames(frame_lines(rail=58000, valid=0))[0]
    ok, why = r.span_ok(bad)
    check(not ok and "rail" in why, "rail 58000 passed: %s" % why)
    bad = r.parse_frames(frame_lines(zero=1400))[0]
    ok, why = r.span_ok(bad)
    check(not ok and "zero" in why, "zero 1400 passed: %s" % why)


def test_sweep_rate():
    frames = r.parse_frames(frame_lines(sweeps=100) + frame_lines(sweeps=1140))
    check(abs(r.sweeps_per_second(frames, 50.0) - 20.8) < 1e-9,
          "1040 sweeps over 50 s is not 20.8/s")
    check(r.sweeps_per_second(frames, None) is None,
          "a rate without a duration was invented")


def test_report_exit_codes():
    check(quiet_report([]) == 1, "no frames: exit code not 1")
    clean = r.parse_frames(frame_lines() + frame_lines())
    check(quiet_report(clean) == 0, "clean: exit code not 0")
    check(quiet_report(clean, rest=True) == 0, "clean --rest: exit code not 0")
    check(quiet_report(clean, stops=True) == 1,
          "--stops with no row at a stop: exit code not 1")
    noisy = r.parse_frames(frame_lines() + frame_lines(with_row(29, 501)))
    check(quiet_report(noisy, rest=True) == 1, "flicker --rest: exit code not 1")
    check(quiet_report(noisy) == 0, "flicker without --rest: exit code not 0")
    invalid = r.parse_frames(frame_lines(valid=0, rail=0, zero=0))
    check(quiet_report(invalid) == 1, "span never valid: exit code not 1")


def test_report_prints_names_and_cb_max():
    frames = r.parse_frames(frame_lines(with_row(50, 0)) + frame_lines(with_row(50, 1000)))
    out = io.StringIO()
    r.report(frames, out=out)
    text = out.getvalue()
    check("TEMPO" in text, "the report does not name row 50 TEMPO")
    check("873150" in text, "the report does not print cb_max")
    check(str(r.CB_MAX_INIT) in text, "the report does not quote the cb_max criterion")


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print("FAIL (%d)" % len(FAILS))
        for f in FAILS:
            print("  - " + f)
        sys.exit(1)
    print("read_play guard OK")
