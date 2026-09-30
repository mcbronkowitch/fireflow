"""Rev A board rules, one source (P4-2 spec §4.3). place.py, route.py and
build.py read these values. The checks (place_check.py, route_check.py) keep
their own copies on purpose: a threshold is not re-derived from the thing it
guards."""

SIGNAL_W = 0.25
SUPPLY_W = 0.5
SUPPLY_TRACK_NETS = ("+12V", "-12V", "+12V_IN", "-12V_IN", "3V3D")
PLANE_NETS = ("GND", "SM_3V3")          # In1.Cu, In2.Cu
VIA_D, VIA_DRILL = 0.6, 0.3
CLEARANCE = 0.2
EDGE_CLEAR = 0.5
PITCH = 0.2
VIA_COST = 8.0
MAX_ITERS = 30

VICTIMS = ("OUT_L", "OUT_R", "IN_L", "IN_R")
AGGRESSORS = tuple(["LED%d" % n for n in range(19)] + ["LED%d_A" % n for n in range(19)]
                   + ["SR_CLK", "SR_DATA", "SR_LATCH", "SR_DIN",
                      "SD_CK", "SD_CMD", "SD_D0", "SD_D1", "SD_D2", "SD_D3"])
AUDIO_MM = 10.0
LR_PAIRS = (("OUT_L", "OUT_R"), ("IN_L", "IN_R"))
LR_MM = 2.0
SENSE = ("SENSE_0", "SENSE_1", "SENSE_2", "SENSE_3")
SENSE_FACTOR = 1.3
EXEMPT_MARGIN_MM = 2.54
TIERS = {"SENSE": 0, "AUDIO": 1, "REST": 2}


def tier_of(net):
    if net in SENSE:
        return TIERS["SENSE"]
    if net in VICTIMS:
        return TIERS["AUDIO"]
    return TIERS["REST"]


def project_rules():
    """The .kicad_pro section kicad-cli reads the board rules from (probed,
    P4-2 Task 1: board.design_settings.rules is the whole minimal set;
    net_settings adds nothing): merged into the project file build.py writes."""
    return {"board": {"design_settings": {"rules": {
        "min_track_width": SIGNAL_W, "min_via_diameter": VIA_D,
        "min_through_hole_diameter": VIA_DRILL, "min_clearance": CLEARANCE,
        "min_copper_edge_clearance": EDGE_CLEAR}}}}
