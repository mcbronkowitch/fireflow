"""Host test for click_detect.py (spec 2026-10-18-cpu-headroom-design.md
section 2.6): a dropped audio block is a click, a clean tone and a quiet
passage are not. Plain script, exit code is the verdict."""
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import numpy as np
import click_detect as cd

FAILS = []
def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond: FAILS.append(name)

SR = 48000
BLOCK = 96
t = np.arange(10 * SR) / SR
tone = 0.3 * np.sin(2 * np.pi * 220.0 * t)

def drop_block(x, at_s):
    y = x.copy()
    i = int(at_s * SR)
    y[i:i + BLOCK] = 0.0
    return y

clicks = cd.find_clicks(tone, SR)
check("a clean tone has no click", clicks == [])

clicks = cd.find_clicks(drop_block(tone, 5.0), SR)
check("one dropped block is one click", len(clicks) == 1)
check("... found where it was dropped", len(clicks) == 1 and abs(clicks[0][0] - 5.0) < 0.01)

clicks = cd.find_clicks(drop_block(drop_block(tone, 2.0), 8.0), SR)
check("two dropped blocks 6 s apart are two clicks", len(clicks) == 2)
check("... at their times", len(clicks) == 2
      and abs(clicks[0][0] - 2.0) < 0.01 and abs(clicks[1][0] - 8.0) < 0.01)

ramp = tone.copy()
i0, i1 = 3 * SR, 4 * SR
ramp[i0:i1] *= np.linspace(1.0, 0.01, i1 - i0)
ramp[i1:] *= 0.01
check("a quiet passage is not a click", cd.find_clicks(ramp, SR) == [])

# ffmpeg's capture starts on whatever the converter holds: every board
# recording of 2026-10-18 opened with an "event" at t = 0.
cut_in = tone.copy()
cut_in[:5] = 0.0
check("the recording's first moment is not a click", cd.find_clicks(cut_in, SR) == [])

sys.exit(1 if FAILS else 0)
