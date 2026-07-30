"""Compiler/link recipes for fail-closed benchmark optimization modes."""

from pathlib import Path
import subprocess
import unittest


HERE = Path(__file__).resolve().parent


def dry_run(mode):
    return subprocess.run(
        [
            "make",
            "-n",
            "-B",
            "BENCH_FAMILIES=system",
            "BENCH_ITCM_HOT=1",
            "BENCH_OPTIMIZATION=%s" % mode,
            "build/bench.elf",
        ],
        cwd=HERE,
        check=True,
        capture_output=True,
        text=True,
    ).stdout


class OptimizationMakeContract(unittest.TestCase):
    def test_o2_uses_only_the_current_optimization(self):
        recipe = dry_run("o2")
        self.assertIn(" -O2 ", recipe)
        self.assertNotIn(" -O3 ", recipe)
        self.assertNotIn("-flto", recipe)
        self.assertNotIn("-ffast-math", recipe)
        self.assertNotIn("-funroll-loops", recipe)

    def test_o3_uses_o3_without_lto_or_dormant_flags(self):
        recipe = dry_run("o3")
        self.assertIn(" -O3 ", recipe)
        self.assertNotIn(" -O2 ", recipe)
        self.assertNotIn("-flto", recipe)
        self.assertNotIn("-ffast-math", recipe)
        self.assertNotIn("-funroll-loops", recipe)

    def test_o3_lto_passes_lto_to_compile_and_link(self):
        recipe = dry_run("o3-lto")
        compile_lines = [
            line for line in recipe.splitlines()
            if " -c " in line and ("arm-none-eabi-gcc" in line
                                   or "arm-none-eabi-g++" in line)
        ]
        link_lines = [
            line for line in recipe.splitlines()
            if "arm-none-eabi-g++" in line and "build/bench.elf" in line
        ]
        self.assertTrue(compile_lines)
        self.assertTrue(link_lines)
        self.assertTrue(all(" -O3 " in line for line in compile_lines))
        self.assertTrue(all("-flto" in line for line in compile_lines))
        self.assertTrue(all("-flto" in line for line in link_lines))

    def test_every_object_depends_on_the_mode_header(self):
        makefile = (HERE / "Makefile").read_text(encoding="utf-8")
        self.assertIn(
            "$(OBJECTS): $(BUILD_DIR)/bench_optimization.h",
            makefile,
        )


if __name__ == "__main__":
    unittest.main()
