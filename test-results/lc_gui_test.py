"""GUI regression test for the tool button checked state (LibreCAD 2.2.1 branch).

Drives a headless LibreCAD (Qt VNC platform) through the real menus/toolbars and
checks which buttons of the left dock widget are shown as checked.
Usage: lc_gui_test.py <variant: baseline|fix> <vnc-port> <out-dir>
"""
import sys, os, json, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw
import numpy as np
import lcvnc

VARIANT, PORT, OUT = sys.argv[1], int(sys.argv[2]), sys.argv[3]
os.makedirs(OUT, exist_ok=True)

# ---- fixed coordinates for the 1800x1400 screen / 1700x1300 window layout ----
WIDGETS_MENU = (652, 21)
DOCK_ENTRY = {"Line": 13, "Modify": 14, "Select": 17}                  # position in Widgets > Dock Widgets (keyboard navigation)
VIEW_SAFE = (600, 800)                                    # inside the drawing area, away from widgets
SET_REL_ZERO, LOCK_REL_ZERO, ZOOM_PAN = (782, 1205), (840, 1205), (1322, 65)
COMMAND_LINE = (1200, 1150)
LAYER_FILTER = (1100, 228)      # a line edit outside the MDI area: Return propagates to the main window
MOUSE_HINT = (760, 1235, 910, 1300)   # left mouse button hint of the status bar
DOCK_REGION = (60, 250, 270, 1150)   # the tool buttons of the left dock (title bar excluded)
TOOL_KEYS = {"DrawLine": ("l", "2"), "DrawLineAngle": ("l", "a"), "DrawLineHorizontal": ("l", "h"),
             "DrawLineParallel": ("l", "p"), "ModifyMove": ("m", "enter"),   # Move / Copy is the first entry of the Modify submenu
             "SelectSingle": ("s", "down", "down", "enter"), "SelectWindow": ("s", "down", "down", "down", "down", "enter")}
LINE_ORDER = ["DrawLine", "DrawLineAngle", "DrawLineHorizontal", "DrawLineVertical", "DrawLineRectangle",
              "DrawLineParallelThrough", "DrawLineParallel", "DrawLineBisector", "DrawLineTangent1", "DrawLineTangent2",
              "DrawLineOrthTan", "DrawLineOrthogonal", "DrawLineRelAngle", "DrawLinePolygonCenCor", "DrawLinePolygonCenTan",
              "DrawLinePolygonCorCor", "DrawStar", "DrawPoint", "DrawLinePoints", "DrawLineRectangle1Point",
              "DrawLineRectangle2Points", "DrawLineRectangle3Points", "DrawCross", "DrawLineRel", "DrawLineRelX",
              "DrawLineRelY", "DrawLineAngleRel", "DrawLineOrthogonalRel", "DrawLineFromPointToLine"]
SELECT_ORDER = ["DeselectAll", "SelectAll", "SelectSingle", "SelectContour", "SelectWindow", "DeselectWindow",
                "SelectIntersected", "DeselectIntersected", "SelectLayer", "SelectInvert"]
MODIFY_ORDER = ["ModifyMove", "ModifyDuplicate", "ModifyRotate", "ModifyScale", "ModifyMirror", "ModifyMoveRotate",
                "ModifyRotate2", "ModifyRevertDirection", "ModifyTrim", "ModifyTrim2", "ModifyTrimAmount", "ModifyLineJoin",
                "ModifyBreakDivide", "ModifyLineGap", "ModifyOffset", "ModifyBevel", "ModifyRound", "ModifyCut",
                "ModifyStretch", "ModifyEntity", "ModifyAttributes", "ModifyExplodeText", "BlocksExplode", "ModifyDeleteQuick"]

def runs(v, thr):
    out, start = [], None
    for i, x in enumerate(v):
        if x > thr and start is None: start = i
        if x <= thr and start is not None: out.append((start, i)); start = None
    if start is not None: out.append((start, len(v)))
    return out

