# P2 step 1: populate ESPLeveller.kicad_pcb from netlist via pcbnew API
import pcbnew, re, json, os

KICAD_FPS = r"C:\Program Files\KiCad\10.0\share\kicad\footprints"
PROJ = r"C:\Users\ryuen\Documents\Projects\Devices\ESP_Leveller\pcb\ESPLeveller"
PROJ_FPS = os.path.join(PROJ, "footprints", "ESPLeveller.pretty")
BOARD_FILE = os.path.join(PROJ, "ESPLeveller.kicad_pcb")

board = pcbnew.LoadBoard(BOARD_FILE)

# --- clear any existing footprints (skeleton) ---
for fp in list(board.GetFootprints()):
    board.Remove(fp)

# --- nets ---
nets_json = json.load(open(os.path.join(PROJ, "p2_nets.json")))
netmap = {}
for name, nodes in nets_json:
    n = pcbnew.NETINFO_ITEM(board, name)
    board.Add(n)
    netmap[name] = n

# --- components: (ref, value, footprint id, x, y, rot) mm — signal-flow first pass ---
# Flow: USB/TP4056 (left) -> SW1 -> MT3608 -> SuperMini (right, antenna overhangs right edge)
# 18650 holders sit below (J1 header at pack), sensor pod top-right.
COMPS = [
    # ref, value, fpid, x, y, rot_deg
    ("U2", "TP4056_ChargeProt", "ESPLeveller:TP4056_Module_Pads", 30, 60, 0),
    ("SW1", "PWR", "Button_Switch_THT:SW_Tactile_SPST_Angled_PTS645Vx31-2LFS", 52, 60, 0),
    ("U3", "MT3608_5V", "ESPLeveller:MT3608_Module_Pads", 66, 60, 0),
    ("U1", "ESP32C3_SuperMini", "ESPLeveller:ESP32-C3-SuperMini_Socket", 100, 55, 90),
    ("J2", "Sensor_Pod", "Connector_PinSocket_2.54mm:PinSocket_1x08_P2.54mm_Vertical", 100, 25, 90),
    ("R1", "4.7k", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal", 85, 30, 0),
    ("R2", "4.7k", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal", 85, 40, 0),
    ("R3", "330R", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal", 85, 70, 0),
    ("D1", "STATUS_GREEN", "LED_THT:LED_D5.0mm", 95, 75, 90),
    ("J3", "OPT_BTN", "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical", 95, 82, 90),
    ("C1", "100uF", "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm", 66, 70, 0),
    ("J1", "Battery_18650x2", "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical", 35, 70, 0),
]

def load_fp(fpid):
    lib, name = fpid.split(":")
    d = PROJ_FPS if lib == "ESPLeveller" else os.path.join(KICAD_FPS, lib + ".pretty")
    io = pcbnew.GetPluginForPath(d)
    fp = io.FootprintLoad(d, name)
    if fp is None:
        raise RuntimeError(f"footprint not found: {fpid} in {d}")
    return fp

placed = {}
for ref, value, fpid, x, y, rot in COMPS:
    fp = load_fp(fpid)
    fp.SetReference(ref)
    fp.SetValue(value)
    fp.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
    if rot:
        fp.SetOrientationDegrees(rot)
    board.Add(fp)
    placed[ref] = fp

# --- assign pads to nets ---
assigned = 0
for name, nodes in nets_json:
    net = netmap[name]
    for ref, pin in nodes:
        fp = placed.get(ref)
        if fp is None:
            print("MISSING comp", ref); continue
        pad = fp.FindPadByNumber(pin)
        if pad is None:
            print("MISSING pad", ref, pin); continue
        pad.SetNet(net)
        assigned += 1

print(f"placed {len(placed)} footprints, {assigned} pads on nets, {board.GetNetCount()} nets")
board.Save(BOARD_FILE)
print("saved", BOARD_FILE)
