# P2 step 3: GND zone on B.Cu + refill + DRC
import pcbnew, os

BOARD = r"C:\Users\ryuen\Documents\Projects\Devices\ESP_Leveller\pcb\ESPLeveller\ESPLeveller.kicad_pcb"
b = pcbnew.LoadBoard(BOARD)

# has a GND zone already?
has = [z for z in b.Zones() if z.GetNetname() == "GND"]
print("existing GND zones:", len(has))

if not has:
    net = b.FindNet("GND")
    z = pcbnew.ZONE(b)
    z.SetNet(net)
    z.SetLayer(pcbnew.B_Cu)
    z.SetLocalClearance(pcbnew.FromMM(0.3))
    z.SetMinThickness(pcbnew.FromMM(0.2))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)  # solid: pours are the ground
    pts = pcbnew.VECTOR_VECTOR2I()
    for x, y in [(21, 16), (124, 16), (124, 89), (21, 89)]:
        pts.append(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
    z.Outline().NewOutline()
    for p in pts:
        z.Outline().Append(p.x, p.y)
    b.Add(z)
    print("zone added")

ok = b.ZoneFill() if hasattr(b, "ZoneFill") else None
print("ZoneFill:", ok)
# fallback: fill each zone
for z in b.Zones():
    try:
        z.SetIsFilled(True)
    except Exception as e:
        print("fill note:", e)

b.Save(BOARD)
print("saved")
