"""GUI scenarios for the PHANTOM linetype family on LibreCAD master (Qt vnc platform).

Usage: LC_RUNNER=tools/run_lc_master.sh LC_CONF_FILE=LibreCAD-2.conf \
       tools/venv/bin/python tools/lc_gui_test_phantom.py <build: base|hidden> <port> <out-dir>

Scenarios (each one is a fresh LibreCAD instance with an isolated config):
  G1  pen toolbar line-type list          -> shot of the open combobox (row 27:
                                             "Phantom" with the fix, "Border" without)
  G1F same, scrolled to the four Phantom rows; G1E scrolled to the end (Border kept)
  G2  pick "Phantom", draw a line, Save As -> saved DXF checked by name + LTYPE table
  G3  reopen the G2 file, sa + Properties -> Properties dock shows the line type
  G4  open single_<NAME>.dxf + Properties -> Properties dock per acad.lin name
  G5  open phantom_family.dxf, Save As    -> resaved DXF checked (names kept or lost)
Results: <out>/results.json, shots under <out>/shots_*/, DXF outputs under <out>/.
Screen coordinates are for the 1800x1400 vnc screen with the 1700x1300 window seeded
by lcvnc.py (master layout; see lc_calibrate.py)."""
import json
import os
import subprocess
import sys
import time

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
from lcvnc import LC  # noqa: E402
from vncdotool import api  # noqa: E402

os.environ["LC_NO_SHUTDOWN"] = "1"   # several LibreCAD instances per run, one reactor

LINETYPE_BOX = (400, 79)                 # third combobox of the pen toolbar
LINETYPE_BOX_RECT = (316, 64, 494, 94)   # crop for evidence
VIEW_SAFE = (700, 700)
PROPERTIES_TAB = (1790, 770)             # "Proper..." tab of the right dock bar
PROPERTIES_RECT = (1390, 95, 1775, 300)  # General section of the Properties dock
FIXTURES = os.path.join(TOOLS, "..", "test-results", "phantom", "fixtures")
CHECK = os.path.join(TOOLS, "check_dxf_linetype.py")


class Session:
    def __init__(self, build, port, out, tag, args=()):
        self.out = out
        self.tag = tag
        conf = os.path.join(out, f"conf_{tag}")
        self.lc = LC(build, port, conf, os.path.join(out, f"shots_{tag}"), args=args)
        self.lc.wait_stable(timeout=60)

    # -- primitives ---------------------------------------------------------
    def type(self, text, pause=0.04):
        for ch in text:
            self.lc.client.keyPress(ch)
            time.sleep(pause)

    def cmd(self, text):
        """Ctrl+M focuses the command line (FocusCommand), no coordinates needed."""
        self.lc.keys(["ctrl-m"], 0.5)
        self.type(text)
        self.lc.keys(["enter"], 0.8)
        self.lc.move(*VIEW_SAFE)

    def esc(self, n=1):
        for _ in range(n):
            self.lc.keys(["ctrl-m"], 0.3)
            self.lc.keys(["esc"], 0.5)
        self.lc.move(*VIEW_SAFE)

    def shot_crop(self, name, rect):
        img = self.lc.wait_stable(timeout=8)
        path = os.path.join(self.lc.shots_dir, f"{name}.png")
        img.crop(rect).save(path)
        return path

    # -- helpers ------------------------------------------------------------
    def open_linetype_box(self, name, steps=11):
        """Focus the combobox, open its popup with Alt+Down (a mouse click opens it
        too, but the popup is not captured by the vnc screen), then move the
        highlight `steps` rows down so the wanted entries scroll into view
        (10 rows visible; row 0 = "By Layer", 26 = "Center (large)").
        Esc leaves the current value untouched."""
        self.lc.click(*LINETYPE_BOX, pause=0.4)
        self.lc.keys(["esc"], 0.5)
        self.lc.keys(["alt-down"], 1.5)
        for _ in range(steps):
            self.lc.keys(["down"], 0.15)
        img = self.lc.wait_stable(timeout=8)
        path = os.path.join(self.lc.shots_dir, f"{name}.png")
        img.crop((300, 60, 700, 560)).save(path)
        self.lc.keys(["esc"], 0.8)
        self.lc.move(*VIEW_SAFE)
        return path

    def set_pen_linetype(self, text):
        """Open the combobox and use the popup's keyboard search on `text`."""
        self.lc.click(*LINETYPE_BOX, pause=1.0)
        self.lc.wait_stable(timeout=8)
        self.type(text.lower(), pause=0.12)
        self.lc.keys(["enter"], 0.8)
        self.lc.move(*VIEW_SAFE)
        return self.shot_crop("pen_linetype_selected", LINETYPE_BOX_RECT)

    def draw_line(self, p1="0,0", p2="100,50"):
        self.cmd("line")
        self.cmd(p1)
        self.cmd(p2)
        self.esc(2)

    def save_as(self, path):
        if os.path.exists(path):
            os.remove(path)
        self.lc.keys(["alt-f"], 0.8)
        self.lc.keys(["a"], 1.5)             # "Save &as..."
        self.lc.wait_stable(timeout=15)
        self.lc.shot("saveas_dialog")
        self.type(path)
        self.lc.keys(["enter"], 1.5)
        for _ in range(30):
            if os.path.exists(path) and os.path.getsize(path) > 0:
                break
            time.sleep(0.5)
        else:
            self.lc.shot("saveas_failed")
            raise RuntimeError(f"save_as: {path} was not written")
        time.sleep(1.0)
        self.lc.wait_stable(timeout=15)
        self.lc.move(*VIEW_SAFE)

    def properties_shot(self, name):
        """Select everything and open the Properties dock (right tab bar): its
        General section lists the selection's line type by name."""
        self.cmd("sa")
        self.lc.click(*PROPERTIES_TAB, pause=1.5)
        img = self.lc.wait_stable(timeout=10)
        img.save(os.path.join(self.lc.shots_dir, f"{name}_full.png"))
        path = os.path.join(self.lc.shots_dir, f"{name}.png")
        img.crop(PROPERTIES_RECT).save(path)
        self.cmd("sx")
        return path

    def close(self):
        self.lc.close()


