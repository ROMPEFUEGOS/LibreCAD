"""Calibration shots for the master layout: idle window, command line focus (Ctrl+M),
and the pen toolbar line-type combobox opened.  Usage: lc_calibrate.py <build> <port> <out>"""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lcvnc import LC
build, port, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
conf = f"{out}/conf"
lc = LC(build, port, conf, f"{out}/shots")
try:
    lc.wait_stable(timeout=60)
    lc.shot("idle")
    lc.keys(["ctrl-m"], 0.6)
    for ch in "line":
        lc.client.keyPress(ch); time.sleep(0.05)
    lc.shot("cmd_typed")
    lc.keys(["escape"], 0.5)
finally:
    lc.close()
print("ok", out)