def detect_grid(img, region, names):
    """find the tool buttons (light frames on the dock background) inside region; returns {name: rect}"""
    x0, y0, x1, y1 = region
    a = np.asarray(img.crop(region).convert("RGB")).astype(int)
    bg = np.median(a.reshape(-1, 3), axis=0)
    mask = np.abs(a - bg).sum(axis=2) > 8     # button faces and frames are only slightly lighter than the dock
    cols = [(s + x0, e + x0) for s, e in runs(mask.sum(axis=0), 8) if 30 <= e - s <= 42]
    if not cols: raise RuntimeError("no button columns found")
    sub = mask[:, cols[0][0] - x0: cols[-1][1] - x0]
    rows = [(s + y0, e + y0) for s, e in runs(sub.sum(axis=1), 8) if 30 <= e - s <= 42]
    rects = [(cx0, ry0, cx1, ry1) for ry0, ry1 in rows for cx0, cx1 in cols]
    return {n: r for n, r in zip(names, rects)}

def checked_set(idle, cur, grid, thr=8.0):
    out = set()
    for name, (x0, y0, x1, y1) in grid.items():
        r = (x0 + 3, y0 + 3, x1 - 3, y1 - 3)
        a = np.asarray(idle.crop(r).convert("RGB")).astype(int); b = np.asarray(cur.crop(r).convert("RGB")).astype(int)
        if np.abs(a - b).mean() > thr: out.add(name)
    return out

def region_changed(idle, cur, center, half=14, thr=8.0):
    if len(center) == 4:
        r = center
    else:
        r = (center[0] - half, center[1] - half, center[0] + half, center[1] + half)
    a = np.asarray(idle.crop(r).convert("RGB")).astype(int); b = np.asarray(cur.crop(r).convert("RGB")).astype(int)
    return float(np.abs(a - b).mean()) > thr

