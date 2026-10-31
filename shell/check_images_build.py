"""Build guard: compile and link the shell images that matter with the ARM
toolchain, and report each one's SRAM_EXEC use. Plain script, exit code is
the verdict; 77 means skipped (no DaisyToolchain, or libDaisy/DaisySP not
built yet), which ctest reports as "Skipped".

Why it exists: on 2026-10-09 (commit 450eed36) the Rev A play image stopped
compiling after the 9 mm panel pass, and ctest stayed green, because the
shell guards regenerate and compare tables but nothing built shell/.

Every image is built into its own, freshly emptied BUILD_DIR. That is the
whole defence against the stale-object trap: make has 1-second mtime
resolution here, and the switch headers are resolved at parse time, so an
incremental build after a switch change can relink a stale object. A full
build of an empty directory has nothing stale to relink. shell/build, where
the images that get flashed live, is never touched.

The environment is scrubbed of what env.sh sets for the desktop build (CC,
CXX, CMAKE_GENERATOR, the LLVM directory on PATH), because ctest normally
runs from a shell that sourced it and the two toolchains must not mix.

Some images also have symbol rules (SYMBOL_RULES): the SHELL_REMOTE images
must not link the panel-state save path, and the plain play image must (the
canary that shows the listing is read at all).

    python check_images_build.py [OUT_DIR]

OUT_DIR defaults to ../build/shell_images.
"""
import os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SKIP = 77

# The images a broken build must never get past: the default image, the
# Rev A play image as it is flashed (memory: Rev A play image flags), and the
# coupon play image. reva_play persists the panel state (SHELL_PERSIST
# defaults to 1); reva_play_nopersist is the same image with today's init-patch
# boot, the one the desktop-render comparisons use (spec
# 2026-10-28-panel-state-persistence-design.md section 4.2); reva_play_stress
# is the board probes' image.
IMAGES = [
    ("default", []),
    ("reva_play", ["SHELL_PANEL_SCAN=1", "SHELL_KEYS_NC=1"]),
    ("reva_play_nopersist", ["SHELL_PANEL_SCAN=1", "SHELL_KEYS_NC=1", "SHELL_PERSIST=0"]),
    # The persistence probe image (plan 2026-10-28, Task 6): forced saves.
    ("reva_play_stress", ["SHELL_PANEL_SCAN=1", "SHELL_KEYS_NC=1", "SHELL_PERSIST_STRESS=1"]),
    ("coupon_play", ["SHELL_PANEL_SCAN=1", "SHELL_COUPON_PROBE=1"]),
    # The remote CPU hunt's listener image (plan 2026-10-29-cpu-hunt-remote):
    # workloads, gestures and the overrun guard, its tables in QSPI.
    ("reva_remote", ["SHELL_PANEL_SCAN=1", "SHELL_KEYS_NC=1", "SHELL_REMOTE=1"]),
    # Its measurement image (plan Task 6): sections, the outside-window loop,
    # LATE and the candidate toggles, with the probe stamps on.
    ("reva_remote_measure", ["SHELL_PANEL_SCAN=1", "SHELL_KEYS_NC=1", "SHELL_REMOTE=1",
                             "SHELL_REMOTE_MEASURE=1"]),
    # The two class-2 CPU savings are ON in every image above (SHELL_REVERB_HALF
    # and SHELL_FAST_MATH default to 1; shell/README.md, "Turning a CPU saving
    # back"). Their switches back, in the listener image:
    ("reva_remote_off", ["SHELL_PANEL_SCAN=1", "SHELL_KEYS_NC=1", "SHELL_REMOTE=1",
                         "SHELL_REVERB_HALF=0", "SHELL_FAST_MATH=0"]),
    # The measurement image with BOTH reverb tanks (toggle bit 9 selects).
    ("reva_remote_measure_dispatch", ["SHELL_PANEL_SCAN=1", "SHELL_KEYS_NC=1", "SHELL_REMOTE=1",
                                      "SHELL_REMOTE_MEASURE=1", "SHELL_REVERB_HALF=0"]),
    # The lean switches (CPU round 2's class-2 candidates, shell/Makefile,
    # SHELL_LEAN_*) default to 0; the listener image with every one of them
    # at 1, so a candidate's firmware-only path compiles and links.
    ("reva_remote_lean", ["SHELL_PANEL_SCAN=1", "SHELL_KEYS_NC=1", "SHELL_REMOTE=1",
                          "SHELL_LEAN_SVF1=1", "SHELL_LEAN_GRAIN_WIN=1"]),
    # Full -ffast-math folds the engine's isfinite guards: the image build must
    # REFUSE it (math_guards.py on the linked ELF). Expected to fail; passing
    # this entry means the guard check can still go red.
    ("reva_remote_fm2_refused", ["SHELL_PANEL_SCAN=1", "SHELL_KEYS_NC=1", "SHELL_REMOTE=1",
                                 "SHELL_FAST_MATH=2"]),
]

