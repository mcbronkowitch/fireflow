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
    # The complete run, as before profiles existed. Expected to FAIL TO LINK
    # until the engine shrinks or the region grows -- that debt is real and is
    # meant to be visible to whoever runs the bare command.
    "full": Profile(
        families=(
            "system", "voice", "mem", "mod", "abl", "body", "sampler",
        ),
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
