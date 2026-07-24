# Factory-loop final-review fix report

## Status

DONE

## Fix wave

- Hardened `host/vcv/res/test_factory_wav.py` to reject a `data` chunk whose
  length is not divisible by `block_align`.
- Added exact derived-header checks for `block_align` and `byte_rate`.
- Added `host/vcv/res/test_factory_wav_validation.py`, which first verifies
  rejection of minimally corrupted data length, block alignment, and byte
  rate cases, then verifies acceptance of the real `factory.wav` asset.
- Corrected the faceplate layout plan and design spec to describe the
  one-line `Spotymod.cpp` `c.tip` compatibility wiring: runtime parameter
  names/tooltips remain stable while generator data controls captions.
- Removed the line-3 Markdown trailing whitespace from the label
  micro-adjustments spec.

`factory.wav`, `res/Spotymod.svg`, and `src/generated_panel.hpp` were not
modified by this fix wave.

## TDD evidence

Before the validator implementation, the new synthetic regression harness
failed because a RIFF/data-size-consistent partial-frame corruption was
accepted:

```text
PASS -- factory.wav: 418909 frames, 2ch, 48000 Hz, 24-bit PCM
Traceback (most recent call last):
  ... AssertionError: corrupted WAV was accepted
```

After implementation, the harness rejects three minimally corrupted cases
(partial data frame, incorrect `block_align`, and incorrect `byte_rate`)
before accepting the real asset.

## Verification outputs

```text
$ python res/test_factory_wav_validation.py
PASS -- factory.wav: 418909 frames, 2ch, 48000 Hz, 24-bit PCM
PASS -- factory WAV synthetic validation cases and real asset

$ python res/test_factory_wav.py
PASS -- factory.wav: 418909 frames, 2ch, 48000 Hz, 24-bit PCM

$ python res/gen_panel.py
wrote res/Spotymod.svg and src/generated_panel.hpp
params=80 (stride=23) inputs=4 outputs=6 lights=4  panel=42HP

$ python res/test_panel.py
PASS -- panel guards ok
```

`git diff --exit-code -- res/Spotymod.svg src/generated_panel.hpp` confirmed
the generator left both generated artifacts unchanged. `git diff --check`
was clean before committing; `git diff --check main...HEAD` was run again
after committing.

## Commit

This report is included in the one fix-wave commit. Resolve its exact hash
with `git rev-parse HEAD` in this worktree.

## Concerns

None. Git emitted environment-level warnings that it could not access the
user global ignore file and CRLF-normalization notices; neither affected the
commands' zero exit status or repository content checks.
