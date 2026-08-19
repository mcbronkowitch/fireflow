"""Which workload families a bench image contains, and which acceptance gates
apply to it.

A profile names FAMILIES, never rows. run.py's BENCH_PROTOCOL_ROWS_BY_FAMILY
stays the single source of truth for rows and is merely filtered to the
profile's families -- so there is no second place where a row list can drift.

Gates: most of run.py's checks are universal and run for every profile. Only
`wave_acceptance` is profile-scoped, because it needs rows (synth_2x4,
wave_2x4) that only the `system` family supplies.
"""

from collections import namedtuple

Profile = namedtuple("Profile", "families gates")

WAVE_ACCEPTANCE = "wave_acceptance"

PROFILES = {
    # Carries the WAVE regression guard. Fits comfortably, which is the point:
    # that guard has been unenforceable since the full image stopped linking.
    "system": Profile(
        families=("system",),
        gates=frozenset({WAVE_ACCEPTANCE}),
    ),
    # The M5j gate: prices the BODY mode bank and KS pair. Carries `system`
    # as well, so the same document shows what one real synth voice and the
    # worst-case instrument cost -- the figures the kVoices ladder is judged
    # against -- and so the WAVE guard keeps running while BODY is measured.
    "body": Profile(
        families=("system", "body"),
        gates=frozenset({WAVE_ACCEPTANCE}),
    ),
    # The FEED kernel round (spec 2026-08-18). Carries `system` for the same
    # reason `body` and `sweep` do: without it verdict() finds no DTCM+BBD gate
    # anchor and reports "undetermined", and the whole question -- how many
    # pairs fit -- is only meaningful against the instrument's own worst case
    # measured in the SAME image (bench rows shift by points from icache
    # layout alone, so a cross-image subtraction is not a measurement).
    # `system` is also where inst_feed_engine_worst lives, which is the row
    # that actually judges whether FEED fits; feed_pairs only prices a pair.
    "feed": Profile(
        families=("system", "feed"),
        gates=frozenset({WAVE_ACCEPTANCE}),
    ),
    # The BBD gate, on its own. `system` is deliberately NOT carried: the
    # question this profile answers -- what does one BBD line cost at its
    # ceiling -- is answered by the three bbd rows alone, and leaving system
    # out is what makes it a small image that links without argument.
    #
    # The price is that `verdict()` finds no DTCM+BBD gate anchor and
    # reports "undetermined" rather than an anchored figure. That is correct
    # and not a defect: this profile prices a component, it does not judge
    # whether the instrument fits. Use `full` (or a later profile that carries
    # `system`) for that.
    "bbd": Profile(
        families=("bbd",),
        gates=frozenset(),
    ),
    # The cost-curve round (spec 2026-07-29-fx-cost-curves-design). Carries
    # `system` deliberately, unlike `bbd`: without it verdict() has no
    # DTCM+BBD gate anchor and reports "undetermined", which is exactly
    # how the BBD numbers came to stand for two days without a system
    # verdict. `body` (system + body) is the precedent that a two-family
    # image links.
    "sweep": Profile(
        families=("system", "sweep"),
        gates=frozenset({WAVE_ACCEPTANCE}),
    ),
    # Gate rows and BBD kernel rows in ONE image. `system` supplies the
    # decision gate and its anchors; `bbd` supplies the kernel rows. Two
    # families in one image is the whole point:
    # 2026-07-31-b9afe47-bbd-engine.md left "what would settle it is a
    # same-build A/B" open, and a cross-image subtraction is not a measurement
    # -- composition and layout move the gate by points at an unchanged
    # checksum. `body` and `sweep` are the precedents that a two-family image
    # links.
    #
    # NOT a one-off: this is the profile for ANY future gate-versus-BBD A/B,
    # not just the 2026-08-04 signal-path round that introduced it (spec
    # 2026-08-04-signal-path-regression-bench-design). Reuse it rather than
    # adding a date-named twin.
    #
    # TRAP -- `--profile regress --itcm-hot` cannot pass placement at ANY
    # optimization level, and the failure looks like a hardware or linker-script
    # bug if you meet it cold. spky::BbdLine::Process is emitted per TU as a
    # weak/COMDAT symbol; under this profile the linker keeps the copy in
    # build/workloads_bbd.o -- a bench-harness TU that bench/itcm_hot.lds does
    # not list -- and discards the engine objects' copies, so the surviving
    # symbol lands outside ITCM and itcm_placement.py fail-closes on it. At -O3
    # there is a second, independent failure on top: the hot section overflows
    # the 64 KiB region and the link does not complete at all. Do NOT "fix" it
    # by adding workloads_bbd.o to the hotset -- that puts bench-harness code in
    # ITCM and distorts every measurement ever taken there. Measured and written
    # up in docs/bench/2026-08-04-2101349-signal-path-regression.md ("The ITCM
    # finding"); use `axi` with this profile until the hotset definition is
    # reworked, which is M6 work.
    "regress": Profile(
        families=("system", "bbd"),
        gates=frozenset({WAVE_ACCEPTANCE}),
    ),
    # The complete run, as before profiles existed. Expected to FAIL TO LINK
    # until the engine shrinks or the region grows -- that debt is real and is
    # meant to be visible to whoever runs the bare command.
    "full": Profile(
        families=(
            "system", "voice", "mem", "mod", "abl", "bbd", "body", "sampler",
        ),
        gates=frozenset({WAVE_ACCEPTANCE}),
    ),
    # The instrument-level ablation (spec 2026-07-29-instrument-ablation).
    # Carries `system` for two reasons, not one: it supplies the ladder's
    # fourth rung (instrument_worst_bbd, which the whole round is measured
    # against), and without a DTCM+BBD gate anchor verdict() reports
    # "undetermined" -- which is how the BBD figures stood for two days with
    # no verdict attached.
    "ablate": Profile(
        families=("system", "instr"),
        gates=frozenset({WAVE_ACCEPTANCE}),
    ),
}

