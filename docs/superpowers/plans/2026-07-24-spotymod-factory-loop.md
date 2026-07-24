# Spotymod Factory Loop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Embed the first four bars of the user's 110 BPM bass loop as Spotymod's sampler initialization sound.

**Architecture:** Keep the runtime contract unchanged by replacing only `res/factory.wav`. Add a small standard-library Python guard that validates the shipped WAV's RIFF format, channel count, sample rate, bit depth, and tempo-derived frame count; then package and install through the existing local VCV build script.

**Tech Stack:** Python 3 standard library, FFmpeg, RIFF/WAVE PCM, GNU Make/Rack SDK, PowerShell and Git Bash on Windows.

## Global Constraints

- Source: `D:\Audio Projekte\Samples_and_Presets\Bass\Loops\110_16_Schröggelbass_Warped_01.wav`.
- Use the first four 4/4 bars at 110 BPM.
- Target duration: 8.7272727 seconds.
- Target frame count at 48 kHz: 418,909 frames, with one-frame tolerance.
- Preserve stereo, 48 kHz, and 24-bit integer PCM.
- Do not normalize, fade, crossfade, compress, or otherwise process the audio.
- Do not change DSP, persistence, UI, parameter, or panel-layout code.

---

### Task 1: Guard and replace the factory WAV

**Files:**
- Create: `host/vcv/res/test_factory_wav.py`
- Modify: `host/vcv/res/factory.wav`
- Modify: `host/vcv/README.md:46-53`

**Interfaces:**
- Consumes: RIFF/WAVE bytes from `host/vcv/res/factory.wav`.
- Produces: a distributable 24-bit PCM stereo WAV at 48 kHz with 418,909 frames, validated by `python res/test_factory_wav.py`.

- [ ] **Step 1: Write the failing asset test**

Create `host/vcv/res/test_factory_wav.py`:

