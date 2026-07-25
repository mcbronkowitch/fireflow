# README and Roadmap Refresh

**Date:** 2026-07-25  
**Status:** Approved design  
**Scope:** `README.md` and `docs/roadmap.md`

## Purpose

Bring the public project summary and detailed roadmap up to the released
Spotymod 2.13.1 state without restructuring the documentation or making claims
that cannot be verified from the repository.

## Sources of truth

Every changed statement must be supported by at least one of:

- the current `main` implementation;
- the VCV manifest and release tags;
- passing executable tests or committed benchmark evidence;
- an approved design specification already in the repository.

Do not add dates, hardware-readiness claims, release promises, or future scope
that is not already supported by those sources.

## README changes

Keep the README's existing structure and voice. Update only stale or missing
current-state information:

- identify 2.13.1 as the current VCV release;
- retain Synth, Sampler, and WAVE as the three implemented production engines;
- describe the contextual SOURCE control and independent Detune setting without
  duplicating the VCV manual;
- explain that melodic STEP playback now separates:
  - FORM, which chooses one of five phrase engines; and
  - SONG, which arranges two persistent A/B phrase snapshots;
- name the seven SONG choices: AAAB, ABAB, ABBB, BUILD, ROTATE, MIRROR, and OFF;
- show the current PLAY row as `STEP · FORM · SONG · NEW`;
- state that NEW rebuilds the A/B phrase pair and additionally punches an
  active Sampler;
- do not present TRIG as a current panel control;
- replace the stale architecture-image test count with the executable count of
  680 Doctest cases;
- mark M5h as released in 2.11.0;
- add the completed FORM/SONG work to the summary roadmap;
- preserve the warning that the modulation-first engine has not yet been run
  through the unimplemented M6 firmware shell on Spotykach hardware.

## Roadmap changes

Preserve the milestone-oriented structure. Refresh its header and status table,
then add one completed detail section before `Planned` for the cross-cutting
phrase-arrangement work delivered in 2.13.1.

The new entry must document:

- persistent full-pattern snapshots A and B;
- FORM's five phrase engines: TWO MOTIFS, ONE + VAR, HIERARCHICAL,
  CALL / RESPONSE, and OSTINATO;
- SONG's seven arrangements and their exact compact forms:
  - AAAB;
  - ABAB;
  - ABBB;
  - BUILD: `AAAB · AABB · ABBB · AABB`;
  - ROTATE: `AAAB · AABA · ABAA · BAAA`;
  - MIRROR: deterministic Thue–Morse selection;
  - OFF: A continues playing and evolving while B remains stored;
- phrase-boundary application of FORM, SONG, NEW, and effective STEPS changes;
- SONG-only changes preserving A/B content;
- NEW always rebuilding both phrases and additionally punching a Sampler;
- VCV's `STEP · FORM · SONG · NEW` panel row and removed TRIG control;
- stable parameter IDs plus legacy `principle` and `lastBasis` migration;
- renderer scenarios and executable coverage as verification evidence;
- release 2.13.1.

Clarify the verification reminder: the portable engine is exercised through
the desktop renderer and live VCV host, selected CPU workloads have hardware
benchmark evidence, but the modulation-first M6 firmware shell remains
unimplemented. STRING remains the next planned engine milestone.

## Non-goals

- Reorganizing the README or roadmap around a release-history format.
- Turning either file into a complete VCV control manual.
- Changing milestone order.
- Marking STRING, ZAP, PULL, or M6 as started.
- Claiming that the modulation-first firmware runs on Spotykach hardware.
- Editing implementation code, generated assets, or the VCV manual.

## Verification

- Search both files for stale release strings and removed panel terminology.
- Confirm the documented VCV version equals `host/vcv/plugin.json`.
- Confirm every FORM and SONG name matches the engine and VCV switch tables.
- Confirm README and roadmap agree about completed and planned milestones.
- Run the existing VCV panel guard because it encodes the current control
  labels and user-documentation contract.
- Run `git diff --check`.