class Test:
    def __init__(self):
        self.lc = lcvnc.LC(VARIANT, PORT, f"{OUT}/conf", f"{OUT}/shots")
        self.lc.wait_stable(timeout=60)
        self.results = []; self.steps = []
        self.grid = {}; self.idle = None; self.dock = None

    def toggle_dock(self, name):
        # keyboard only: Alt focuses the menu bar, 6x Right = "Widgets", Down opens it,
        # Down = "Dock Widgets", Right opens the submenu, N x Down = the dock, Enter toggles it
        lc = self.lc
        lc.click(*WIDGETS_MENU); lc.keys(["down", "down", "right"], 0.4)
        lc.keys(["down"] * DOCK_ENTRY[name], 0.2); lc.keys(["enter"], 1.0); lc.move(*VIEW_SAFE); time.sleep(0.5)

    def use_dock(self, name):
        if self.dock == name: return
        if self.dock is not None: self.toggle_dock(self.dock)
        self.toggle_dock(name); self.dock = name
        self.lc.keys(["esc"], 0.5)
        self.idle = self.lc.wait_stable(timeout=10)
        self.grid = detect_grid(self.idle, DOCK_REGION, {"Line": LINE_ORDER, "Modify": MODIFY_ORDER, "Select": SELECT_ORDER}[name])
        print(f"[{VARIANT}] dock {name}: {len(self.grid)} buttons detected", flush=True)

    def tool(self, name, move=True):
        """choose a tool from the Tools menu (keyboard). move=False keeps the pointer where it is, so the
        next screenshot shows the state before any mouse event reaches the graphic view"""
        sub, *keys = TOOL_KEYS[name]
        self.lc.keys(["alt-t"], 0.6); self.lc.keys([sub], 0.6); self.lc.keys(keys, 0.4)
        if move: self.lc.move(*VIEW_SAFE)
        time.sleep(0.6)

    def check(self, scenario, step, expected, extra=None):
        cur = self.lc.shot(f"{scenario}_{step}".replace(" ", "_"))
        got = checked_set(self.idle, cur, self.grid)
        ok = (got == set(expected))
        rec = {"scenario": scenario, "step": step, "expected": sorted(expected), "got": sorted(got), "ok": ok}
        if extra: rec.update(extra(cur)); ok = ok and all(v for k, v in rec.items() if k.startswith("extra_ok"))
        rec["ok"] = ok
        self.results.append(rec)
        print(f"[{VARIANT}] {'PASS' if ok else 'FAIL'} {scenario} / {step}: expected {sorted(expected)} got {sorted(got)}"
              + (f" {[(k, v) for k, v in rec.items() if k.startswith('extra')]}" if extra else ""), flush=True)
        # keep a crop of the dock for the visual report
        self.steps.append((f"{scenario} / {step}", cur.crop((60, 150, 290, 700)), ok))
        return cur

    def cmd(self, text):
        """type a command in the command line"""
        self.lc.click(*COMMAND_LINE, pause=0.3)
        for ch in text:
            self.lc.client.keyPress(ch); time.sleep(0.05)
        time.sleep(0.2); self.lc.keys(["enter"], 0.8); self.lc.move(*VIEW_SAFE); time.sleep(0.2)

    def enter(self, pause=1.0):
        """Return typed in a widget outside the MDI area which does not consume it (a QLineEdit):
        it reaches QC_ApplicationWindow::keyPressEvent -> slotEnter() -> RS_EventHandler::enter()"""
        self.lc.click(*LAYER_FILTER, pause=0.3); self.lc.keys(["enter"], pause); self.lc.move(*VIEW_SAFE); time.sleep(0.3)

    def esc(self, pause=0.8):
        """Escape typed in the empty command line: 'back' = ends the current tool step/tool"""
        self.lc.click(*COMMAND_LINE, pause=0.3); self.lc.keys(["esc"], pause); self.lc.move(*VIEW_SAFE); time.sleep(0.3)

    def reset(self):
        for _ in range(4): self.esc(0.3)
        self.lc.move(*VIEW_SAFE); time.sleep(0.5)

    def run(self):
        lc = self.lc
        self.use_dock("Line")
        # S1 - the reported bug: choosing another tool must release the previous button
        self.reset(); self.check("S1 switch tools", "idle", [])
        self.tool("DrawLine"); self.check("S1 switch tools", "2 Points", ["DrawLine"])
        self.tool("DrawLineAngle"); self.check("S1 switch tools", "Angle", ["DrawLineAngle"])
        self.tool("DrawLineHorizontal"); self.check("S1 switch tools", "Horizontal", ["DrawLineHorizontal"])
        # Escape (typed in the command line) ends the current tool only, the tools started before resume one by one
        self.esc(); self.check("S1 switch tools", "Escape: Horizontal ends, Angle resumes", ["DrawLineAngle"])
        self.esc(); self.check("S1 switch tools", "Escape: Angle ends, 2 Points resumes", ["DrawLine"])
        self.esc(); self.check("S1 switch tools", "Escape: nothing left", [])
        # S2 - finishing the tool with the right mouse button releases the button
        self.reset(); self.tool("DrawLine"); self.check("S2 right click finishes", "2 Points", ["DrawLine"])
        lc.click(*VIEW_SAFE, button=3, pause=0.8); self.check("S2 right click finishes", "right click", [])
        # S3 - a tool started on top of another one: only the current one is checked, the resumed one gets its button back
        self.reset(); self.tool("DrawLine"); self.tool("DrawLineAngle")
        self.check("S3 stacked tools", "2 Points then Angle", ["DrawLineAngle"])
        lc.click(*VIEW_SAFE, button=3, pause=0.8); self.check("S3 stacked tools", "right click ends Angle, 2 Points resumes", ["DrawLine"])
        self.esc(); self.check("S3 stacked tools", "Escape", [])
        # S4 - drawing a line: button stays while drawing, released when the tool ends
        self.reset(); self.tool("DrawLine"); lc.click(450, 600, pause=0.6); lc.click(650, 800, pause=0.6); lc.move(*VIEW_SAFE)
        self.check("S4 draw a line", "two points clicked", ["DrawLine"])
        lc.click(*VIEW_SAFE, button=3, pause=0.8); self.check("S4 draw a line", "right click (back to first point)", ["DrawLine"])
        lc.click(*VIEW_SAFE, button=3, pause=0.8); self.check("S4 draw a line", "right click (finished)", [])
        # S6 - the same tool chosen twice: still a single checked button
        self.reset(); self.tool("DrawLine"); self.tool("DrawLine"); self.check("S6 same tool twice", "2 Points twice", ["DrawLine"])
        self.esc(); self.check("S6 same tool twice", "Escape: second ends, first resumes", ["DrawLine"])
        self.esc(); self.check("S6 same tool twice", "Escape: nothing left", [])
        # S7 - the relative zero toggles must not interfere (issue #2012)
        self.reset(); lock_idle = self.idle
        lc.click(*LOCK_REL_ZERO, pause=0.8); lc.move(*VIEW_SAFE); time.sleep(0.5)
        locked = lambda cur: {"extra_lock_checked": region_changed(lock_idle, cur, LOCK_REL_ZERO)}
        self.check("S7 lock relative zero", "lock clicked", [], lambda cur: {**locked(cur), "extra_ok": region_changed(lock_idle, cur, LOCK_REL_ZERO)})
        self.tool("DrawLine"); self.tool("DrawLineAngle")
        self.check("S7 lock relative zero", "tools used while locked", ["DrawLineAngle"], lambda cur: {**locked(cur), "extra_ok": region_changed(lock_idle, cur, LOCK_REL_ZERO)})
        self.esc()
        self.check("S7 lock relative zero", "Escape: Angle ends, 2 Points resumes, still locked", ["DrawLine"], lambda cur: {**locked(cur), "extra_ok": region_changed(lock_idle, cur, LOCK_REL_ZERO)})
        self.esc()
        self.check("S7 lock relative zero", "Escape: nothing left, still locked", [], lambda cur: {**locked(cur), "extra_ok": region_changed(lock_idle, cur, LOCK_REL_ZERO)})
        lc.click(*LOCK_REL_ZERO, pause=0.8); lc.move(*VIEW_SAFE); time.sleep(0.5)
        self.check("S7 lock relative zero", "unlocked again", [], lambda cur: {**locked(cur), "extra_ok": not region_changed(lock_idle, cur, LOCK_REL_ZERO)})
        # S7b - "set relative zero" in the middle of a tool keeps the tool's button
        self.reset(); self.tool("DrawLine"); lc.click(*SET_REL_ZERO, pause=0.8); lc.move(*VIEW_SAFE)
        self.check("S7b set relative zero inside a tool", "set relative zero clicked", ["DrawLine"])
        lc.click(500, 650, pause=0.8); lc.move(*VIEW_SAFE); self.check("S7b set relative zero inside a tool", "point clicked, tool resumed", ["DrawLine"])
        self.esc(); self.check("S7b set relative zero inside a tool", "Escape", [])
        # S8 - zoom pan on top of a tool: the tool gets its button back when the pan ends
        self.reset(); self.tool("DrawLine"); lc.click(*ZOOM_PAN, pause=0.8); lc.move(*VIEW_SAFE)
        self.check("S8 zoom pan inside a tool", "zoom pan active", [])
        lc.click(*VIEW_SAFE, button=3, pause=0.8); self.check("S8 zoom pan inside a tool", "right click ends pan, 2 Points resumes", ["DrawLine"])
        self.esc(); self.check("S8 zoom pan inside a tool", "Escape", [])
        # S5 - modify chain: the Move button stays checked from the selection to the move
        self.reset(); self.use_dock("Modify")
        # a line drawn from the command line, nothing selected: Move starts with the selection phase
        self.cmd("line"); self.cmd("0,0"); self.cmd("100,100"); self.esc(); self.esc(); self.cmd("deselectall")
        self.tool("ModifyMove"); self.check("S5 move chain", "Move chosen (selection)", ["ModifyMove"])
        self.cmd("sa"); before = self.check("S5 move chain", "all selected (command sa)", ["ModifyMove"])
        self.enter(); lc.move(*VIEW_SAFE)
        # the mouse hint must change from "Select to move" to the reference point prompt
        self.check("S5 move chain", "Enter: move phase", ["ModifyMove"],
                   lambda cur: {"extra_hint_changed": region_changed(before, cur, MOUSE_HINT), "extra_ok": region_changed(before, cur, MOUSE_HINT)})
        self.esc(); self.check("S5 move chain", "Escape", [])
        # S5b - another tool started during the selection phase: the modify button is released, and back when the tool ends
        self.reset(); self.tool("ModifyMove"); self.tool("DrawLine", move=False)
        # the Line dock is hidden here: the Move button must be released (2 Points has the button now)
        self.check("S5b tool during selection", "Move then 2 Points (before any mouse event)", []); lc.move(*VIEW_SAFE)
        self.esc(); self.check("S5b tool during selection", "Escape: 2 Points ends, Move resumes", ["ModifyMove"])
        self.reset(); self.check("S5b tool during selection", "reset", [])
        # S5c - the modify button clicked again during the selection phase
        self.reset(); self.tool("ModifyMove"); self.tool("ModifyMove")
        self.check("S5c modify tool twice", "Move twice", ["ModifyMove"])
        self.reset(); self.check("S5c modify tool twice", "reset", [])
        # S5d - Select Window (a select tool from its button) during the selection phase: replaces the helper only
        self.reset(); self.cmd("deselectall"); self.tool("ModifyMove"); self.tool("SelectWindow", move=False)
        self.check("S5d select window during selection", "Move then Select Window (before any mouse event)", []); lc.move(*VIEW_SAFE)
        lc.click(*VIEW_SAFE, button=3, pause=0.8); self.check("S5d select window during selection", "right click ends Select Window, Move resumes", ["ModifyMove"])
        lc.click(*VIEW_SAFE, button=3, pause=0.8); self.check("S5d select window during selection", "right click cancels Move", [])
        # S5e - right click cancels the selection phase
        self.reset(); self.cmd("deselectall"); self.tool("ModifyMove"); self.check("S5e right click cancels selection", "Move chosen", ["ModifyMove"])
        lc.click(*VIEW_SAFE, button=3, pause=0.8); self.check("S5e right click cancels selection", "right click", [])
        # S9a - Select Entity during the selection phase: no action is started, Move keeps its button
        #       (observed before any mouse event reaches the view)
        self.reset(); self.cmd("deselectall"); self.tool("ModifyMove"); self.tool("SelectSingle", move=False)
        self.check("S9a select entity during selection", "Move then Select Entity (before any mouse event)", ["ModifyMove"]); lc.move(*VIEW_SAFE)
        self.check("S9a select entity during selection", "after a mouse move", ["ModifyMove"])
        self.reset(); self.check("S9a select entity during selection", "reset", [])
        # S9 - Select Entity during the selection phase: no action is started, its button must not stay checked
        self.reset(); self.use_dock("Select")
        self.cmd("deselectall"); self.tool("ModifyMove"); self.tool("SelectSingle", move=False)
        self.check("S9 select entity during selection", "Move then Select Entity (before any mouse event)", []); lc.move(*VIEW_SAFE)
        self.reset(); self.check("S9 select entity during selection", "reset", [])
        # S9b - Select Entity chosen twice from idle: still running, single button
        self.reset(); self.tool("SelectSingle"); self.check("S9b select entity twice", "Select Entity", ["SelectSingle"])
        self.tool("SelectSingle"); self.check("S9b select entity twice", "Select Entity again", ["SelectSingle"])
        self.esc(); self.check("S9b select entity twice", "Escape", [])
        self.finish()

    def finish(self):
        self.lc.close()
        json.dump(self.results, open(f"{OUT}/results.json", "w"), indent=1)
        # visual report: one column per step
        cols = 6; w, h = 230, 550; rows = (len(self.steps) + cols - 1) // cols
        m = Image.new("RGB", (cols * (w + 10) + 10, rows * (h + 40) + 10), "white"); d = ImageDraw.Draw(m)
        for i, (label, crop, ok) in enumerate(self.steps):
            x = 10 + (i % cols) * (w + 10); y = 10 + (i // cols) * (h + 40)
            m.paste(crop, (x, y + 30)); d.text((x, y), label[:40], fill="black"); d.text((x, y + 12), "PASS" if ok else "FAIL", fill="green" if ok else "red")
        m.save(f"{OUT}/report.png")
        n_ok = sum(r["ok"] for r in self.results)
        print(f"[{VARIANT}] {n_ok}/{len(self.results)} checks passed", flush=True)

if __name__ == "__main__":
    t = Test()
    try:
        t.run()
    except Exception:
        import traceback; traceback.print_exc()
        try: t.lc.shot("crash"); t.finish()
        except Exception: pass
        sys.exit(2)
