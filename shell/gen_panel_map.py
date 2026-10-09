#!/usr/bin/env python3
"""Generates shell/generated_panel_map.h: the Rev A panel as the firmware sees it.

Spec: docs/superpowers/specs/2026-10-02-rev-a-p6a-panel-scan-design.md,
sections 2 and 4; the classification is spec
docs/superpowers/specs/2026-10-09-rev-a-p6b1-shared-control-law-design.md,
section 4.2.

Inputs, all read, none written:
  hardware/reva/panel-map.json         P3's assignment (assign.py): pots, muxes,
                                       calibration and spare channels
  hardware/reva/blocks.py              P2's tables: SR_OUTPUTS, KEYS, MODULE_PINS
  control/params.hpp                   the shared control law's ParamId names
  host/vcv/src/generated_hw_panel.hpp  the FireflowHW controls

RESERVED below is the only hand-written part. Every other pot sends the
control-law parameter of its own name; a pot P3 adds or renames with no such
parameter stops the generator until it gets one or goes into RESERVED.

    python shell/gen_panel_map.py           write the header
    python shell/gen_panel_map.py --check   exit 1 if the committed header is stale
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
REVA = os.path.join(ROOT, "hardware", "reva")
PANEL_MAP = os.path.join(REVA, "panel-map.json")
PARAMS_HPP = os.path.join(ROOT, "control", "params.hpp")
HW_PANEL = os.path.join(ROOT, "host", "vcv", "src", "generated_hw_panel.hpp")
OUT = os.path.join(HERE, "generated_panel_map.h")

CHANNELS_PER_MUX = 8   # P2 section 3: ten 74HC4051
MAX_KEYS = 4           # shell/keys.h kMaxKeys
SENSE_ADC_BASE = 8     # shell/mux_plan.h kSenseAdcBase (ADC_9)
# libDaisy's patch_sm ADC index per module pin: the pin table in
# shell/main.cpp's adc_use_measured_sampling_time(), and P2 section 2.
ADC_OF_PIN = {"A2": 8, "A3": 9, "D9": 10, "D8": 11}
CAL_IDS = ("CAL_GND", "CAL_3V3")

# P6a's "safe" rule (SAFE/UNMAPPED, spec 2026-10-02 section 2.2) is retired:
# the firmware runs the same control law as VCV (spec 2026-10-09-rev-a-p6b1
# section 4.2), so every pot with a FireflowHW counterpart sends its own id.
#
# Spec 2026-10-07 section 4: pots on the board with no parameter yet. They are
# read like every pot and send nothing until the follow-up spec gives them one.
# One pot, two FireflowHW controls at its position: Fireflow.cpp's ctlVisible()
# shows STAGES ("BBD Bend") where the deck's ENGINE is the BBD and ATTACK on
# every other engine. That is the only such rule the firmware knows
# (shell::knob_target()), so a panel-map "ids" list may name a second id only
# from here, and only for its own deck.
ALT_ON_BBD = {"ATTACK_A": "STAGES_A", "ATTACK_B": "STAGES_B"}

RESERVED = {
    "ROOT_A": "per-deck scale root, follow-up to spec 2026-10-07 section 4",
    "ROOT_B": "per-deck scale root, follow-up to spec 2026-10-07 section 4",
    "REV_MOD": "reverb tail wobble (WOBL), follow-up to spec 2026-10-07 section 4",
}


class GenError(Exception):
    pass


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def read_committed():
    return read(OUT) if os.path.isfile(OUT) else ""


def param_names(text):
    """The ffctl::ParamId enumerators, NUM_PARAMS excluded."""
    start = text.index("enum ParamId {")
    end = text.index("NUM_PARAMS", start)
    return set(re.findall(r"^\s+(\w+),$", text[start:end], re.M))


def hw_panel_ids(text):
    start = text.index("kParamCtls[]")
    return set(re.findall(r"^\s*\{(\w+), WK_", text[start:text.index("};", start)], re.M))


def load_inputs():
    if REVA not in sys.path:
        sys.path.insert(0, REVA)
    import blocks
    panel_map = json.loads(read(PANEL_MAP))
    tables = {
        "sr_outputs": [list(chip) for chip in blocks.SR_OUTPUTS],
        "keys": list(blocks.KEYS),
        "module_pins": dict(blocks.MODULE_PINS),
        "mux_s": [blocks.sr(n) for n in blocks.MUX_S],
        "mux_en": [blocks.sr(n) for n in blocks.MUX_EN],
        "leds": [blocks.led_net(i) for i in range(len(panel_map["leds"]))],
    }
    return {"panel_map": panel_map, "tables": tables,
            "params": param_names(read(PARAMS_HPP)),
            "hw_ids": hw_panel_ids(read(HW_PANEL)),
            "reserved": dict(RESERVED)}


def _run(flat, nets):
    """The bit the first of `nets` sits on; they must follow one another."""
    missing = [n for n in nets if n not in flat]
    if missing:
        raise GenError("chain: %s not in SR_OUTPUTS" % ", ".join(missing))
    idx = [flat.index(n) for n in nets]
    if idx != list(range(idx[0], idx[0] + len(idx))):
        raise GenError("chain: %s..%s not contiguous: %s" % (nets[0], nets[-1], idx))
    return idx[0]


def build(panel_map, tables, params, hw_ids, reserved):
    pots, cal = panel_map["pots"], panel_map["calibration"]
    spare, muxes = panel_map.get("spare", []), panel_map["muxes"]

    # muxes and sense pins
    senses = ["SENSE_%d" % i for i in range(len(muxes))]
    if sorted(muxes) != senses:
        raise GenError("sense pins: %s" % sorted(muxes))
    sense_of_mux = {}
    for i, s in enumerate(senses):
        if muxes[s] != sorted(muxes[s]):
            raise GenError("%s: mux list not ascending: %s" % (s, muxes[s]))
        for m in muxes[s]:
            if m in sense_of_mux:
                raise GenError("mux %d on two sense pins" % m)
            sense_of_mux[m] = i
    n_mux = len(sense_of_mux)
    if sorted(sense_of_mux) != list(range(n_mux)):
        raise GenError("muxes are not 0..%d: %s" % (n_mux - 1, sorted(sense_of_mux)))

    # every mux input exactly once: pots, calibration, spare (P2 section 3)
    seen = {}
    for rows in (pots, cal, spare):
        for r in rows:
            m, ch, name = r["mux"], r["channel"], r.get("id", "spare")
            if m not in sense_of_mux or not 0 <= ch < CHANNELS_PER_MUX:
                raise GenError("%s: no such mux input: mux %s ch %s" % (name, m, ch))
            if (m, ch) in seen:
                raise GenError("twice: mux %d ch %d (%s, %s)" % (m, ch, seen[(m, ch)], name))
            seen[(m, ch)] = name
            if r["sense"] != senses[sense_of_mux[m]]:
                raise GenError("%s: sense %s, but mux %d is on %s"
                               % (name, r["sense"], m, senses[sense_of_mux[m]]))
    if len(seen) != n_mux * CHANNELS_PER_MUX:
        raise GenError("%d of %d mux inputs accounted for"
                       % (len(seen), n_mux * CHANNELS_PER_MUX))

    # sense pin -> ADC channel: the profile assumes one contiguous run
    pin_of = {net: pin for pin, net in tables["module_pins"].items()}
    for i, s in enumerate(senses):
        adc = ADC_OF_PIN.get(pin_of.get(s))
        if adc != SENSE_ADC_BASE + i:
            raise GenError("%s on %s is ADC index %s, expected %d"
                           % (s, pin_of.get(s), adc, SENSE_ADC_BASE + i))

    # the 595 chain: bit k of the word lands on output k of SR_OUTPUTS
    flat = [n for chip in tables["sr_outputs"] for n in chip]
    if len(flat) > 64:
        raise GenError("chain: %d bits do not fit a 64-bit word" % len(flat))
    addr_shift = _run(flat, tables["mux_s"])
    enable_shift = _run(flat, tables["mux_en"][:n_mux])
    leds = tables["leds"]
    led_shift = _run(flat, leds)

    # the 165: key i on D i, and the first bit shifted out is D7
    keys = tables["keys"]
    if not 0 < len(keys) <= MAX_KEYS:
        raise GenError("keys: %d, the firmware holds 1..%d" % (len(keys), MAX_KEYS))
    key_bits = [7 - i for i in range(len(keys))]

    cal_by_id = {c["id"]: c for c in cal}
    if sorted(cal_by_id) != sorted(CAL_IDS):
        raise GenError("calibration: %s, expected %s" % (sorted(cal_by_id), list(CAL_IDS)))

    # classification (spec 2026-10-09-rev-a-p6b1 section 4.2): a reserved pot
    # sends nothing, every other pot the control-law parameter of its name
    ids = [p["id"] for p in pots]
    if len(set(ids)) != len(ids):
        raise GenError("pot ids not unique")
    for name in sorted(reserved):
        if name not in ids:
            raise GenError("not a pot: %s" % name)
    for p in pots:
        if p["id"] in reserved:   # HwOnly on the VCV panel, not in kParamCtls
            continue
        # First, so a reserved pot taken off the list names its own defect
        # rather than the VCV panel's missing control.
        if p["id"] not in params:
            raise GenError("%s: no ffctl id in control/params.hpp" % p["id"])
        for i in p.get("ids", [p["id"]]):
            if i not in hw_ids:
                raise GenError("not in generated_hw_panel.hpp: %s" % i)

    rows = []
    for p in sorted(pots, key=lambda r: (r["mux"], r["channel"])):
        if p["id"] in reserved:
            rows.append((p, "-1", None, "reserved: " + reserved[p["id"]]))
            continue
        ids = p.get("ids", [p["id"]])
        if ids[0] != p["id"]:
            raise GenError("%s: ids list starts with %s" % (p["id"], ids[0]))
        alt = None
        if len(ids) > 1:
            if len(ids) > 2 or ALT_ON_BBD.get(p["id"]) != ids[1]:
                raise GenError("%s: shares its position with %s; the firmware knows "
                               "only %s" % (p["id"], ", ".join(ids[1:]), ALT_ON_BBD))
            if ids[1] not in params:
                raise GenError("%s: alternate %s has no ffctl id" % (p["id"], ids[1]))
            alt = ids[1]
        rows.append((p, "ffctl::" + p["id"], alt,
                     p["id"] + (" / %s on the BBD" % alt if alt else "")))

    return _render(rows, len(senses), n_mux, sense_of_mux, len(flat), addr_shift,
                   len(tables["mux_s"]), enable_shift, led_shift, len(leds),
                   cal_by_id, keys, key_bits)


def _render(rows, n_sense, n_mux, sense_of_mux, chain_bits, addr_shift, addr_bits,
            enable_shift, led_shift, led_bits, cal_by_id, keys, key_bits):
    out = []
    w = out.append
    w("// GENERATED by shell/gen_panel_map.py -- do not edit by hand.")
    w("// Sources: hardware/reva/panel-map.json, hardware/reva/blocks.py,")
    w("// control/params.hpp; the RESERVED list lives in the generator.")
    w("// Spec: docs/superpowers/specs/2026-10-02-rev-a-p6a-panel-scan-design.md,")
    w("// classification: 2026-10-09-rev-a-p6b1-shared-control-law-design.md 4.2")
    w("#pragma once")
    w('#include "controls.h"')
    w('#include "keys.h"')
    w('#include "mux_plan.h"')
    w("")
    w("namespace shell {")
    w("")
    w("// P2 sections 3 and 4: %d 74HC4051 on %d sense pins, one mux per sense pin"
      % (n_mux, n_sense))
    w("// per step; a %d-bit 595 chain -- address %d-%d, enables %d-%d, LEDs %d-%d."
      % (chain_bits, addr_shift, addr_shift + addr_bits - 1, enable_shift,
         enable_shift + n_mux - 1, led_shift, led_shift + led_bits - 1))
    w("inline constexpr ChainProfile kRevaChain{")
    w("    %d, kSenseAdcBase, %d," % (n_sense, n_mux))
    w("    {%s}," % ", ".join([str(CHANNELS_PER_MUX)] * n_mux))
    w("    {%s}," % ", ".join(str(sense_of_mux[m]) for m in range(n_mux)))
    w("    %d, %d, %d, %d, %d, -1, %d, true};"
      % (chain_bits, addr_shift, enable_shift, led_shift, led_bits, addr_bits))
    w("")
    w("// One row per pot in (mux, channel) order; SHELL_PLAY_V prints the values")
    w("// in this order. param -1: scanned and reported, sent nowhere (spec 2.3);")
    w("// every other row writes its control-law knob (P6b-1 spec 4.2), the ATTACK")
    w("// rows STAGES instead while their deck is on the BBD (knob_target()).")
    w("inline constexpr ControlEntry kRevaControls[] = {")
    for i, (p, target, alt, note) in enumerate(rows):
        w("    {%d, %d, %s, %d%s},  // row %d %s -- %s"
          % (p["mux"], p["channel"], target, sense_of_mux[p["mux"]],
             ", ffctl::" + alt if alt else "", i, p["id"], note))
    w("};")
    w("static_assert(entries_valid(kRevaControls,")
    w("                            sizeof(kRevaControls) / sizeof(kRevaControls[0])),")
    w('              "kRevaControls: an id outside the knob vector");')
    w("inline constexpr ControlTable kRevaTable{")
    w("    kRevaControls,")
    w("    static_cast<int>(sizeof(kRevaControls) / sizeof(kRevaControls[0]))};")
    w("")
    w("// The calibration channels (P2 section 3): the panel reads its own span.")
    z, r = cal_by_id["CAL_GND"], cal_by_id["CAL_3V3"]
    w("inline constexpr MuxChannel kRevaCalZero{%d, %d};  // CAL_GND" % (z["mux"], z["channel"]))
    w("inline constexpr MuxChannel kRevaCalRail{%d, %d};  // CAL_3V3" % (r["mux"], r["channel"]))
    w("")
    w("// %s on the 165's D0..D%d; the first bit shifted out is D7."
      % (", ".join(keys), len(keys) - 1))
    w("inline constexpr KeyPad kRevaKeys{%d, {%s}};"
      % (len(keys), ", ".join(str(b) for b in key_bits)))
    w("")
    w("} // namespace shell")
    return "\n".join(out) + "\n"


def check_text(committed, generated):
    return committed.replace("\r\n", "\n") == generated


def main(argv):
    try:
        text = build(**load_inputs())
    except GenError as e:
        print("gen_panel_map: %s" % e, file=sys.stderr)
        return 2
    if "--check" in argv:
        if check_text(read_committed(), text):
            print("shell/generated_panel_map.h is current")
            return 0
        print("shell/generated_panel_map.h is STALE: run python shell/gen_panel_map.py",
              file=sys.stderr)
        return 1
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote shell/generated_panel_map.h")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
