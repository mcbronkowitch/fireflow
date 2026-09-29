# P4a routing spike (throwaway harness)

Spec: `docs/superpowers/specs/2026-09-29-rev-a-p4a-routing-spike-design.md`.
Report: `docs/hardware/routing-spike.md`.

Runs under KiCad's Python:

    "/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe" hardware/reva/spike/run.py --method none --layers 2

Everything here is spike code and goes when P4 starts. What outlives it lives
in `hardware/gen/` (`kipcb.py`, `pcb_proof.py`, and `route.py` if our own
router wins). Outputs land in `out/`, gitignored.