def check_dxf(path, names):
    proc = subprocess.run(["/usr/bin/python3", CHECK, path, ",".join(names)],
                          capture_output=True, text=True)
    return {"ok": proc.returncode == 0, "report": proc.stdout.strip()}


def main():
    build, port, out = sys.argv[1], int(sys.argv[2]), os.path.abspath(sys.argv[3])
    only = [a.split("=", 1)[1] for a in sys.argv[4:] if a.startswith("--only=")]
    only = set(only[0].split(",")) if only else None
    os.makedirs(out, exist_ok=True)
    results = {"build": build, "scenarios": {}}

    def wanted(tag):
        return only is None or tag in only

    # NOTE: one combobox popup per LibreCAD instance. After a popup is dismissed
    # with Esc, the Qt vnc server stops answering the next popup, so G1, G1F and
    # G2 each get their own instance.
    if wanted("G1"):
        s = Session(build, port, out, "g1")
        try:
            results["scenarios"]["G1_combobox_list"] = s.open_linetype_box("combo_open", steps=27)
        finally:
            s.close()
    if wanted("G1F"):
        s = Session(build, port, out, "g1f")
        try:
            results["scenarios"]["G1F_combobox_family"] = s.open_linetype_box("combo_open_family", steps=30)
        finally:
            s.close()

    if wanted("G1E"):
        s = Session(build, port, out, "g1e")
        try:
            results["scenarios"]["G1E_combobox_end"] = s.open_linetype_box("combo_open_end", steps=34)
        finally:
            s.close()

    saved = os.path.join(out, "phantom_saved.dxf")
    if wanted("G2"):
        s = Session(build, port, out, "g2")
        try:
            sel = s.set_pen_linetype("phantom")
            s.draw_line()
            s.save_as(saved)
            results["scenarios"]["G2_pick_phantom_draw_save"] = {
                "combobox_crop": sel, "dxf": saved,
                "check": check_dxf(saved, ["PHANTOM"])}
        finally:
            s.close()

    if wanted("G3") and os.path.exists(saved):
        s = Session(build, port, out, "g3", args=(saved,))
        try:
            results["scenarios"]["G3_reopen_properties"] = s.properties_shot("properties_reopened")
        finally:
            s.close()

    if wanted("G4"):
        g4 = {}
        for name in ("PHANTOM", "PHANTOM2", "PHANTOMX2"):
            fx = os.path.join(FIXTURES, f"single_{name}.dxf")
            s = Session(build, port, out, f"g4_{name}", args=(fx,))
            try:
                g4[name] = s.properties_shot(f"properties_{name}")
            finally:
                s.close()
        results["scenarios"]["G4_fixture_properties"] = g4

    if wanted("G5"):
        fx = os.path.join(FIXTURES, "phantom_family.dxf")
        s = Session(build, port, out, "g5", args=(fx,))
        try:
            resaved = os.path.join(out, "phantom_family_resaved.dxf")
            s.save_as(resaved)
            results["scenarios"]["G5_fixture_resaved"] = {
                "dxf": resaved, "check": check_dxf(resaved, ["PHANTOM", "PHANTOM2", "PHANTOMX2"])}
        finally:
            s.close()

    with open(os.path.join(out, "results.json"), "w") as f:
        json.dump(results, f, indent=2)
    print(json.dumps(results, indent=2))
    try:
        api.shutdown()
    except Exception:
        pass


if __name__ == "__main__":
    main()
