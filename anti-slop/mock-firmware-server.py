"""Mock of the ESP_Leveller firmware HTTP surface, for click-through testing the web UI.

Usage:  python mock-firmware-server.py [path/to/index.html] [port]

The page is the HTML literal embedded in ESP_Leveller.ino. If no path is given,
look for index.html beside this script, then try to extract it from the sketch.
"""
import json, os, re, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
SKETCH = os.path.join(HERE, os.pardir, "ESP_Leveller.ino")


def find_page():
    if len(sys.argv) > 1:
        return sys.argv[1]
    beside = os.path.join(HERE, "index.html")
    if os.path.exists(beside):
        return beside
    with open(SKETCH, encoding="utf-8") as f:
        return re.search(r'R"rawliteral\(\n(.*?)\n\)rawliteral"', f.read(), re.S).group(1)


PAGE = find_page()
if os.path.exists(PAGE):
    with open(PAGE, encoding="utf-8") as f:
        PAGE = f.read()
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8899

STATE = {"state": "IDLE", "tilt": 0.0, "thr": 15, "t": 0}
T0 = time.time()


def build():
    s = dict(STATE)
    if s["state"] == "PLAYING":
        s["t"] = int((time.time() - T0) * 1000)
    return json.dumps({"state": s["state"], "tilt": round(s["tilt"], 1),
                       "thr": s["thr"], "t": s["t"]})


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def _send(self, code, body, ctype="application/json"):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/":
            return self._send(200, PAGE, "text/html; charset=utf-8")
        if path == "/api/state":
            return self._send(200, build())
        if path == "/api/reset":
            STATE.update({"state": "IDLE", "tilt": 0.0, "thr": 15, "t": 0})
            globals()["T0"] = time.time()
            return self._send(200, "reset", "text/plain")
        if path == "/api/events":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            self.wfile.write(("data: " + build() + "\n\n").encode())
            self.wfile.flush()
            while True:
                time.sleep(0.1)
                self.wfile.write(("data: " + build() + "\n\n").encode())
                self.wfile.flush()
        self._send(404, '{"err":"no route"}')

    def do_POST(self):
        path = self.path.split("?")[0]
        if path == "/api/start":
            if STATE["state"] == "IDLE":
                STATE["state"] = "PLAYING"
                globals()["T0"] = time.time()
                STATE["t"] = 0
            elif STATE["state"] == "GAMEOVER":
                STATE["state"] = "IDLE"
        elif path == "/api/abort":
            if STATE["state"] == "PLAYING":
                STATE["t"] = int((time.time() - T0) * 1000)
                STATE["state"] = "GAMEOVER"
            elif STATE["state"] == "COUNTDOWN":
                STATE["state"] = "IDLE"
        elif path == "/api/calibrate":
            STATE["tilt"] = 0.0
            STATE["state"] = "IDLE"
        elif path == "/api/threshold":
            m = re.search(r"val=(\d+)", self.path)
            if m and STATE["state"] == "IDLE":
                v = int(m.group(1))
                if 5 <= v <= 90:
                    STATE["thr"] = v
        self._send(200, "ok", "text/plain")


# test hook: let the driver force states and tilt
def drift():
    while True:
        time.sleep(0.1)
        if STATE["state"] == "PLAYING":
            STATE["tilt"] = min(90.0, STATE["tilt"] + 2.0)


threading.Thread(target=drift, daemon=True).start()
ThreadingHTTPServer(("127.0.0.1", 8899), H).serve_forever()
