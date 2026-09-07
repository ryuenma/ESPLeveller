# P2 step 2: DSN export -> Freerouting (Java 21) -> SES import
import pcbnew, subprocess, os, sys

BOARD = r"C:\Users\ryuen\Documents\Projects\Devices\ESP_Leveller\pcb\ESPLeveller\ESPLeveller.kicad_pcb"
DSN = BOARD.replace(".kicad_pcb", ".dsn")
SES = BOARD.replace(".kicad_pcb", ".ses")
JAR = os.path.expanduser("~/.kicad-mcp/freerouting-2.1.0.jar")
JAVA = r"C:\Program Files\Eclipse Adoptium\jre-21.0.12.101-hotspot\bin\java.exe"

b = pcbnew.LoadBoard(BOARD)
r = pcbnew.ExportSpecctraDSN(b, DSN)
print("export dsn:", r, os.path.getsize(DSN), "bytes")

cmd = [JAVA, "-jar", JAR, "-de", DSN, "-do", SES, "--gui.enabled=false", "-mp", "60", "-mt", "1"]
print("routing:", " ".join(cmd))
p = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
print("rc:", p.returncode)
print(p.stdout[-2000:])
print(p.stderr[-1000:])
if not os.path.isfile(SES):
    print("NO SES PRODUCED"); sys.exit(1)
print("ses size:", os.path.getsize(SES))

b = pcbnew.LoadBoard(BOARD)
r = pcbnew.ImportSpecctraSES(b, SES)
print("import ses:", r)
b.Save(BOARD)
print("saved")
