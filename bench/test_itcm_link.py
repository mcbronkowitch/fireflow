"""Real-link contract for the optional ITCM audio hotset."""

import os
from pathlib import Path
import subprocess
import unittest

from itcm_placement import validate_itcm_placement


HERE = Path(__file__).resolve().parent
OPTIMIZATION = os.environ.get("BENCH_TEST_OPTIMIZATION", "o2")
if OPTIMIZATION not in {"o2", "o3", "o3-lto"}:
    raise RuntimeError(
        "BENCH_TEST_OPTIMIZATION must be o2, o3, or o3-lto"
    )

NM = Path(r"C:\Program Files\DaisyToolchain\bin\arm-none-eabi-nm.exe")
OBJDUMP = Path(
    r"C:\Program Files\DaisyToolchain\bin\arm-none-eabi-objdump.exe"
)
READELF = Path(
    r"C:\Program Files\DaisyToolchain\bin\arm-none-eabi-readelf.exe"
)
ELF = HERE / "build" / "bench.elf"
@unittest.skipUnless(
    all(tool.is_file() for tool in (NM, OBJDUMP, READELF)),
    "Daisy Arm inspection tools are not installed",
)
class ItcmLinkContract(unittest.TestCase):
    def test_audio_hotset_links_into_itcm_and_data_stays_in_dtcm(self):
        subprocess.run(
            [
                "make",
                "-j8",
                "BENCH_FAMILIES=system",
                "BENCH_ITCM_HOT=1",
                "BENCH_OPTIMIZATION=%s" % OPTIMIZATION,
                "build/bench.elf",
            ],
            cwd=HERE,
            check=True,
            capture_output=True,
            text=True,
        )
        placement = validate_itcm_placement(
            ELF,
            nm=NM,
            objdump=OBJDUMP,
            readelf=READELF,
        )
        self.assertGreater(placement["hot_size"], 0)