```python
#!/usr/bin/env python3
import struct
from pathlib import Path

WAV = Path(__file__).with_name("factory.wav")
RATE = 48_000
CHANNELS = 2
BITS = 24
EXPECTED_FRAMES = round(RATE * 4 * 4 * 60 / 110)


def riff_chunks(raw):
    assert raw[:4] == b"RIFF" and raw[8:12] == b"WAVE", "not RIFF/WAVE"
    pos = 12
    while pos + 8 <= len(raw):
        chunk_id = raw[pos:pos + 4]
        size = struct.unpack_from("<I", raw, pos + 4)[0]
        start = pos + 8
        end = start + size
        assert end <= len(raw), f"{chunk_id!r} runs past EOF"
        yield chunk_id, raw[start:end]
        pos = end + (size & 1)


def main():
    raw = WAV.read_bytes()
    chunks = dict(riff_chunks(raw))
    assert b"fmt " in chunks and b"data" in chunks, "missing fmt/data chunk"
    fmt = chunks[b"fmt "]
    tag, channels, rate, _byte_rate, block_align, bits = struct.unpack_from(
        "<HHIIHH", fmt
    )
    if tag == 0xFFFE:
        assert len(fmt) >= 40, "short extensible fmt chunk"
        tag = struct.unpack_from("<H", fmt, 24)[0]
    assert tag == 1, f"format tag {tag}, want integer PCM"
    assert channels == CHANNELS, f"{channels} channels, want {CHANNELS}"
    assert rate == RATE, f"{rate} Hz, want {RATE}"
    assert bits == BITS, f"{bits}-bit, want {BITS}-bit"
    frames = len(chunks[b"data"]) // block_align
    assert abs(frames - EXPECTED_FRAMES) <= 1, (
        f"{frames} frames, want {EXPECTED_FRAMES} +/- 1"
    )
    print(
        f"PASS -- factory.wav: {frames} frames, "
        f"{channels}ch, {rate} Hz, {bits}-bit PCM"
    )


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run the test and verify RED**

Run from `host/vcv`:

```powershell
python res/test_factory_wav.py
```

Expected: FAIL against the current file because it is 32-bit float at 44.1 kHz, not 24-bit PCM at 48 kHz.

- [ ] **Step 3: Render the minimal four-bar replacement**

Run from the repository root:

```powershell
ffmpeg -y -v error -i "D:\Audio Projekte\Samples_and_Presets\Bass\Loops\110_16_Schröggelbass_Warped_01.wav" -map_metadata -1 -vn -af "atrim=end_sample=418909" -c:a pcm_s24le "host\vcv\res\factory.wav"
```

This decodes and re-encodes the original 24-bit PCM samples only to truncate at
an exact sample boundary. It applies no gain, fade, resampling, compression, or
other filter.

- [ ] **Step 4: Update the sampler documentation**

Replace the factory-sample sentence in `host/vcv/README.md` with:

```markdown
Sampler on an empty part autoloads the embedded first four bars of the
project author's own 110 BPM bass loop (`res/factory.wav`) so the deck makes
sound on the very first gesture; it never overwrites content already in the
buffer, and a deliberate *Clear sample* stays cleared even if you flip ENG
back and forth.
```

- [ ] **Step 5: Verify GREEN and run existing panel guards**

Run from `host/vcv`:

```powershell
python res/test_factory_wav.py
python res/gen_panel.py
python res/test_panel.py
git diff --check
```

Expected:

```text
PASS -- factory.wav: 418909 frames, 2ch, 48000 Hz, 24-bit PCM
PASS -- panel guards ok
```

- [ ] **Step 6: Inspect and commit the self-contained asset change**

```powershell
git status --short
git diff --stat
git add host/vcv/res/test_factory_wav.py host/vcv/res/factory.wav host/vcv/README.md
git diff --cached --check
git commit -m "feat(vcv): embed four-bar factory loop"
```

Expected: exactly the test, WAV, and README are included in the commit.

### Task 2: Package, install, and verify the plugin

**Files:**
- Verify: `host/vcv/dist/Spotymod/res/factory.wav`
- Verify: `C:\Users\bernd\AppData\Local\Rack2\plugins-win-x64\Spotymod\res\factory.wav`
- Verify: `C:\Users\bernd\AppData\Local\Rack2\plugins-win-x64\Spotymod-2.11.0-win-x64.vcvplugin`

**Interfaces:**
- Consumes: the committed `host/vcv/res/factory.wav` and existing local build script.
- Produces: synchronized packaged and unpacked Rack2 plugin assets.

- [ ] **Step 1: Confirm Rack is not holding the plugin open**

```powershell
Get-Process Rack -ErrorAction SilentlyContinue
```

Expected: no process. If Rack is running, close it normally before continuing.

- [ ] **Step 2: Build and install from the feature worktree**

Run:

```powershell
& 'C:\Program Files\Git\bin\bash.exe' -lc 'source /c/Users/bernd/Documents/AI/Spotykach/host/vcv/build-local.sh install RACK_DIR=/c/Users/bernd/Documents/AI/Rack-SDK' 'C:/Users/bernd/Documents/AI/Spotykach/.worktrees/vcv-face-cleanup/host/vcv/build-local.sh'
```

Expected: exit code 0, versioned archive emitted, unpacked plugin synchronized,
and `Restart Rack.` printed.

- [ ] **Step 3: Compare source, package, and installed asset hashes**

```powershell
$wt = 'C:\Users\bernd\Documents\AI\Spotykach\.worktrees\vcv-face-cleanup\host\vcv'
$installed = 'C:\Users\bernd\AppData\Local\Rack2\plugins-win-x64\Spotymod'
Get-FileHash "$wt\res\factory.wav","$wt\dist\Spotymod\res\factory.wav","$installed\res\factory.wav" -Algorithm SHA256
Get-FileHash "$wt\dist\Spotymod\plugin.dll","$installed\plugin.dll" -Algorithm SHA256
Get-FileHash "$wt\res\Spotymod.svg","$installed\res\Spotymod.svg" -Algorithm SHA256
Get-Item 'C:\Users\bernd\AppData\Local\Rack2\plugins-win-x64\Spotymod-2.11.0-win-x64.vcvplugin'
```

Expected: all three WAV hashes match; DLL hashes match; SVG hashes match; the
versioned archive exists with a current timestamp.

- [ ] **Step 4: Run final verification**

From `host/vcv`:

```powershell
python res/test_factory_wav.py
python res/test_panel.py
git status --short
```

Expected: both tests pass and Git reports a clean worktree.
