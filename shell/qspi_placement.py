"""Refuses an image whose QSPI-resident code includes anything on the audio
path, the engine-switch path or the control tick (spec 2026-10-09-rev-a-p6b1
section 5). Input: `arm-none-eabi-nm -C -S` output. Code in QSPI runs
execute-in-place through the QSPI bus: fine at boot, not inside a 2 ms block."""
import fnmatch, sys

QSPI_LO, QSPI_HI = 0x90100000, 0x90800000
HOT_PATTERNS = [
    "spky::*::process*", "spky::*::tick*", "spky::*::follow*", "spky::*::_control_tick*",
    "spky::*Engine::*",          # every part engine: switched at runtime
    "spky::Part::*", "spky::Center::*", "spky::ModLane::*", "spky::SuperModulator::*",
    "spky::Instrument::set_*", "spky::Instrument::process*",
    "control::*", "spkymod::*", "spkyvcv::*",
    "shell::panel_scan_tick*", "shell::MuxScan::*", "shell::key_update*", "shell::pot_filter*",
    "*IRQHandler*", "*Callback*", "daisy::AudioHandle::*", "daisy::SaiHandle::*",
    "daisy::DmaHandle*", "HAL_DMA_*", "HAL_SAI_*",
]

# Boot code that is not cold either: it runs before the board's own QSPI init
# has returned. DaisyPatchSM::Init() calls QSPIHandle::Init(), which takes the
# QSPI out of memory-mapped mode and puts it back; startup, the static
# constructors, the clock/MPU/cache setup in System::Init and the SDRAM init
# run before that, and DaisyPatchSM::Init and main are on the stack across it.
# Names only, as a backstop: what these call is checked on the call graph when
# the list in qspi_cold.ld grows (see its header).
WINDOW_PATTERNS = [
    "Reset_Handler", "SystemInit", "__libc_init_array", "main", "_GLOBAL__sub_I_*",
    "daisy::System::*", "daisy::QSPIHandle::*", "HAL_QSPI_*", "QSPI_*",
    "SdramHandle::*", "HAL_SDRAM_*", "FMC_*",
    "HAL_Init", "HAL_MspInit", "HAL_InitTick", "HAL_RCC*", "HAL_PWR*", "HAL_MPU_*",
    "HAL_GPIO_Init",
    "daisy::patch_sm::DaisyPatchSM::Init*", "daisy::DaisySeed::Init*",
]

def _reason(name):
    if any(fnmatch.fnmatchcase(name, p) for p in HOT_PATTERNS):
        return "hot code in QSPI"
    if any(fnmatch.fnmatchcase(name, p) for p in WINDOW_PATTERNS):
        return "runs before QSPI is memory-mapped"
    return None

def violations(nm_text):
    bad = []
    for line in nm_text.splitlines():
        parts = line.split(None, 3)
        if len(parts) < 4 or parts[2] not in ("T", "t", "W", "w"):
            continue
        addr = int(parts[0], 16)
        name = parts[3]
        if QSPI_LO <= addr < QSPI_HI and _reason(name):
            bad.append(name)
    return bad

if __name__ == "__main__":
    bad = violations(open(sys.argv[1], encoding="utf-8").read())
    for b in bad:
        print("QSPI placement: " + _reason(b) + ": " + b)
    sys.exit(1 if bad else 0)