DEFAULT_PROFILE = "full"


def resolve(name, rows_by_family):
    """Look up a profile by name and validate its manifest, or raise with a
    clear diagnosis naming the profile and the problem.

    A profile that fails validation here is a manifest error, caught before
    anything is built, flashed, or measured. Two things are checked:

    - every family the profile names must be a family run.py actually has
      row expectations for (a typo, or a family that is not compiled into
      run.py's expectations yet -- see the spec's note on the `body`
      profile);
    - a profile that declares WAVE_ACCEPTANCE must have families that,
      between them, supply the two rows the gate compares (synth_2x4,
      wave_2x4). Declaring the gate without the rows would otherwise only
      be caught after a full hardware repeat -- see the design spec S3.

    rows_by_family is run.py's BENCH_PROTOCOL_ROWS_BY_FAMILY, passed in
    rather than imported: run.py already imports this module, so importing
    run.py from here would be circular. This also keeps
    BENCH_PROTOCOL_ROWS_BY_FAMILY exactly where the spec says it belongs --
    run.py's own, independent, hand-maintained expectation -- instead of
    duplicating or relocating it.
    """
    try:
        profile = PROFILES[name]
    except KeyError:
        raise KeyError(
            "unknown bench profile %r (known: %s)"
            % (name, ", ".join(sorted(PROFILES)))
        )

    unknown_families = [f for f in profile.families if f not in rows_by_family]
    if unknown_families:
        raise ValueError(
            "bench profile %r names families with no known rows: %s -- "
            "run.py's BENCH_PROTOCOL_ROWS_BY_FAMILY has no entry for "
            "them (typo, or the family is not compiled into run.py's row "
            "expectations yet)"
            % (name, ", ".join(unknown_families))
        )

    if WAVE_ACCEPTANCE in profile.gates:
        required = {"synth_2x4", "wave_2x4"}
        supplied = {
            row_name
            for family in profile.families
            for row_name in rows_by_family[family]
        }
        missing = required - supplied
        if missing:
            raise ValueError(
                "bench profile %r declares wave_acceptance but its "
                "families (%s) do not supply %s -- add a family that "
                "carries them, or drop the gate from this profile"
                % (
                    name,
                    ", ".join(profile.families) or "none",
                    ", ".join(sorted(missing)),
                )
            )

    return profile
