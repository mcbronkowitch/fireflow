# Pre-M6 Milestone Order

**Date:** 2026-07-24
**Status:** approved in discussion

## Context

M6 is the hardware-facing firmware shell. The required hardware is not
currently available, while four approved engine-level designs remain
unimplemented. Those designs should become explicit milestones before M6
instead of remaining an unprioritized backlog.

## Decision

Keep all existing milestone identifiers stable and insert four milestones at
the end of the M5 series:

| Milestone | Scope | Source spec |
|-----------|-------|-------------|
| **M5i** | WAVE — four-voice PPG-style wavetable part engine | `2026-07-18-wave-engine-design.md` |
| **M5j** | STRING — four-voice Karplus-Strong part engine | `2026-07-18-string-engine-design.md` |
| **M5k** | ZAP — monophonic percussion part engine | `2026-07-18-zap-percussion-engine-design.md` |
| **M5l** | PULL — chord gravity between the two decks | `2026-07-19-pull-chord-gravity-design.md` |
| **M6** | Firmware shell and hardware bring-up | `2026-07-12-spotykach-firmware-shell-design.md` |

The required order is:

`M5i WAVE → M5j STRING → M5k ZAP → M5l PULL → M6`

PULL deliberately lands last before M6. M6 keeps its existing identifier so
all earlier specifications and documentation links remain valid.

## Documentation changes

- Add M5i–M5l to the roadmap status table as planned.
- Add matching detail sections before the M6 section.
- Change M6 from “next” to “after M5l” while keeping it planned.
- Mirror the status-table entries in the README.
- Do not change the implementation status of any source spec: all four remain
  designed but unimplemented until their respective implementation work lands.

## Scope

This change only prioritizes and documents existing approved designs. It does
not modify DSP code, choose new engine behavior, relax benchmark constraints,
or begin M6 hardware work.

## Verification

- Roadmap and README show the same M5i–M6 order and status.
- Every new milestone links to its existing source spec.
- No existing milestone identifier changes.
- M6 remains explicitly hardware-dependent and unimplemented.
