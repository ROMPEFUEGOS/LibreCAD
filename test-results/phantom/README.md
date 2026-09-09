# Evidencia — familia de tipo de línea PHANTOM (LibreCAD master)

Commit bajo prueba (worktree `src-hidden`, rama `feature/phantom-linetype-family`): `9cf22155d`, un solo commit sobre `origin/master` = `3f50782a5` (10/09/2026). Base de comparación: `src-master-base` en `3f50782a5` (master con HIDDEN, sin PHANTOM).

Builds: `src-master-base/build/base` (Release) y `src-hidden/build/hidden` (RelWithDebInfo, `-DBUILD_TESTS=ON -DLIBRECAD_REGISTER_INTEGRATION_TESTS=ON`). Ambas con `pixi` (Qt 6.8.2 de conda-forge, GCC 13.3), sin sudo, 0 warnings nuevos.

## Contenido

| ruta | qué es |
|---|---|
| `fixtures/phantom_family.dxf` | DXF R12 (ezdxf 1.4.3, `ezdxf.new("R12", setup=False)`) con LTYPE `PHANTOM`/`PHANTOM2`/`PHANTOMX2` en mm (acad.lin × 25.4), tres `LINE` con cada uno y una capa `PHANTOM_LAYER` con `PHANTOM` + una línea ByLayer |
| `fixtures/single_<NAME>.dxf` | una sola `LINE` con ese tipo, para el panel de Propiedades |
| `unit/unit_linetype_verbose.txt`, `unit/ci_lanes.txt` | los seis tests `[linetype]` con `-s` (159 asertos); resumen de `librecad_dxf_fast_tests` (521 casos) y `librecad_dwg_fast_tests` (909 pasan, 37 saltados) |
| `base/`, `fix/` | una carpeta por build: `results.json`, `shots_<escenario>/` (capturas + `app.log`), `phantom_saved.dxf` (G2), `phantom_family_resaved.dxf` (G5), `canvas_family.png` (lienzo con la fixture de familia, sin selección), `conf_<escenario>/` (configuración aislada) |
| `combobox_base_vs_fix.png` | G1: desplegable de tipo de línea de la barra de pen, fila 27 (base: *Border*; fix: *Phantom*) |
| `combobox_fix_family.png`, `combobox_fix_end.png` | G1F/G1E: las cuatro variantes Phantom tras Center; Border sigue al final |
| `properties_reopened_base_vs_fix.png` | G3: panel Propiedades tras reabrir la línea dibujada como Phantom |
| `properties_fixtures_base_vs_fix.png` | G4: panel Propiedades con cada fixture de una línea (base: *Continuous* ×3; fix: *Phantom* / *Phantom (small)* / *Phantom (large)*) |
| `canvas_family_base_vs_fix.png` | lienzo con `phantom_family.dxf` (base: cuatro líneas sólidas; fix: patrón largo-corto-corto a tres escalas + ByLayer) |
| `lc_gui_test_phantom.py`, `check_dxf_linetype.py`, `run_lc_master.sh`, `canvas_shot.py` | copia del arnés usado |

## Resultado de los escenarios con comprobación automática

| escenario | base `3f50782a5` | fix `9cf22155d` |
|---|---|---|
| G2 elegir *Phantom*, línea, *Save as* | no existe la entrada; `LINE` guardada ByLayer; sin LTYPE `PHANTOM` | `LINE` con `6/PHANTOM`; LTYPE `PHANTOM` = 31.75/−6.35/6.35/−6.35/6.35/−6.35; 35 registros |
| G5 abrir `phantom_family.dxf`, *Save as* | las tres `LINE` con `6/CONTINUOUS`, capa `PHANTOM_LAYER` = `CONTINUOUS`; los tres registros `PHANTOM*` quedan huérfanos (34 registros) | `PHANTOM` / `PHANTOM2` / `PHANTOMX2`, capa `PHANTOM`; 35 registros, sin duplicados |

## Cómo reproducir

```bash
cd ~/Documents/LibreCAD-fork/tools
export LC_RUNNER=$PWD/run_lc_master.sh LC_CONF_FILE=LibreCAD-2.conf
./venv/bin/python lc_gui_test_phantom.py base   5901 ../test-results/phantom/base
./venv/bin/python lc_gui_test_phantom.py hidden 5902 ../test-results/phantom/fix
./check_dxf_linetype.py ../test-results/phantom/fix/phantom_family_resaved.dxf PHANTOM,PHANTOM2,PHANTOMX2
./venv/bin/python ../test-results/phantom/canvas_shot.py hidden 5902 ../test-results/phantom/fix
```

Trampas del arnés: las de `test-results/hidden/README.md` siguen valiendo (un popup por instancia, fixtures R12, puerto ocupado = instancia huérfana, el lector correcto es el panel Propiedades). Nueva: la captura completa de G4 muestra la línea **seleccionada** (patrón azul de selección), por eso el patrón real se toma aparte con `canvas_shot.py` (`za` + `sx`).
