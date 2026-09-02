"""Small helper around vncdotool to drive LibreCAD on Qt's VNC platform."""
import os, subprocess, time, tempfile
from PIL import Image, ImageChops
import numpy as np
from vncdotool import api

TOOLS = os.path.dirname(os.path.abspath(__file__))

class LC:
    def __init__(self, variant, port, confdir, shots_dir, args=()):
        import socket
        self.variant, self.port = variant, port
        self.shots_dir = shots_dir
        os.makedirs(shots_dir, exist_ok=True)
        # seed the isolated configuration: no welcome dialog, deterministic window geometry
        os.makedirs(f"{confdir}/config/LibreCAD", exist_ok=True)
        conf = f"{confdir}/config/LibreCAD/LibreCAD.conf"
        if not os.path.exists(conf):
            with open(conf, "w") as f:
                f.write("[Startup]\nFirstLoad=0\n\n[Appearance]\nLanguage=en\nLanguageCmd=en\n\n"
                        "[Geometry]\nWindowWidth=1700\nWindowHeight=1300\nWindowX=0\nWindowY=0\n")
        self.proc = subprocess.Popen([f"{TOOLS}/run_lc.sh", variant, str(port), confdir, *args],
                                     stdout=open(f"{shots_dir}/app.log", "w"), stderr=subprocess.STDOUT,
                                     start_new_session=True)
        self.client = None
        self.n = 0
        # wait until the VNC server accepts connections
        for _ in range(120):
            time.sleep(1)
            if self.proc.poll() is not None:
                raise RuntimeError("librecad exited early, see app.log")
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=1):
                    break
            except OSError:
                continue
        else:
            raise RuntimeError("VNC port never opened")
        time.sleep(1)
        self.client = api.connect(f"localhost::{port}", password=None, timeout=10)
        self.client.captureScreen(f"{shots_dir}/_connect.png")

    def shot(self, name):
        self.n += 1
        path = f"{self.shots_dir}/{self.n:02d}_{name}.png"
        self.client.captureScreen(path)
        return Image.open(path).convert("RGB")

    def wait_stable(self, timeout=30, interval=0.7):
        """wait until two consecutive screenshots are identical"""
        prev = None
        for _ in range(int(timeout / interval)):
            time.sleep(interval)
            path = f"{self.shots_dir}/_tmp.png"
            self.client.captureScreen(path)
            cur = Image.open(path).convert("RGB")
            if prev is not None and ImageChops.difference(prev, cur).getbbox() is None:
                return cur
            prev = cur
        return prev

    def key(self, k, pause=0.5):
        self.client.keyPress(k); time.sleep(pause)

    def keys(self, ks, pause=0.5):
        for k in ks:
            self.key(k, pause)

    def move(self, x, y, pause=0.2):
        self.client.mouseMove(x, y); time.sleep(pause)

    def click(self, x, y, button=1, pause=0.5):
        self.client.mouseMove(x, y); time.sleep(0.15)
        self.client.mousePress(button); time.sleep(pause)

    def close(self):
        import signal
        try:
            if self.client: self.client.disconnect()
        except Exception: pass
        try:
            os.killpg(os.getpgid(self.proc.pid), signal.SIGTERM); self.proc.wait(10)
        except Exception:
            try: os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
            except Exception: pass
        try: api.shutdown()
        except Exception: pass

def changed_blobs(idle, cur, region, thr=30, cell=6, min_cells=6):
    """Bounding boxes (in full-screenshot coordinates) of the areas inside
    region=(x0,y0,x1,y1) which differ between two screenshots."""
    x0, y0, x1, y1 = region
    a = np.asarray(idle.crop(region)).astype(int); b = np.asarray(cur.crop(region)).astype(int)
    d = (np.abs(a - b).sum(axis=2) > thr)
    h, w = d.shape
    gh, gw = (h + cell - 1) // cell, (w + cell - 1) // cell
    grid = np.zeros((gh, gw), dtype=bool)
    for gy in range(gh):
        for gx in range(gw):
            if d[gy*cell:(gy+1)*cell, gx*cell:(gx+1)*cell].any():
                grid[gy, gx] = True
    seen = np.zeros_like(grid); blobs = []
    for gy in range(gh):
        for gx in range(gw):
            if grid[gy, gx] and not seen[gy, gx]:
                stack = [(gy, gx)]; seen[gy, gx] = True; cells = []
                while stack:
                    cy, cx = stack.pop(); cells.append((cy, cx))
                    for ny, nx in ((cy-1,cx),(cy+1,cx),(cy,cx-1),(cy,cx+1),(cy-1,cx-1),(cy-1,cx+1),(cy+1,cx-1),(cy+1,cx+1)):
                        if 0 <= ny < gh and 0 <= nx < gw and grid[ny, nx] and not seen[ny, nx]:
                            seen[ny, nx] = True; stack.append((ny, nx))
                if len(cells) >= min_cells:
                    ys = [c[0] for c in cells]; xs = [c[1] for c in cells]
                    blobs.append((x0 + min(xs)*cell, y0 + min(ys)*cell, x0 + (max(xs)+1)*cell, y0 + (max(ys)+1)*cell))
    return blobs
