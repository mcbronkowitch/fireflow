#!/usr/bin/env python3
"""Generates shell/generated_workloads.h: the remote CPU hunt's board
workloads and gestures (spec 2026-10-29-cpu-hunt-remote-design.md section
5.2, plan Task 5), from shell/workloads.json (Task 4).

Inputs, read, never written:
  shell/workloads.json         the workloads (knob vectors plus their load
                               material) and the gestures
  shell/generated_panel_map.h  kRevaControls: the row count and the row names
                               every vector is checked against

What the header holds (namespace shell):
  kWorkloads[]     one WorkloadDef per workload, id = index in the JSON. The
                   name inline (no pointer: the table lives in QSPI and a
                   stored char* is one more word the placement guard has to
                   reason about), the rows as int16_t x1000 of the normalised
                   pot value, the material (excite, rec, settle_blocks,
                   input).
  kGestureSteps[]  every gesture's steps, gesture after gesture; a step sets
                   `row` to `value_x1000` at `block` (from the gesture's
                   first counted block); row -1 = a MOD press, -2 = SHIFT
                   held + MOD press (kGestureRowMod / kGestureRowShiftMod).
  kGestures[]      per gesture: name, base workload id, its slice of
                   kGestureSteps, its length in blocks.

On the board (__arm__) the three tables go to the section .qspi_rodata, which
shell/qspi_cold.ld places in QSPI behind .qspi_cold: about 40 KB that
SRAM_EXEC (~45 KB free) cannot spare. They are read only at a LOAD / GEST
(the gesture's steps are copied to SRAM then). The desktop build has no
section attribute. The definitions are compiled only where
SHELL_WORKLOADS_DEFINE is defined (workload_run.cpp); everyone else sees the
declarations and the inline lookups.

    python shell/gen_workloads.py           write the header
    python shell/gen_workloads.py --check   exit 1 if the committed header is stale
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
JSON_PATH = os.path.join(HERE, "workloads.json")
PANEL_MAP = os.path.join(HERE, "generated_panel_map.h")
OUT = os.path.join(HERE, "generated_workloads.h")

NAME_MAX = 31                 # WorkloadDef / GestureDef name[NAME_MAX + 1]
INPUTS = ("silence", "noise")  # WorkloadInput order
ROW_MOD, ROW_SHIFT_MOD = -1, -2
SECTION = ".qspi_rodata"

_ROW_COMMENT = re.compile(r"//\s*row (\d+) (\w+)\b")


class GenError(Exception):
    pass


def table_rows_from(text):
    """The kRevaControls row names in row order, from the `// row N NAME`
    comments of generated_panel_map.h."""
    body = text.split("kRevaControls[] = {", 1)
    if len(body) != 2:
        raise GenError("generated_panel_map.h: no kRevaControls table")
    body = body[1].split("};", 1)[0]
    rows = []
    for m in _ROW_COMMENT.finditer(body):
        if int(m.group(1)) != len(rows):
            raise GenError("generated_panel_map.h: row comments out of order at row %s" % m.group(1))
        rows.append(m.group(2))
    if not rows:
        raise GenError("generated_panel_map.h: no row comments in kRevaControls")
    return rows


def load_inputs():
    with open(JSON_PATH, encoding="utf-8") as f:
        doc = json.load(f)
    with open(PANEL_MAP, encoding="utf-8") as f:
        rows = table_rows_from(f.read())
    return {"doc": doc, "table_rows": rows}


def _name(kind, i, name, seen):
    if not isinstance(name, str) or not name or not re.match(r"^[A-Za-z0-9_]+$", name):
        raise GenError("%s %d: name %r is not [A-Za-z0-9_]+" % (kind, i, name))
    if len(name) > NAME_MAX:
        raise GenError("%s %d: name %r is longer than %d characters" % (kind, i, name, NAME_MAX))
    if name in seen:
        raise GenError("%s %d: duplicate name %r" % (kind, i, name))
    seen.add(name)


def _bool_pair(kind, i, key, v):
    if not (isinstance(v, list) and len(v) == 2 and all(isinstance(x, bool) for x in v)):
        raise GenError("%s %d: %s must be a pair of booleans, got %r" % (kind, i, key, v))


def _value(where, v):
    if not isinstance(v, int) or isinstance(v, bool) or not 0 <= v <= 1000:
        raise GenError("%s: value %r outside 0..1000" % (where, v))


def validate(doc, table_rows):
    n_rows = len(table_rows)
    if doc.get("row_count") != n_rows:
        raise GenError("workloads.json row_count %r, the table has %d rows"
                       % (doc.get("row_count"), n_rows))
    names = doc.get("rows")
    if not isinstance(names, list) or len(names) != n_rows:
        raise GenError("workloads.json lists %s row names, the table has %d rows"
                       % (len(names) if isinstance(names, list) else "no", n_rows))
    for r, (a, b) in enumerate(zip(names, table_rows)):
        if a != b:
            raise GenError("row %d is %s in workloads.json, %s in the table" % (r, a, b))

    seen = set()
    wls = doc.get("workloads") or []
    if not wls:
        raise GenError("no workloads")
    for i, w in enumerate(wls):
        _name("workload", i, w.get("name"), seen)
        rows = w.get("rows_x1000")
        if not isinstance(rows, list) or len(rows) != n_rows:
            raise GenError("workload %d (%s) has %s rows, the table has %d rows"
                           % (i, w.get("name"), len(rows) if isinstance(rows, list) else "no", n_rows))
        for r, v in enumerate(rows):
            _value("workload %d (%s) row %d" % (i, w["name"], r), v)
        _bool_pair("workload", i, "excite", w.get("excite"))
        _bool_pair("workload", i, "rec", w.get("rec"))
        s = w.get("settle_blocks")
        if not isinstance(s, int) or isinstance(s, bool) or not 0 < s <= 0xFFFF:
            raise GenError("workload %d: settle_blocks %r not in 1..65535" % (i, s))
        if w.get("input") not in INPUTS:
            raise GenError("workload %d: input %r not one of %s" % (i, w.get("input"), INPUTS))

    by_name = {w["name"]: i for i, w in enumerate(wls)}
    gseen = set()
    for gi, gj in enumerate(doc.get("gestures") or []):
        _name("gesture", gi, gj.get("name"), gseen)
        if gj.get("base") not in by_name:
            raise GenError("gesture %d (%s): base %r is not a workload" % (gi, gj["name"], gj.get("base")))
        steps = gj.get("steps") or []
        if not steps:
            raise GenError("gesture %d (%s): no steps" % (gi, gj["name"]))
        blocks = gj.get("blocks")
        if not isinstance(blocks, int) or isinstance(blocks, bool) or blocks <= 0:
            raise GenError("gesture %d (%s): blocks %r" % (gi, gj["name"], blocks))
        prev = -1
        for si, st in enumerate(steps):
            where = "gesture %d (%s) step %d" % (gi, gj["name"], si)
            b, r, v = st.get("block"), st.get("row"), st.get("value_x1000")
            if not isinstance(b, int) or b < 0 or b >= 2 ** 32:
                raise GenError("%s: block %r" % (where, b))
            if b < prev:
                raise GenError("%s: block %d before the previous step's %d (steps must be in order)"
                               % (where, b, prev))
            prev = b
            if b >= blocks:
                raise GenError("%s: block %d not inside the gesture's %d blocks" % (where, b, blocks))
            if not isinstance(r, int) or not ROW_SHIFT_MOD <= r < n_rows:
                raise GenError("%s: row %r outside %d..%d" % (where, r, ROW_SHIFT_MOD, n_rows - 1))
            if r < 0:
                if v != 0:
                    raise GenError("%s: a key step carries value %r, not the dummy 0" % (where, v))
            else:
                _value(where, v)
    return by_name


def build(doc, table_rows):
    by_name = validate(doc, table_rows)
    wls = doc["workloads"]
    gests = doc.get("gestures") or []
    n_rows = len(table_rows)
    total = sum(len(g["steps"]) for g in gests)
    longest = max([len(g["steps"]) for g in gests] or [0])

    o = []
    o.append("// GENERATED by shell/gen_workloads.py -- do not edit by hand.")
    o.append("// Source: shell/workloads.json (tools/pick_workloads.py, plan 2026-10-29-cpu-hunt-")
    o.append("// remote Task 4), rows checked against kRevaControls (generated_panel_map.h).")
    o.append("// Spec: docs/superpowers/specs/2026-10-29-cpu-hunt-remote-design.md section 5.2.")
    o.append("//")
    o.append("// A workload: one value per kRevaControls row, x1000 of the normalised pot")
    o.append("// value, loaded through apply_boot_patch_rows() over the init patch; its")
    o.append("// material as the desktop sweep applied it (host/sweep/main.cpp")
    o.append("// material_for()). A gesture: steps at block offsets from its first counted")
    o.append("// block, on its base workload; row %d = MOD press, %d = SHIFT + MOD press."
             % (ROW_MOD, ROW_SHIFT_MOD))
    o.append("// On the board the tables live in %s (QSPI, shell/qspi_cold.ld)." % SECTION)
    o.append("#pragma once")
    o.append("#include <cstdint>")
    o.append("")
    o.append("#if defined(__arm__)")
    o.append('#define SHELL_WORKLOADS_SECTION __attribute__((section("%s")))' % SECTION)
    o.append("#else")
    o.append("#define SHELL_WORKLOADS_SECTION")
    o.append("#endif")
    o.append("")
    o.append("namespace shell {")
    o.append("")
    o.append("inline constexpr int kWorkloadCount     = %d;" % len(wls))
    o.append("inline constexpr int kWorkloadRowCount  = %d;" % n_rows)
    o.append("inline constexpr int kWorkloadNameMax   = %d;" % NAME_MAX)
    o.append("inline constexpr int kGestureCount      = %d;" % len(gests))
    o.append("inline constexpr int kGestureStepTotal  = %d;" % total)
    o.append("inline constexpr int kGestureMaxSteps   = %d;" % longest)
    o.append("inline constexpr int kGestureRowMod      = %d;   // a MOD press" % ROW_MOD)
    o.append("inline constexpr int kGestureRowShiftMod = %d;   // SHIFT held + MOD press" % ROW_SHIFT_MOD)
    o.append("")
    o.append("// What the sweep fed the audio input. The board does not synthesise it: its")
    o.append("// input carries whatever is patched, and the load line names this value.")
    o.append("enum class WorkloadInput : uint8_t { Silence = 0, Noise = 1 };")
    o.append("")
    o.append("struct WorkloadDef")
    o.append("{")
    o.append("    char          name[kWorkloadNameMax + 1];")
    o.append("    int16_t rows_x1000[kWorkloadRowCount];")
    o.append("    uint8_t       excite[2];       // excite_other_deck at load, held across the switch")
    o.append("    uint8_t       rec[2];          // REC latched through the settle, released at its end")
    o.append("    uint16_t      settle_blocks;   // untimed blocks after the load")
    o.append("    WorkloadInput input;")
    o.append("};")
    o.append("")
    o.append("struct GestureStep")
    o.append("{")
    o.append("    uint32_t block;         // from the gesture's first counted block")
    o.append("    int16_t  row;           // kRevaControls row, or kGestureRowMod / kGestureRowShiftMod")
    o.append("    int16_t  value_x1000;   // 0 on a key step")
    o.append("};")
    o.append("")
    o.append("struct GestureDef")
    o.append("{")
    o.append("    char     name[kWorkloadNameMax + 1];")
    o.append("    uint16_t base;     // workload id the gesture plays on")
    o.append("    uint16_t first;    // index into kGestureSteps")
    o.append("    uint16_t n;        // steps")
    o.append("    uint32_t blocks;   // the gesture's length")
    o.append("};")
    o.append("")
    o.append("extern const WorkloadDef kWorkloads[kWorkloadCount];")
    o.append("extern const GestureStep kGestureSteps[kGestureStepTotal];")
    o.append("extern const GestureDef  kGestures[kGestureCount];")
    o.append("")
    o.append("// nullptr for an id outside the table.")
    o.append("inline const WorkloadDef* workload_by_id(int id)")
    o.append("{")
    o.append("    return id >= 0 && id < kWorkloadCount ? &kWorkloads[id] : nullptr;")
    o.append("}")
    o.append("inline const GestureDef* gesture_def(int id)")
    o.append("{")
    o.append("    return id >= 0 && id < kGestureCount ? &kGestures[id] : nullptr;")
    o.append("}")
    o.append("// The gesture's steps and *n their count; nullptr (and *n = 0) outside the table.")
    o.append("inline const GestureStep* gesture_by_id(int id, int* n)")
    o.append("{")
    o.append("    const GestureDef* d = gesture_def(id);")
    o.append("    *n = d ? d->n : 0;")
    o.append("    return d ? &kGestureSteps[d->first] : nullptr;")
    o.append("}")
    o.append("")
    o.append("#if defined(SHELL_WORKLOADS_DEFINE)")
    o.append("SHELL_WORKLOADS_SECTION const WorkloadDef kWorkloads[kWorkloadCount] = {")
    for i, w in enumerate(wls):
        rows = w["rows_x1000"]
        chunks = [", ".join("%d" % v for v in rows[k:k + 16]) for k in range(0, len(rows), 16)]
        o.append('    {"%s", {' % w["name"])
        for k, c in enumerate(chunks):
            o.append("        %s%s" % (c, "," if k + 1 < len(chunks) else ""))
        o.append("     }, {%d, %d}, {%d, %d}, %d, WorkloadInput::%s},  // id %d"
                 % (int(w["excite"][0]), int(w["excite"][1]), int(w["rec"][0]), int(w["rec"][1]),
                    w["settle_blocks"], w["input"].capitalize(), i))
    o.append("};")
    o.append("")
    o.append("SHELL_WORKLOADS_SECTION const GestureStep kGestureSteps[kGestureStepTotal] = {")
    for gi, g in enumerate(gests):
        o.append("    // gesture %d %s" % (gi, g["name"]))
        for st in g["steps"]:
            o.append("    {%du, %d, %d}," % (st["block"], st["row"], st["value_x1000"]))
    o.append("};")
    o.append("")
    o.append("SHELL_WORKLOADS_SECTION const GestureDef kGestures[kGestureCount] = {")
    first = 0
    for gi, g in enumerate(gests):
        o.append('    {"%s", %d, %d, %d, %du},  // gesture %d'
                 % (g["name"], by_name[g["base"]], first, len(g["steps"]), g["blocks"], gi))
        first += len(g["steps"])
    o.append("};")
    o.append("#endif // SHELL_WORKLOADS_DEFINE")
    o.append("")
    o.append("} // namespace shell")
    return "\n".join(o) + "\n"


def read_committed():
    with open(OUT, encoding="utf-8") as f:
        return f.read()


def check_text(committed, generated):
    return committed.replace("\r\n", "\n") == generated


def main(argv):
    try:
        text = build(**load_inputs())
    except GenError as e:
        print("gen_workloads: %s" % e, file=sys.stderr)
        return 2
    if "--check" in argv:
        if os.path.exists(OUT) and check_text(read_committed(), text):
            print("shell/generated_workloads.h is current")
            return 0
        print("shell/generated_workloads.h is STALE: run python shell/gen_workloads.py",
              file=sys.stderr)
        return 1
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote shell/generated_workloads.h")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
