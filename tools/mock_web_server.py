#!/usr/bin/env python3
"""Serves the ESP32 web page (from tools/web_page.html) with a fake car behind it.

Run:  python3 tools/mock_web_server.py   then open http://localhost:8080
Test the "car is moving" lock with:  http://localhost:8080/mock/moving?on=1  (and on=0 to stop)
"""
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

LAMPS = [
    ("low_left", "Low beam, driver"), ("low_right", "Low beam, passenger"),
    ("fog_left", "Fog light, driver"), ("fog_right", "Fog light, passenger"),
    ("fog_rear", "Rear fog light"), ("reverse_left", "Reverse light, driver"),
    ("reverse_right", "Reverse light, passenger"), ("turn_left", "Turn signal, left"),
    ("turn_right", "Turn signal, right"), ("side_right", "Side marker, passenger"),
    ("rear_right_a", "Rear light, passenger A"), ("rear_right_b", "Rear light, passenger B"),
    ("interior", "Interior button lights"),
]
SEAT_MOVES = {"forward", "back", "up", "down", "tilt_up", "tilt_down", "backrest_forward",
              "backrest_back", "headrest_up", "headrest_down"}
AUTO_OFF_SECONDS = 120

car = {"moving": False, "locked": True, "lamps": set(), "lampsStarted": 0.0}
settings = {"unlock": 1, "unlockAny": 0, "lowbeam": 1, "lbSec": 60, "track": 1, "scrollMs": 300,
            "hdr": 1, "duck": 1, "duckPct": 30, "welcome": 0, "goodbye": 0,
            "zones": ["coolant", "rpm", "speed", "ambient"]}


def load_page():
    return (Path(__file__).resolve().parent / "web_page.html").read_text()


def state():
    started = time.time() - START
    if car["lamps"] and time.time() - car["lampsStarted"] > AUTO_OFF_SECONDS:
        car["lamps"].clear()
    left = max(0, int(AUTO_OFF_SECONDS - (time.time() - car["lampsStarted"]))) if car["lamps"] else 0
    speed = 48 if car["moving"] else 0
    return {
        "ign": 3, "speed": speed, "rpm": 2100 if car["moving"] else 800, "coolant": 87 if started > 5 else 43,
        "ambient": 16, "odo": 425829, "locked": int(car["locked"]), "lockKnown": 1, "reverse": 0, "hold": 0,
        "lamps": "".join("1" if lid in car["lamps"] else "0" for lid, _ in LAMPS), "lampsLeft": left,
        "range": "518 KM ", "cons": "10.6 L/100", "avg": "17.5 KM/H",
    }


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(self.command, self.path)

    def reply(self, body, content_type="application/json"):
        data = body.encode() if isinstance(body, str) else json.dumps(body).encode()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def result(self, ok, msg):
        self.reply({"ok": ok, "msg": msg})

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == "/":
            self.reply(load_page(), "text/html")
        elif url.path == "/api/state":
            self.reply(state())
        elif url.path == "/api/lamps":
            self.reply([{"id": i, "label": l} for i, l in LAMPS])
        elif url.path == "/api/settings":
            self.reply(settings)
        elif url.path == "/mock/moving":
            car["moving"] = parse_qs(url.query).get("on", ["0"])[0] == "1"
            self.reply({"moving": car["moving"]})
        else:
            self.send_error(404)

    def do_POST(self):
        url = urlparse(self.path)
        args = {k: v[0] for k, v in parse_qs(url.query).items()}
        if url.path == "/api/door":
            if car["moving"]:
                return self.result(False, "Refused: the car is moving")
            want_locked = args.get("act") == "lock"
            if want_locked == car["locked"]:
                return self.result(True, "Already locked" if want_locked else "Already unlocked")
            car["locked"] = want_locked
            self.result(True, "Locking" if want_locked else "Unlocking")
        elif url.path == "/api/seat":
            if car["moving"]:
                return self.result(False, "Refused: the car is moving")
            self.result(args.get("move") in SEAT_MOVES, "Moving" if args.get("move") in SEAT_MOVES else "Unknown seat move")
        elif url.path == "/api/lamp":
            on = args.get("on") == "1"
            if on and car["moving"]:
                return self.result(False, "Refused: the car is moving")
            ids = [i for i, _ in LAMPS] if args.get("id") == "all" else [args.get("id")]
            for lamp_id in ids:
                (car["lamps"].add if on else car["lamps"].discard)(lamp_id)
            if on:
                car["lampsStarted"] = time.time()
            self.result(True, "On" if on else "Off")
        elif url.path == "/api/stop":
            car["lamps"].clear()
            self.result(True, "Stopped")
        elif url.path == "/api/exit":
            self.result(True, "Restarting into Bluetooth audio")
        elif url.path == "/api/set":
            key, value = args.get("key", ""), args.get("value", "")
            if key.startswith("zone"):
                settings["zones"][int(key[4])] = value
            elif key in settings:
                settings[key] = int(value)
            else:
                return self.result(False, "Unknown setting")
            self.result(True, "Saved")
        else:
            self.send_error(404)


START = time.time()
if __name__ == "__main__":
    print("Mock car on http://localhost:8080  (moving test: /mock/moving?on=1)")
    ThreadingHTTPServer(("127.0.0.1", 8080), Handler).serve_forever()