# Images whose build must be refused, and the refusal's text (name -> text).
EXPECT_REFUSED = {
    "reva_remote_fm2_refused": "math guard: isfinite guard folded away in",
}

# The save path, as linked symbols (nm -C): persist_write() and the QSPI
# erase and program entry points it is the only caller of. A SHELL_REMOTE
# image suppresses saves at build time (spec 2026-10-29 section 5.3: a loaded
# workload must not replace Bastian's saved patch, and an erase would unmap
# the QSPI under the workload tables), so it must not link any of them --
# which is a fact about the image, not about the source, and is checked here
# on the image (shell/test_remote_wiring.py holds the source side). The plain
# play image MUST link them: that is the canary that proves this check can
# see the symbols at all, so it cannot pass on an empty or misread listing.
SAVE_PATH = [
    "shell::persist_write(",
    "daisy::QSPIHandle::EraseSector(",
    "daisy::QSPIHandle::Write(",
    "daisy::QSPIHandle::Impl::Write(",
    "daisy::QSPIHandle::Impl::WritePage(",
]
# name -> (symbols that must be linked, symbols that must not be)
SYMBOL_RULES = {
    "reva_play": (SAVE_PATH, []),
    "reva_remote": ([], SAVE_PATH),
    "reva_remote_measure": ([], SAVE_PATH),
}


def linked_names(nm_text):
    """The symbol names of an `nm -C` listing (address, type, name)."""
    names = []
    for line in nm_text.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3:
            names.append(parts[2])
        elif len(parts) == 2:   # undefined: type, name
            names.append(parts[1])
    return names


def symbol_problems(nm_text, must_have, must_lack):
    """What breaks the rules: ['missing X', 'linked X', ...]; [] when none."""
    names = linked_names(nm_text)
    out = []
    for want in must_have:
        if not any(n.startswith(want) for n in names):
            out.append("missing " + want)
    for bad in must_lack:
        for n in names:
            if n.startswith(bad):
                out.append("linked " + n)
    return out

TOOLCHAIN_BIN = os.environ.get("DAISY_TOOLCHAIN_BIN",
                               r"C:\Program Files\DaisyToolchain\bin")
GIT_USR_BIN = r"C:\Program Files\Git\usr\bin"

# --print-memory-usage (libDaisy's LDFLAGS) prints one line per region:
#   "       SRAM_EXEC:      261340 B     262880 B     99.41%"
MEM_LINE = re.compile(r"^\s*SRAM_EXEC:\s+(\d+)\s*([KMG]?B)\s+(\d+)\s*([KMG]?B)\s+([\d.]+)%",
                      re.M)
UNIT = {"B": 1, "KB": 1024, "MB": 1024 ** 2, "GB": 1024 ** 3}


def sram_exec(output):
    """(used, size) in bytes from the linker's memory report, or None."""
    m = MEM_LINE.search(output)
    if not m:
        return None
    return int(m.group(1)) * UNIT[m.group(2)], int(m.group(3)) * UNIT[m.group(4)]


def clean_env():
    env = dict(os.environ)
    for k in ("CC", "CXX", "CMAKE_GENERATOR", "CFLAGS", "CXXFLAGS", "LDFLAGS"):
        env.pop(k, None)
    keep = [p for p in env.get("PATH", "").split(os.pathsep)
            if p and "llvm" not in p.lower()]
    # The Makefile calls `python` for its switch writers: the interpreter
    # running this guard is the one that must answer.
    env["PATH"] = os.pathsep.join(
        [TOOLCHAIN_BIN, GIT_USR_BIN, os.path.dirname(sys.executable)] + keep)
    return env


