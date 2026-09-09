# Evidencia — familia de tipo de línea HIDDEN (LibreCAD master)

Commits bajo prueba (worktree `src-hidden`, base `origin/master` = 5822ae887; tests unitarios y carriles CI re-ejecutados ahí; la evidencia GUI se tomó sobre b8da4aaf2, 4 commits antes, sin cambios en el área):
- PR-0 `539058907` — fix del mapeo `HIDDEN2` → `DashLineTiny` (rama `fix/hidden2-linetype-mapping`)
- PR-1 `eb4f40fe8` — familia HIDDEN (rama `feature/hidden-linetype-family`, apilada sobre PR-0)

(Las capturas de G1/G3/G4 se tomaron con la versión previa a la revisión adversarial, `be3842601`; las enmiendas posteriores — `convLTW`, miembro privado, mensajes — no tocan lo que esas capturas miden. G2 y G5 se repitieron con `64ceb6ee9`; tras el rebase a `5822ae887` (→ `eb4f40fe8`) se repitieron los tests unitarios y los dos carriles CI.)

Builds: `src-master-base/build/base` (master sin cambios, Release) y `src-hidden/build/hidden` (RelWithDebInfo, `-DBUILD_TESTS=ON -DLIBRECAD_REGISTER_INTEGRATION_TESTS=ON`). Ambas con `pixi` (Qt 6.8.2 de conda-forge, GCC 13.3), sin sudo.

## Contenido

| ruta | qué es |
|---|---|
| `fixtures/hidden_family.dxf` | DXF R12 (ezdxf) con LTYPE `HIDDEN`/`HIDDEN2`/`HIDDENX2` en mm (acad.lin × 25.4), tres `LINE` con cada uno y una capa `HIDDEN_LAYER` con `HIDDEN` + una línea ByLayer |
| `fixtures/single_<NAME>.dxf` | una sola `LINE` con ese tipo, para el panel de Propiedades |
| `pr0/unit_before_fix.txt`, `pr0/unit_after_fix.txt` | `librecad_dxf_roundtrip_tests "[linetype]"` antes (falla `7 == 8`) y después del fix de PR-0 |
| `pr1/unit_linetype_verbose.txt`, `pr1/ci_lanes.txt` | los tres tests `[linetype]` con `-s`; resumen de `librecad_dxf_fast_tests` y `librecad_dwg_fast_tests` |
| `base/`, `fix/` | una carpeta por build: `results.json`, `run.log`, `shots_<escenario>/` (capturas + `app.log`), `hidden_saved.dxf` (G2), `hidden_family_resaved.dxf` (G5), `conf_<escenario>/` (configuración aislada) |
| `combobox_base_vs_fix.png` | G1: desplegable de tipo de línea de la barra de pen, base vs fix |
| `combobox_fix_family.png` | G1F: las cuatro variantes Hidden visibles |
| `properties_reopened_base_vs_fix.png` | G3: panel Propiedades tras reabrir la línea dibujada como Hidden |
| `properties_fixtures_base_vs_fix.png` | G4: panel Propiedades con cada fixture de una línea |
| `calib_base/` | capturas de calibración del layout de master (barra de pen, `Ctrl+M`) |

## Cómo reproducir

```bash
cd ~/Documents/LibreCAD-fork/tools
export LC_RUNNER=$PWD/run_lc_master.sh LC_CONF_FILE=LibreCAD-2.conf
./venv/bin/python lc_gui_test_hidden.py base   5901 ../test-results/hidden/base
./venv/bin/python lc_gui_test_hidden.py hidden 5902 ../test-results/hidden/fix
./check_dxf_linetype.py ../test-results/hidden/fix/hidden_family_resaved.dxf HIDDEN,HIDDEN2,HIDDENX2
```

Escenarios (uno por instancia de LibreCAD; ver la cabecera de `lc_gui_test_hidden.py`): G1/G1F desplegable (`Alt+↓`, el clic abre el popup pero el servidor `vnc` de Qt no lo pinta en la captura), G2 elegir *Hidden* + línea + *Save as*, G3 reabrir + Propiedades, G4 fixtures de una línea + Propiedades, G5 fixture de familia + *Save as*.

## Trampas del arnés (aprendidas aquí)

- **Un popup por instancia**: tras cerrar un desplegable con Esc, el siguiente popup deja de responder al cliente VNC (`TimeoutError`). `F4` también lo cuelga.
- Una instancia que se queda viva retiene el puerto y el cliente pasa a hablar con ella en silencio: `lcvnc.py` ahora rechaza arrancar si el puerto está ocupado.
- Las fixtures deben ser **R12**: este master (b8da4aaf2) no abre los DXF R2000+ generados por ezdxf 1.4.3 («Cannot open DXF file»), mientras que 2.2.1 sí los abre. Posible regresión del lector de #2790, pendiente de aislar y reportar aparte.
- Los conversores CLI (`dxf2svg`, `dxf2png`, `dxf2dwg`) se quedan colgados con `QT_QPA_PLATFORM=offscreen` en este master; no se han usado.
- El diálogo `attr` (Modify › Attributes) muestra «Unchanged», no el valor actual: el lector correcto es el panel **Propiedades** del dock derecho (pestaña en x≈1790, y≈770 a 1800×1400).
- Los conversores de conda activan su entorno con scripts que fallan bajo `set -u`: `run_lc_master.sh` hace `set +u` alrededor de `pixi shell-hook`.
