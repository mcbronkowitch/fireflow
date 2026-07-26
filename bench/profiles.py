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
    # The complete run, as before profiles existed. Expected to FAIL TO LINK
    # until the engine shrinks or the region grows -- that debt is real and is
    # meant to be visible to whoever runs the bare command.
    "full": Profile(
        families=(
            "system", "voice", "mem", "mod", "abl", "taps", "sampler",
        ),
        gates=frozenset({WAVE_ACCEPTANCE}),
    ),
}

DEFAULT_PROFILE = "full"


def resolve(name):
    """Look up a profile by name, or raise with the valid names listed."""
    try:
        return PROFILES[name]
    except KeyError:
        raise KeyError(
            "unknown bench profile %r (known: %s)"
            % (name, ", ".join(sorted(PROFILES)))
        )
