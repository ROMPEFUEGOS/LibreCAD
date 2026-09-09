import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, "/home/kecojones/Documents/LibreCAD-fork/tools")
from lcvnc import LC
from vncdotool import api
os.environ["LC_NO_SHUTDOWN"] = "1"
variant, port, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
fx = "/home/kecojones/Documents/LibreCAD-fork/test-results/phantom/fixtures/phantom_family.dxf"
lc = LC(variant, port, os.path.join(out, "conf_canvas"), os.path.join(out, "shots_canvas"), args=(fx,))
lc.wait_stable(timeout=60)
# zoom auto via command line, then deselect
for cmd in ("za", "sx"):
    lc.keys(["ctrl-m"], 0.5)
    for ch in cmd: lc.client.keyPress(ch); time.sleep(0.05)
    lc.keys(["enter"], 1.0)
lc.move(700, 700)
img = lc.wait_stable(timeout=10)
img.save(os.path.join(out, "shots_canvas", "canvas_family_full.png"))
img.crop((80, 120, 1380, 820)).save(os.path.join(out, "canvas_family.png"))
lc.close()
try: api.shutdown()
except Exception: pass
print("ok")
