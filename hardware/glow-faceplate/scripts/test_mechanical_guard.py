#!/usr/bin/env python3
"""Mutation tests for the source-authoritative faceplate guard."""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
DXF = ROOT / ".reference/glow-touch2/Simple Touch Faceplate template.dxf"
VCV = ROOT / "host/vcv/res/touch2_geometry.py"
RULES = ROOT / "hardware/glow-faceplate/glow-faceplate.kicad_dru"
GENERATOR = HERE / "generate_master.py"
VERIFIER = HERE / "verify_mechanics.py"


def _curve_blocks(text: str) -> list[re.Match[str]]:
    return list(re.finditer(r"  \(gr_curve\n.*?\n  \)", text, re.DOTALL))


class MechanicalGuardTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        folder = Path(self.temporary.name)
        self.board = folder / "faceplate.kicad_pcb"
        self.svg = folder / "faceplate.svg"
        result = subprocess.run(
            [sys.executable, "-B", str(GENERATOR), "--dxf", str(DXF),
             "--board", str(self.board), "--svg", str(self.svg)],
            text=True, capture_output=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _verify(self, *, board: Path | None = None,
                vcv: Path | None = None,
                rules: Path | None = None) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, "-B", str(VERIFIER),
             "--board", str(board or self.board), "--svg", str(self.svg),
             "--dxf", str(DXF), "--vcv-geometry", str(vcv or VCV),
             "--rules", str(rules or RULES)],
            text=True, capture_output=True, check=False,
        )

    def _mutated_board(self, name: str, text: str) -> Path:
        path = Path(self.temporary.name) / f"{name}.kicad_pcb"
        path.write_text(text, encoding="utf-8", newline="\n")
        return path

    def test_dxf_analysis_finds_closed_source_unions(self) -> None:
        from dxf_apertures import analyze_dxf

        source = analyze_dxf(DXF)
        self.assertEqual(len(source.records), 27)
        self.assertEqual(len(source.apertures), 15)
        self.assertEqual(sum(item.kind == "aperture"
                             for item in source.apertures), 13)
        self.assertEqual(sum(item.kind == "slot"
                             for item in source.apertures), 2)
        self.assertEqual(source.open_reference_record_indices, (19, 20, 22))
        self.assertTrue(any(abs(item.width - item.height) > 0.000001
                            for item in source.apertures))

    def test_generated_master_is_green(self) -> None:
        result = self._verify()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_later_dxf_spline_segment_deletion_is_red(self) -> None:
        text = self.board.read_text(encoding="utf-8")
        references = [match for match in _curve_blocks(text)
                      if '(layer "Dwgs.User")' in match.group(0)]
        self.assertGreater(len(references), 114)
        victim = references[-1]
        mutated = text[:victim.start()] + text[victim.end():]
        result = self._verify(board=self._mutated_board("later-delete", mutated))
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("mechanical reference", result.stdout)

    def test_later_dxf_spline_unlock_is_red(self) -> None:
        text = self.board.read_text(encoding="utf-8")
        references = [match for match in _curve_blocks(text)
                      if '(layer "Dwgs.User")' in match.group(0)]
        victim = references[-1]
        changed = victim.group(0).replace("    (locked)\n", "", 1)
        self.assertNotEqual(changed, victim.group(0))
        mutated = text[:victim.start()] + changed + text[victim.end():]
        result = self._verify(board=self._mutated_board("later-unlock", mutated))
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("not locked", result.stdout)

    def test_mechanical_reference_group_omission_is_red(self) -> None:
        text = self.board.read_text(encoding="utf-8")
        match = re.search(r'  \(group "mechanical_reference"\n.*?\n  \)',
                          text, re.DOTALL)
        self.assertIsNotNone(match)
        block = match.group(0)
        members = re.search(r"    \(members (.*?)\)\n", block)
        self.assertIsNotNone(members)
        member_ids = re.findall(r'"[0-9a-f-]+"', members.group(1))
        self.assertGreater(len(member_ids), 114)
        changed = block.replace(" " + member_ids[-1], "", 1)
        mutated = text[:match.start()] + changed + text[match.end():]
        result = self._verify(board=self._mutated_board("group-omit", mutated))
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("mechanical_reference group", result.stdout)

    def test_source_aperture_extent_mutation_is_red(self) -> None:
        text = self.board.read_text(encoding="utf-8")
        match = re.search(r"\(drill oval ([0-9.]+) ([0-9.]+)\)", text)
        self.assertIsNotNone(match)
        changed = (f"(drill oval {float(match.group(1)) + 0.02:.6f} "
                   f"{match.group(2)})")
        mutated = text[:match.start()] + changed + text[match.end():]
        result = self._verify(board=self._mutated_board("aperture-extent", mutated))
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("source envelope", result.stdout)

    def test_nominalized_round_aperture_is_red(self) -> None:
        text = self.board.read_text(encoding="utf-8")
        match = re.search(r"\(drill oval ([89]\.[0-9]+) ([89]\.[0-9]+)\)", text)
        self.assertIsNotNone(match)
        mutated = text[:match.start()] + "(drill 8)" + text[match.end():]
        result = self._verify(board=self._mutated_board("nominalized-round", mutated))
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("source-envelope oval", result.stdout)

    def test_protected_silk_text_is_red(self) -> None:
        text = self.board.read_text(encoding="utf-8")
        mutated = text.replace('"FIREFLOW / GLOW"', '"SYNTHUX"', 1)
        self.assertNotEqual(mutated, text)
        result = self._verify(board=self._mutated_board("protected-silk", mutated))
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("forbidden", result.stdout)

    def test_provisional_copper_text_is_red(self) -> None:
        text = self.board.read_text(encoding="utf-8")
        addition = '''
  (gr_text "FADER"
    (at 40 30 0)
    (layer "F.Cu")
    (effects (font (size 1 1) (thickness 0.15)))
  )
'''
        mutated = text.rsplit("\n)", 1)[0] + addition + ")\n"
        result = self._verify(board=self._mutated_board("provisional-copper", mutated))
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("forbidden", result.stdout)

    def test_provisional_mask_text_is_red(self) -> None:
        text = self.board.read_text(encoding="utf-8")
        addition = '''
  (gr_text "SWITCH"
    (at 40 30 0)
    (layer "F.Mask")
    (effects (font (size 1 1) (thickness 0.15)))
  )
'''
        mutated = text.rsplit("\n)", 1)[0] + addition + ")\n"
        result = self._verify(board=self._mutated_board("provisional-mask", mutated))
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("forbidden", result.stdout)

    def test_mask_sliver_setting_mutation_is_red(self) -> None:
        text = self.board.read_text(encoding="utf-8")
        mutated = text.replace("(solder_mask_min_width 0.20)",
                               "(solder_mask_min_width 0.10)", 1)
        self.assertNotEqual(mutated, text)
        result = self._verify(board=self._mutated_board("mask-sliver", mutated))
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("mask sliver", result.stdout)

    def test_rule_source_omission_is_red(self) -> None:
        rules = RULES.read_text(encoding="utf-8")
        changed = rules.replace("edge_clearance", "edge_not_checked", 1)
        self.assertNotEqual(changed, rules)
        path = Path(self.temporary.name) / "incomplete.kicad_dru"
        path.write_text(changed, encoding="utf-8", newline="\n")
        result = self._verify(rules=path)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("rule source is incomplete", result.stdout)

    @unittest.skipUnless(os.environ.get("KICAD_CLI"),
                         "set KICAD_CLI for the KiCad custom-rule mutation")
    def test_kicad_rejects_thin_copper_graphic(self) -> None:
        text = self.board.read_text(encoding="utf-8")
        copper = next(match for match in _curve_blocks(text)
                      if '(layer "F.Cu")' in match.group(0))
        changed = copper.group(0).replace("(stroke (width 0.55)",
                                          "(stroke (width 0.10)", 1)
        self.assertNotEqual(changed, copper.group(0))
        self.board.write_text(text[:copper.start()] + changed + text[copper.end():],
                              encoding="utf-8", newline="\n")
        self.board.with_suffix(".kicad_dru").write_text(
            RULES.read_text(encoding="utf-8"), encoding="utf-8", newline="\n")
        report = self.board.with_suffix(".drc-report.txt")
        result = subprocess.run(
            [os.environ["KICAD_CLI"], "pcb", "drc", "--output", str(report),
             "--format", "report", "--units", "mm", "--severity-all",
             "--exit-code-violations", str(self.board)],
            text=True, capture_output=True, check=False,
        )
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        evidence = report.read_text(encoding="utf-8")
        self.assertIn("Conservative copper graphic width", evidence)

    def test_vcv_centres_are_read_from_host_source(self) -> None:
        geometry = VCV.read_text(encoding="utf-8")
        changed = geometry.replace('"out_l": (4.31, 15.15)',
                                   '"out_l": (14.31, 15.15)', 1)
        self.assertNotEqual(changed, geometry)
        vcv = Path(self.temporary.name) / "touch2_geometry.py"
        vcv.write_text(changed, encoding="utf-8", newline="\n")
        result = self._verify(vcv=vcv)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("VCV fiducial", result.stdout)


if __name__ == "__main__":
    unittest.main()