def make_path(path):
    """BUILD_DIR as make sees it: relative to shell/ where possible (a drive
    letter's colon in a target name is trouble for make), forward slashes."""
    try:
        path = os.path.relpath(path, HERE)
    except ValueError:  # another drive
        pass
    return path.replace("\\", "/")


def main():
    out_root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1
                               else os.path.join(REPO, "build", "shell_images"))
    gcc = os.path.join(TOOLCHAIN_BIN, "arm-none-eabi-gcc.exe" if os.name == "nt"
                       else "arm-none-eabi-gcc")
    if not os.path.isfile(gcc):
        print("SKIP: no DaisyToolchain at %s (set DAISY_TOOLCHAIN_BIN)" % TOOLCHAIN_BIN)
        return SKIP
    for lib in ("lib/libDaisy/build/libdaisy.a", "lib/DaisySP/build/libdaisysp.a"):
        if not os.path.isfile(os.path.join(REPO, lib)):
            print("SKIP: %s is missing -- build the libraries once (CLAUDE.md, "
                  "'Daisy firmware')" % lib)
            return SKIP

    env = clean_env()
    make = shutil.which("make", path=env["PATH"])
    if not make:
        print("SKIP: no make on the DaisyToolchain PATH")
        return SKIP

    fails = []
    for name, switches in IMAGES:
        build_dir = os.path.join(out_root, name)
        shutil.rmtree(build_dir, ignore_errors=True)
        os.makedirs(build_dir)
        cmd = [make, "-C", HERE, "-j8", "images",
               "BUILD_DIR=" + make_path(build_dir)] + switches
        label = "%-12s %s" % (name, " ".join(switches) or "(no switches)")
        r = subprocess.run(cmd, env=env, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True, errors="replace")
        images_ok = all(os.path.isfile(os.path.join(build_dir, "shell-%s.bin" % k))
                        for k in ("sram", "qspi"))
        if name in EXPECT_REFUSED:
            if (r.returncode != 0 and not images_ok
                    and EXPECT_REFUSED[name] in r.stdout):
                print("ok   %s -- refused as expected (%s)" % (label, EXPECT_REFUSED[name]))
            else:
                print("FAIL %s -- expected the build to be refused with %r (make exit %d, "
                      "images %s)" % (label, EXPECT_REFUSED[name], r.returncode,
                                      "built" if images_ok else "absent"))
                fails.append(name)
            continue
        mem = sram_exec(r.stdout)
        if r.returncode != 0 or not images_ok or mem is None:
            why = ("make exit %d" % r.returncode if r.returncode != 0 else
                   "no shell-sram.bin/shell-qspi.bin" if not images_ok else
                   "no SRAM_EXEC line in the link output")
            print("FAIL %s -- %s" % (label, why))
            tail = r.stdout.strip().splitlines()[-40:]
            print("\n".join("     | " + line for line in tail))
            fails.append(name)
            continue
        used, size = mem
        rules = SYMBOL_RULES.get(name)
        if rules:
            nm = subprocess.run([os.path.join(TOOLCHAIN_BIN, "arm-none-eabi-nm"), "-C",
                                 os.path.join(build_dir, "shell.elf")], env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, errors="replace")
            problems = (["nm exit %d" % nm.returncode] if nm.returncode != 0 else
                        symbol_problems(nm.stdout, rules[0], rules[1]))
            if problems:
                print("FAIL %s -- symbol rules: %s" % (label, "; ".join(problems)))
                fails.append(name)
                continue
        print("ok   %s  SRAM_EXEC %d of %d B (%.2f %%), %d B free%s"
              % (label, used, size, 100.0 * used / size, size - used,
                 ", save path %s" % ("linked" if rules[0] else "absent") if rules else ""))

    if fails:
        print("%d of %d images failed: %s" % (len(fails), len(IMAGES), ", ".join(fails)))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
