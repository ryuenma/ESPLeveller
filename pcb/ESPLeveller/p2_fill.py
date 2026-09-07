# P2 step 3b: fill zones with ZONE_FILLER
import pcbnew
BOARD = r"C:\Users\ryuen\Documents\Projects\Devices\ESP_Leveller\pcb\ESPLeveller\ESPLeveller.kicad_pcb"
b = pcbnew.LoadBoard(BOARD)
filler = pcbnew.ZONE_FILLER(b)
zones = list(b.Zones())
print("zones:", len(zones))
ok = filler.Fill(zones)
print("fill:", ok)
b.Save(BOARD)
print("saved")
