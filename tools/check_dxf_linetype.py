#!/usr/bin/python3
"""check_dxf_linetype.py <file.dxf> <LTYPE[,LTYPE...]>

Exit 0 when every requested linetype (a) is defined in the LTYPE table and
(b) is referenced by at least one LINE entity in model space. Prints the
compiled dash pattern of each so the acad.lin metrics can be eyeballed.
Uses the system python3 + ezdxf (no venv needed)."""
import sys
import ezdxf

doc = ezdxf.readfile(sys.argv[1])
wanted = [w.upper() for w in sys.argv[2].split(",")]
ltypes = {lt.dxf.name.upper(): lt for lt in doc.linetypes}
used = sorted({e.dxf.linetype.upper() for e in doc.modelspace() if e.dxftype() == "LINE"})
ok = True
for w in wanted:
    lt = ltypes.get(w)
    pattern = None
    if lt is not None:   # raw group-49 dash lengths, signed as in acad.lin (+dash / -gap)
        pattern = [t.value for t in lt.pattern_tags.tags if t.code == 49]
    hit = lt is not None and w in used
    ok = ok and hit
    print(f"{w:12s} LTYPE={'yes' if lt else 'NO':3s} used-by-LINE={'yes' if w in used else 'NO':3s} pattern={pattern}")
print("LINE linetypes in file:", used)
sys.exit(0 if ok else 1)
