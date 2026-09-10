# Plan: Named Linetypes — document-owned LTYPE table, name-preserving pens, custom patterns

> **Status**: Draft for maintainer discussion (2026-09-10). Every file:symbol reference below
> was verified against `origin/master` = `f3aa61ac9` on that date. Anchors are given as
> *file · symbol*, not line numbers: `rs_filterdxfrw.cpp` is ~33k lines and is reformatted often.
> **Motivation**: CAM front-ends for cutting machines (stone/glass bridge saws, water-jet,
> plasma, press brakes, SigmaNEST-style nesting) read the **linetype name** of each DXF entity
> (group code 6) to pick the machining mode. Today LibreCAD keeps only the names of its 8
> built-in families; every other name is rewritten as `CONTINUOUS` on the first save, silently
> (#1738 asks for this since 2024; `.lin` support was called "planned" in #1917).
> **Decisions proposed (not locked — see §2 for the questions put to maintainers)**:
> 1. **The linetype name is the identity**; `RS2::LineType` stays as a derived cache ("nearest
>    built-in") for legacy consumers. Enum values are never renumbered.
> 2. **The document owns its linetype table** (`LC_LineTypeList`, shaped like `RS_LayerList`),
>    seeded with the built-ins; `.lin` files are only *imported into* it.
> 3. **Phase 2 renders custom patterns in device millimetres**, exactly like the built-ins do
>    today; drawing-unit / `$LTSCALE` scaling is an optional later phase (#1476).
> 4. **Names are preserved literally** (`ACAD_ISO09W100` stays `ACAD_ISO09W100`; the enum
>    cache still says `PhantomLine` for icons and legacy paths).
> 5. **A referenced name without a LTYPE record gets a marker record** (`73`=0, `40`=0 — or the
>    metrics of the built-in family the name maps to, for a drawable ISO alias; §4 step 6) so the
>    name survives and the written DXF/DWG stays valid.
> **Builds on**: #2790 (imported LTYPE records archived as metadata), #2815 (HIDDEN),
> #2823 (PHANTOM). **Addresses**: #1738. **Touches**: #1476, #1734, #1959, #1917.
> **Ladder**: 5 PRs (+1 optional), each small, tested, bisect-safe, green on CMake and qmake6.

## What we need from you

This plan makes the **linetype name** the identity of a pen and gives every document its own
LTYPE table, so a name a cutting machine reads (`CUT`, `ACAD_ISO02W100`) survives open → save
instead of being rewritten to `CONTINUOUS` — today `rs_filterdxfrw.cpp` · `nameToLineType`
returns `RS2::SolidLine` for any unknown name and `lineTypeToName` falls back to `CONTINUOUS`.
No product code exists yet, and the single decision that unblocks Phase 1 is Q1 + Q2 below —
**document-owned table, pen = literal name + derived `RS2::LineType` cache** — answered, or a
reasonable silence plus explicit agreement to proceed; the other seven have defaults we follow
unless you say otherwise.

Each line is the question, our proposed answer in bold, and where the reasoning lives; all nine
are argued in **§2 Questions for maintainers (Phase 0 gate)**.

1. Document-owned linetype table (like layers), or global `.lin` lists seeding every drawing (QCAD does both)? → **document table as truth; `.lin` only imported into it**, built-ins seeding every document. §2 Q1
2. Pen identity: name + cached enum (**A**), or a sentinel `RS2::CustomLine` (**B**)? → **A**; B still patches every consumer and loses the name anyway. §2 Q2
3. Units for custom patterns when drawing? → **device mm, identical to the built-ins**, from Phase 2; `$LTSCALE`/drawing-unit mode is an optional Phase 6 (§9, #1476). §2 Q3
4. Aliases (`ACAD_ISO09W100`, `HIDDEN2`…): keep the literal name, or canonicalise on save? → **keep literal**; the enum cache still points at the family for icons and legacy paths. §2 Q4
5. A name referenced by an entity or layer with no LTYPE record in the file? → **create a marker record** (`73`=0, `40`=0; a drawable ISO alias copies its family's metrics) and write it back. §2 Q5
6. Complex linetypes (text/shape segments)? → keep the `DRW_LType` **opaque in metadata, render only the dash list** (segments count as gaps of their length); `.shx` shapes out of scope. §2 Q6
7. Where does the "Linetypes" editor live — a Drawing Preferences tab (like dimension styles) or a dock (like layers)? → **tab first; dock if preferred**; the dialog is Phase 4. §2 Q7
8. Backport to 2.2.1? → **no**; architecture change, 2.2.1 receives fixes only. §2 Q8
9. `dwg-dxf-verify.yml` is path-filtered, so a PR touching only `linetypes/**`, `gui/render/**` or `ui/**` never runs the DXF/DWG test lanes — the Pixi builds and CodeQL still compile it, so it is built but never exercised. Add those paths, or keep the evidence local? → **ask, don't push**: this ladder edits no workflow file; each PR carries §1.1's local gate. §2 Q9

---

## 0. Verified Ground Truth (what the plan builds on)

### 0.1 Where a linetype name is lost today (the only points)

| Site | Behaviour | Evidence |
|---|---|---|
| `RS_FilterDXFRW::nameToLineType(const QString&)` | name → `RS2::LineType` table; **`return RS2::SolidLine` for any unknown name** | `librecad/src/lib/filters/rs_filterdxfrw.cpp` · `nameToLineType` |
| `RS_FilterDXFRW::lineTypeToName(RS2::LineType)` | inverse; `default: return "CONTINUOUS"` | same file · `lineTypeToName` |
| `RS_FilterDXFRW::setEntityAttributes()` | `pen.setLineType(nameToLineType(attrib->lineType))` — entities | same file · `setEntityAttributes` |
| `RS_FilterDXFRW::attributesToPen(const DRW_Layer*)` ← `addLayer()` | same for layers; the raw `DRW_Layer` is archived but `writeLayers()` does not restore its `lineType` | same file · `attributesToPen`, `addLayer` |
| `RS_FilterDXFRW::getEntityAttributes()` / `writeLayers()` | `ent->lineType = lineTypeToName(pen.getLineType())`, `lay.lineType = lineTypeToName(pen.getLineType())` | same file · `getEntityAttributes`, `writeLayers` |

*The remaining three loss points, in detail:*

- **Same table reused** — `rs_filterdxf1.cpp`, `rs_filtershp.cpp`, `lc_dimstyle.cpp` and
  `rs_filterjww.cpp` each reach the same two functions or keep their own copy.
  - Which file does what:
    - `rs_filterdxf1.cpp` (QCad 1.x `FormatDXF1` importer, `canExport()` is false; ×7 live, and the
      8th grep hit is inside the commented-out HATCH block and names the uncompiled
      `RS_FilterDXF`), `rs_filtershp.cpp` (DBF column, ×1), `lc_dimstyle.cpp` (×6, nested classes).
    - `lc_dimstyle.cpp`: the `const QString&` setters `LC_DimStyle::DimensionLine::setLineType` and
      `LC_DimStyle::ExtensionLine::setLineTypeFirst/Second` keep the raw name and only derive the
      enum cache (not a loss point); the `RS2::LineType` overloads overwrite the name with
      `lineTypeToName(enum)` and are what `LC_DlgDimStyleManager`, `LC_PropertiesProviderDimBase`
      and `LC_DimStyleToVariablesMapper` (`$DIMLTYPE`, read as int) call.
    - `rs_filterjww.cpp` has its own copy.
    - Evidence: `git grep -n 'nameToLineType\|lineTypeToName' -- librecad/src`; it also hits the
      uncompiled `rs_filterdxf.{h,cpp}` (absent from `CMakeLists.txt`/`src.pro`),
      `rs_filterjww.{h,cpp}` and `tests/dxf_roundtrip_tests.cpp`.
- **`RS_FilterDXFRW::findLineTypeHandleToWrite(name)`** — the dim-style path writes a handle instead
  of a name and drops the reference silently when it cannot resolve one.
  - Who calls it, what it resolves against, and what DWG does:
    - Callers: `prepareDRWDimStyleDimLine/ExtLine` (`$DIMLTYPE`/`$DIMLTEX1`/`$DIMLTEX2` =
      345/346/347, DXF newer than AC1018 only) and `addDimStyleOverrideToExtendedData` (DIMENSION
      overrides 345/347/348, DXF only: `writeDimension` returns through its DWG branch first).
    - It looks the upper-cased name up in the DXF writer's `lineTypesMap`, which holds only the
      records `dxfRW::writeLineType` has already written (never BYLAYER/BYBLOCK/CONTINUOUS); a miss
      silently omits the group; returns `DRW::NoHandle` whenever `m_dxfW == nullptr`.
    - On DWG nothing sets `DRW_Dimstyle::dimltypeH/dimltex1H/dimltex2H`, which
      `DRW_Dimstyle::encodeDwg` writes for AC1021+, **so a DWG 2007+ save writes null dim-style
      linetype handles today, built-in names included** (pre-existing, not specific to custom
      names).
    - Evidence: `rs_filterdxfrw.cpp` · `findLineTypeHandleToWrite`, `prepareDRWDimStyleDimLine`,
      `prepareDRWDimStyleExtLine`, `addDimStyleOverrideToExtendedData`, `writeDimstyles`,
      `writeDimension`; `libdxfrw.cpp` · `dxfRW::writeLineType`; `drw_objects.cpp` ·
      `DRW_Dimstyle::encodeDwg`.
- **Below the filter everything is already name-based** — libdxfrw writes group 6 verbatim
  (`writeUtf8String(6, ent->lineType)`, upper-cased for R12) and the DWG writer resolves
  `ent->lineType` against `ltypeMap` by upper-cased name.
  - **A name without a record leaves `lTypeH.ref == 0`**, and from there:
    - **`DRW_Entity::encodeDwgCommon` still derives `ltFlags = 3` from the non-reserved name and
      `DRW_Entity::encodeDwgEntHandle` writes a null linetype hard pointer (code 0, ref 0), a
      dangling linetype reference in the written DWG, silently.**
    - **LibreCAD's reader maps it back to ByLayer (`DRW_Entity::parseDwg` → `lineType = ""`, no
      `ltypemap` hit in `dwgReader::parseAttribs`, `nameToLineType("")` → `LineByLayer`).**
    - **Layers degrade explicitly to CONTINUOUS in `dwgWriter15::emitLayerRecord`.**
    - Evidence: `libraries/libdxfrw/src/libdxfrw.cpp`; `intern/dwgwriter15.cpp` ·
      `dwgWriter15::encodeEntity`, `emitLayerRecord`; `intern/dwgwriter24.cpp` ·
      `dwgWriter24::encodeEntity`; `drw_entities.cpp` · `encodeDwgCommon`, `encodeDwgEntHandle`,
      `parseDwg`; `intern/dwgreader.cpp` · `parseAttribs`.

### 0.2 What already exists and is reused

| Item | State | Evidence |
|---|---|---|
| Document plumbing | `RS_Graphic` owns `m_layerList, m_blockList, m_namedViewsList, m_ucsList, m_dimstyleList, m_textStyleList, m_variableDict, m_dwgAdvancedMetadata`; `RS_Document` declares the `get*List()` pure virtuals; `RS_Block` delegates; `initForNewDocument()` seeds | `rs_graphic.h`, `rs_document.h`, `blocks/rs_block.h` |

*The other reused pieces, in detail:*

- **LTYPE record archive (#2790, `abd1f4a8e`)** — every imported `DRW_LType` is already archived on
  the document and re-emitted on save.
  - What it stores, what it writes back, and what it does not cover:
    - `LC_DwgAdvancedMetadata::addLineTypeName(const DRW_LType&)` stores
      `m_lineTypeTableEntries[name]` (exact-case key) and by handle;
      `findLineTypeTableEntryByName()` is case-insensitive (`tableNameEquals`); fed by
      `RS_FilterDXFRW::addLType()`.
    - `writeLTypes()` writes the **35 built-in records** (CONTINUOUS, ByLayer, ByBlock + 8 families
      × 4 scales) through `writeLType()` — an imported record with the same name wins — notes the
      names in `m_builtinLTypeNames` and re-emits every imported non-built-in record once.
    - Cleared on import and on new document; **not** copied by clipboard/paste/`libraryInsert`;
      **not** part of `isModified()`; not undoable.
    - Evidence: `librecad/src/lib/engine/document/lc_dwgadvancedmetadata.h` · `addLineTypeName`,
      `findLineTypeTableEntryByName`, `lineTypeTableEntries`; `rs_filterdxfrw.cpp` · `addLType`,
      `writeLType`, `writeLTypes`; test "DXF unused LTYPE and STYLE application groups survive
      filter round trip" (`dxf_roundtrip_tests.cpp`).
- **`DRW_LType`** — the libdxfrw record already carries a whole dash pattern, but `update()`
  rewrites 73 and 40 and the DXF reader rejects a 73 that disagrees with the number of 49 groups.
  - The record, and the code that rewrites it on every read and write:
    - Fields: `name, desc, flags(70), size(73), length(40), path(49…)`, `alignment` (72),
      `segments` (one `DRW_LTypeSegment` per group-49 element on every version, mirroring `path`;
      groups 74/75/340/44–46/50/9 decorate the current segment), `shapeHandles`, `xrefBlockHandle`,
      private `declaredSize` (the parsed 73), `appData/extData/reactors`.
    - `updateValues(name, desc, size, length, path)` calls `reset()` (clears `segments`) and fills
      `path` only. `update()`: a non-empty `segments` overrides `path` (otherwise `segments` is
      rebuilt from `path`), then 73 = element count and 40 = Σ|49| are recomputed.
    - The DXF reader (`dxfRW::processLType` → `finishLType`) calls `update()` before
      `validateDxf()`: the file's 40 is discarded, and a 73 that disagrees with the number of 49
      groups (`declaredSize != size`) fails with `BAD_CODE_PARSED`, so the file does not open;
      Phase-0 inline fixtures must keep 73 equal to the 49 count.
    - The DXF writer (`dxfRW::writeLineType`) calls `update()` on a copy, allocates a new handle 5
      and writes 49 from `segments`: the marker record falls out of an empty path, 40 is never kept
      verbatim, and code that edits `path` on a copy of an imported record must clear or rebuild
      `segments`.
    - The DWG encoder (`DRW_LType::encodeDwg`) writes the stored `size`/`length` as they are.
    - Evidence: `libraries/libdxfrw/src/drw_objects.h` · `class DRW_LType`, `updateValues`,
      `reset`; `drw_objects.cpp` · `DRW_LType::parseCode`, `update`, `validateDxf`, `encodeDwg`;
      `libdxfrw.cpp` · `dxfRW::processLType`, `writeLineType`.
- **"Name + enum" precedent** — `LC_DimStyle::DimensionLine` keeps `QString DIMLTYPE` and
  `RS2::LineType DIMLTYPE_LineType` side by side (`LC_DimStyle::ExtensionLine`: `DIMLTEX1/2`).
  - How the precedent behaves, and where it stops being a precedent:
    - The `const QString&` setters keep the name; the enum overloads
      (`DimensionLine::setLineType(RS2::LineType)`,
      `ExtensionLine::setLineTypeFirst/Second(RS2::LineType)`) overwrite it with
      `lineTypeToName()`, which is the hazard `RS_Pen` must avoid.
    - The filter writes a **handle**, not the name: `findLineTypeHandleToWrite(name)` resolves the
      upper-cased name against the LTYPE records `dxfRW::writeLineType` has already emitted
      (`lineTypesMap`; BYLAYER/BYBLOCK/CONTINUOUS, written with fixed handles, never enter it).
    - DIMSTYLE record: 345/346/347, only for DXF targets newer than AC1018
      (`prepareDRWDimStyleDimLine/ExtLine`). Per-DIMENSION DSTYLE xdata: 345/347/348
      (`addDimStyleOverrideToExtendedData` ↔ `parseDimStyleOverride`), which disagrees with the
      346/347 of the table writer and of `DRW_Dimstyle`.
    - Unresolved names and the whole DWG path (`m_dxfW == nullptr`) drop the reference silently
      (§0.1).
    - Import is one-sided: `DRW_Dimstyle::parseCode` has no case for 345–347 and nothing maps the
      DWG `dimltypeH/dimltex1H/dimltex2H` to names, so only the DSTYLE xdata path yields a name
      from a file.
    - `RS_Color` carries `QString m_colorName` as passive metadata.
    - Evidence: `lc_dimstyle.h` · `lineTypeName()`, `DIMLTYPE_LineType`; `lc_dimstyle.cpp` ·
      `DimensionLine::setLineType`, `ExtensionLine::setLineTypeFirst/Second`;
      `rs_filterdxfrw.cpp` · `findLineTypeHandleToWrite`, `prepareDRWDimStyleDimLine`,
      `prepareDRWDimStyleExtLine`, `addDimStyleOverrideToExtendedData`, `parseDimStyleOverride`;
      `drw_objects.cpp` · `DRW_Dimstyle::parseCode`.
- **Document table to imitate** — `RS_LayerList`: value member of `RS_Graphic`, `QList<RS_Layer*>` +
  `QSet` mirror, listeners (`RS_LayerListListener`), `m_modified`, destructive mutators `protected`
  + `friend class RS_Graphic`.
  - `find()` is NFC-normalised but **case-sensitive** (linetypes must be case-insensitive: DXF
    semantics); `add()` merges and **deletes** the duplicate (contract not to repeat). Simpler
    name-keyed table: `LC_TextStyleList` (`find`, `addStyle`, `remove`, `replace`, `m_modified`).
    `RS_Graphic::isModified()` ORs layers/blocks/views/UCS/dimstyles/variables — **not** text styles
    nor metadata. Evidence: `librecad/src/lib/engine/document/layers/rs_layerlist.{h,cpp}`,
    `textstyles/lc_textstylelist.h`, `rs_graphic.cpp` · `isModified`.
- **ByLayer/ByBlock resolution** — `RS_Entity::getPenResolved()` copies through
  `setLineTypeFromPen()` and tests `isLineTypeByLayer()/isLineTypeByBlock()`, but five other
  pen-to-pen copies move the enum only.
  - The five enum-only copies, and what they cost:
    - The INSERT expander is the free function `updatePen(RS_Pen, const RS_Pen&)` in
      `rs_insert.cpp`'s anonymous namespace, applied to every member clone and to nested inserts in
      `RS_Insert::update(const RS_InsertExpansionBudget&)`: it tests
      `getLineType() == RS2::LineByBlock` and copies the enum only, with
      `setLineType(blockPen.getLineType())`.
    - `RS_Modification::explode` copies the expanded child's pen verbatim
      (`updateExplodedChildrenRecursively`, `resolvePen = false` for inserts).
    - Four other pen-to-pen copies are enum-only too:
      `RS_Modification::doChangeEntityAttributes/doChangeBlockAttributes` (reached through
      `RS_Modification::changeAttributes`, e.g. by Modify > Attributes and `LC_ActionPenApply`),
      `LC_LayerTreeWidget::copyLayerAttributes` and `QG_WidgetPen::setPen(const RS_Entity*, …)`.
    - Once `setLineType(enum)` derives the canonical name, ByBlock members of a custom-typed INSERT
      draw with the built-in name, and save it after explode, unless all five use
      `setLineTypeFromPen()`.
    - Evidence: `rs_entity.cpp` · `getPenResolved`; `rs_insert.cpp` · `updatePen`,
      `RS_Insert::update`; `rs_modification.cpp` · `explode`,
      `updateExplodedChildrenRecursively`, `doChangeEntityAttributes`, `doChangeBlockAttributes`;
      `lc_layertreewidget.cpp` · `copyLayerAttributes`; `qg_widgetpen.cpp` ·
      `setPen(const RS_Entity*, …)`; `git grep -n 'setLineType(.*getLineType()' -- librecad/src`.
- **`RS_Pen`** — holds `RS2::LineType m_lineType`, `m_width`, `m_screenWidth`, `RS_Color m_color`,
  `m_alpha`, `m_dashOffset`, and `isSameAs()` is the renderer's pen-cache key.
  - Equality, copies and the size of the blast radius:
    - `operator==` compares type/width/colour; its only consumer is
      `matchesSidecarPresentation()` (`rs_filterdxfrw.cpp`), where name-aware equality is the
      wanted semantics.
    - `isSameAs(p, patternOffset)` adds alpha, compares `m_dashOffset` with the painter's current
      offset argument, requires a valid pen (`!FlagInvalid`) and **is the renderer's pen-cache
      key** (`lc_graphicviewrenderer.cpp` ×2, `lc_printpreviewviewrenderer.cpp`,
      `lc_printviewportrenderer.cpp`); `updateBy()` copies everything except `m_screenWidth`. Not
      hashed anywhere.
    - 98 `getLineType(` / 81 `setLineType(` lines in 50 files, counting declarations and the
      same-named members of `QG_LineTypeBox`, `QG_PenToolBar`, `LC_PenItem` and `LC_DimStyle`;
      86 files mention `RS2::LineType` or those calls.
    - Dead: `no_used/` ×6 and `rs_filterdxf.cpp` ×5 (in neither build list), plus
      `rs_python_wrappers.cpp` (in CMake, but its body is `#ifdef RS_OPT_PYTHON`, which is never
      defined).
    - Evidence: `librecad/src/lib/engine/rs_pen.h` · `operator==`, `isSameAs`, `updateBy`;
      `rs_filterdxfrw.cpp` · `matchesSidecarPresentation`.

### 0.3 Rendering

| Item | State | Evidence |
|---|---|---|
| **Null deref** | `getPattern()` returns `nullptr` for an enum without an entry; the painter dereferences it unchecked (reproduced with a stale QSettings value) | `rs_painter.cpp` · `rsToQDashPattern` |
| Other pattern consumers | `LC_MakerCamSVG::svgPathAnyLineType / getLinePattern` reimplement the patterns with their own `switch` on the enum | `librecad/src/lib/generators/makercamsvg/lc_makercamsvg.cpp` |
| Pixel test rig | `TestPainter` (QImage + `LC_GraphicViewport`) in `rs_hatch_tests.cpp` — reusable for dash-pattern pixel tests | `librecad/src/lib/engine/document/entities/tests/rs_hatch_tests.cpp` |

*The three larger rendering facts, in detail:*

- **Single funnel** — every renderer resolves an `RS_Pen` per entity, adjusts it (screen width,
  selection/highlight, dash offset) and calls `RS_Painter::setPen(const RS_Pen&)`.
  - Two file-local helpers, `rsToQtLineType(RS2::LineType)` and
    `rsToQDashPattern(RS2::LineType, screenWidth, dpmm, newDashOffset&)`, turn
    `RS_LineTypePattern::getPattern(enum)->pattern` into a `QPen(Qt::CustomDashLine)`. Evidence:
    `librecad/src/lib/gui/render/rs_painter.cpp` · `setPen`, `rsToQDashPattern`;
    `rs_linetypepattern.cpp` · `getPattern`.
- **Units** — **device millimetres**: `k = dpmm / max(screenWidth, 1)`, each element becoming
  `max(k·|d|, 1)` in QPen dash units; zoom-independent, drawing-unit-independent.
  - What that means on screen, which scale variables are ignored, and how the DXF metrics compare:
    - Those units are pen widths (Qt multiplies the dash pattern by the pen width; width 0 counts
      as 1 px), so on screen `max(|d|·dpmm, max(screenWidth, 1))` px. The smallest element is one
      pen width, from the `std::max(…, 1.)` clamp in `rsToQDashPattern`, not 1 px; `newDashOffset`
      is scaled by the same `k`.
    - `$LTSCALE`, `$PSLTSCALE` and `$CELTSCALE` (R13+ only) round-trip through the header variable
      dict (`addHeader`/`writeHeader`), but no renderer reads them; Phase 6 would read
      `getVariableDouble("$LTSCALE", 1.0)` next to `$DIMSCALE` in
      `updateUnitAndDefaultWidthFactors`.
    - Group 48 is parsed into `DRW_Entity::ltypeScale`, ignored by `setEntityAttributes`, and
      therefore lost on save (libdxfrw writes it only for R13+ when it is not 1.0); the one
      exception is the opaque INSERT/ATTRIB MTEXT sidecar.
    - Built-in DXF record metrics (the 49-values written by `writeLTypes()`, which
      `LC_LineTypeNames` inherits) match the screen patterns of `rs_linetypepattern.cpp` within
      ~6 % for DOT*, DASHED*, HIDDEN*, CENTER/CENTERTINY/CENTERX2, PHANTOM*, BORDERTINY and BORDER2
      (`DASHED` 12.7/−6.35 mm vs 12/−6), but not for DASHDOT* (DASHDOT gap 6.35 vs 5; DASHDOTTINY
      0.9525 vs 2.0), DIVIDE* (DIVIDE 6.35 vs 4.9; DIVIDE2 3.175 vs 1.9), BORDER/BORDERX2 (6.35 vs
      4; 12.7 vs 8) and CENTER2 (long dash 19.05 vs 16). These metrics therefore must not be used
      to draw built-ins.
    - Evidence: `rs_painter.cpp` · `rsToQDashPattern`, `setPen`, `getDpmmCached`;
      `rs_linetypepattern.cpp` · `PATTERN_*`; `rs_filterdxfrw.cpp` · `writeLTypes`, `addHeader`,
      `writeHeader`, `setEntityAttributes`; `lc_graphicviewportrenderer.cpp` ·
      `updateUnitAndDefaultWidthFactors`.
- **Six pen-preparation sites** —
  `LC_GraphicViewRenderer::setPenForEntity / setPenForDraftEntity / setPenForOverlayEntity + setupRefSnapEntityPen`;
  `LC_PrintPreviewViewRenderer::setPenForPrintingEntity`;
  `LC_PrintViewportRenderer::setPenForPrintingEntity` (print, PDF, PNG, SVG).
  - What they share, which ones see a document, and which pens bypass them:
    - All derive from `LC_GraphicViewportRenderer`, which holds `RS_Graphic* m_graphic` and
      `updateGraphicRelatedSettings(RS_Graphic*)` (reads `$DIMSCALE` there) — no document listener
      today.
    - Only four of them resolve a document pen with `getPenResolved()` (`setPenForEntity`,
      `setPenForDraftEntity`, both `setPenForPrintingEntity`); `LC_PrintViewportRenderer` also
      serves library thumbnails (`qg_librarywidget.cpp`).
    - UI-only pens without a document: hard-coded `DashLineTiny` (selection, visual snap) and
      `DotLineTiny`/`DotLine2` (`setupRefSnapEntityPen`) in `LC_GraphicViewRenderer`, plus enums
      read from QSettings that reach `RS_Painter::setPen(const RS_Pen&)` outside the six sites —
      grid (`RS_Grid::loadSettings` → `LC_GridSystem::drawMetaGrid/drawGrid`), snap indicator
      (`RS_Snapper::initFromSettings` → `LC_Crosshair::draw`), selection box (`RS_OverlayBox`,
      `selection_overlay_line_type` / `selection_overlay_inverted_line_type`); these are the
      stale-QSettings null-deref path.
    - `PATTERN_SELECTED` (`RS2::LineSelected`) is unused and `PATTERN_BLOCK_LINE` unreachable
      (`rsToQtLineType` maps ByLayer/ByBlock to `Qt::SolidLine` first). Polylines restart the
      pattern at every vertex.
    - Evidence: `render/lc_graphicviewportrenderer.h`, `render/widget/lc_graphicviewrenderer.cpp`,
      `render/widget/lc_printpreviewviewrenderer.cpp`,
      `render/headless/lc_printviewportrenderer.cpp`; `grid/rs_grid.cpp` · `loadSettings`,
      `grid/lc_gridsystem.cpp`; `rs_snapper.cpp` · `initFromSettings`; `lc_crosshair.cpp` · `draw`;
      `rs_overlaybox.cpp`; `rs_linetypepattern.cpp` · `getPattern`.

### 0.4 UI surfaces

| Item | State | Evidence |
|---|---|---|
| Clipboard / paste / library insert | copy **layers and blocks only** | `lc_copyutils.cpp`, `rs_modification.cpp` · `libraryInsert`, `rs_clipboard.cpp` |
| Build systems | both first-class: every new file goes to the top `CMakeLists.txt` (explicit list) **and** to `librecad/src/src.pro`; tests are CMake-only (`librecad_dxf_fast_tests`, `librecad_dxf_roundtrip_tests`, `librecad_dwg_fast_tests`, `librecad_tests`) | `CMakeLists.txt`, `librecad/src/src.pro`, `.github/workflows/dwg-dxf-verify.yml` |

*The remaining UI surfaces, in detail:*

- **Combo** — `QG_LineTypeBox::init(showByLayer, showUnchanged, showNoPen)` hard-codes the rows,
  `itemData` = enum, icons `:/linetypes/linetypeNN.lci` (SVG 32×12).
  - Fixed indices, a live "unused" setter, and the −1 trap:
    - The special rows (Unchanged/ByLayer/ByBlock) sit at fixed indices relied upon by
      `slotLineTypeChanged`, `LC_PenPaletteWidget` (`currentIndex() > 0`) and `setLayerLineType()`.
    - Despite its "not used currently" comment `setLayerLineType()` is live:
      `QG_PenToolBar::updateByLayer` (from `layerActivated`, `layerEdited`, `setLayerList`) and
      `QG_PenToolBar::setLayerLineType` (Pen Pick, Pen Sync-by-layer, pen palette) call it. It
      repaints the index-0 "By Layer" icon from a 6-case switch (Dash/Dot icons swapped against
      `init()`; Hidden/Center/Border/Phantom and every size variant get the Continuous icon),
      assumes By Layer is index 0, and ends with `slotLineTypeChanged(currentIndex())` →
      `lineTypeChanged` → `penChanged`.
    - `setLineType()` uses `findData(t)`: **on a miss the index is −1 and `itemData(-1).toInt()==0`
      is emitted as `NoPen`** — accepting a dialog would overwrite an unknown type with "No Pen".
    - `showNoPen` is never passed `true` by any caller, so no combo has a No Pen row and every
      `setLineType(RS2::NoPen)` already takes the −1 path today; it reads back as NoPen only
      because `QVariant().toInt()==0==NoPen` (cf. #1734).
    - Evidence: `librecad/src/ui/components/comboboxes/qg_linetypebox.cpp` · `init`, `setLineType`,
      `slotLineTypeChanged`, `setLayerLineType`; `qg_pentoolbar.cpp` · `updateByLayer`,
      `setLayerLineType`.
- **Document-bound instances** — pen toolbar (**one instance for the whole application**, created
  before any document), 8 of the 13 `QG_WidgetPen`, quick-select ×2, dimstyle manager ×3, pen
  palette (`cbType`, initialised once) and property sheet (`LC_PropertyLineTypeCombobox`).
  - The pen toolbar's `setGraphicView` only swaps the layer listener, and it pushes its pen into
    the activated document via `slotPenChanged`. The 13 `QG_WidgetPen` sit in 10 compiled dialogs;
    the 8 document-bound ones are `qg_layerdialog`, `lc_layerdialog_ex` and `qg_dlg_attributes`
    through `setPen(const RS_Pen&, bool, bool, title)`, and `lc_dlg_entityproperties`,
    `lc_dlg_dimension` (`wPenEditor`), `lc_dlg_tolerance`, `qg_dlg_text`, `qg_dlg_mtext` through
    `setPen(const RS_Entity*, const RS_Layer*, title)`; the other 5 are settings-bound (next
    bullet). Evidence: `qg_pentoolbar.cpp`, `qg_widgetpen.cpp`, `lc_penpalettewidget.cpp`,
    `ui/dock_widgets/property_sheet/properties/linetype/lc_property_linetype_combobox{,_view}.{h,cpp}`.
- **Settings-bound instances (no document, must keep the enum)** — 6 `QG_LineTypeBox` in
  `qg_dlgoptionsgeneral.{ui,cpp}` (grid, snap, overlay) **plus 5 `QG_WidgetPen`**.
  - `lc_dlg_options_graphic_view.ui` is an orphan that no source includes and neither build
    compiles, so it is ignored. The five pen widgets are `lc_layertreeoptionsdialog` ×4
    (`RS_Settings::writePen/readPen`) and `lc_quickinfowidgetoptionsdialog` ×1
    (`penHighlightLineType`), through the same `setPen(const RS_Pen&, bool, bool, title)` overload
    as the two layer dialogs. Evidence: `qg_dlgoptionsgeneral.cpp`,
    `lc_layertreeoptionsdialog.cpp`, `lc_quickinfowidgetoptionsdialog.cpp`;
    `git grep -c 'class="QG_WidgetPen"' -- '*.ui'` outside `ui/not_used/`.
- **Registry** — `LC_PenInfoRegistry` singleton, `QMap<RS2::LineType, QIcon/QString>`; names/icons
  for the property sheet (`LC_PropertyLineTypeComboboxView::doDrawValueDetails`), palette model and
  quick info.
  - `hasLineType(int)` validates `.lcpp` entries; the getters are `const`, so an enum outside the
    table yields a null `QIcon` / empty `QString` (blank cell in the property sheet and quick info)
    without inserting anything; `hasLineType()` is unaffected. Evidence:
    `librecad/src/ui/dock_widgets/pen_palette/lc_peninforegistry.cpp`.
- **Integer persistence (stay on the enum)** — `RS_Settings::writePen/readPen` (layer-tree default
  pens, `FIXME` no validation), 11 concrete settings keys, the `.lcpp` palette, `$DIMLTYPE` and the
  plugin API all persist the enum as an int.
  - The four kinds of integer persistence:
    - The keys are 7 literals — `indicator_lines_line_type` (read in `rs_snapper.cpp`),
      `selection_overlay_line_type`, `selection_overlay_inverted_line_type` (`rs_overlaybox.cpp`),
      `metaGridPointsLineType`, `metaGridLinesLineType`, `GridLinesLineType` (`rs_grid.cpp`) —
      these six are edited in `qg_dlgoptionsgeneral.cpp` — plus `penHighlightLineType`
      (`lc_quickinfowidgetoptions.cpp`); and the 4 keys
      `pen{NormalLayer,DimensionalLayer,InfoLayer,AltPosLayer}LineType` generated by the
      `writePen/readPen` above (callers in `lc_layertreemodel_options.cpp`).
    - `.lcpp` palette: `LC_PenPaletteData::toStringRepresentation`, exactly 4 fields, rejects
      unknown ints.
    - `$DIMLTYPE` as int group 70 (`lc_dimstyletovariablesmapper.cpp`; `$DIMLTEX1/2` written with
      the int overload though read as strings).
    - Plugins: `DPI::LineType` in `document_interface.h`, **different numbering** from
      `RS2::LineType`, direct cast in `Doc_plugin_interface::getCurrentLayerProperties`; the string
      bridge `convLTW` (`doc_plugin_interface.cpp`) exchanges LibreCAD enum spellings
      (`"SolidLine"`, `"DashLine"`…), **not DXF names**: `lt2str` returns "BYLAYER" for any enum
      outside its table (every `*Tiny`), `str2lt` returns `LineByLayer` for any unknown string, and
      `BorderLine2/X2` both map to "BorderLine". `Plugin_Entity::getData` → `updateData`
      round-trips it: `plugins/sameprop` copies `DPI::LTYPE` verbatim to other entities through
      `updateData`; `list` and `divide` print it.
- **Dialog plumbing** — `QG_WidgetPen::setPen(...)` receives no document, and the `QG_DialogFactory`
  layer and attribute requests carry no graphic either.
  - Which requests carry a document and which do not:
    - Factory = `QG_DialogFactory` over `RS_DialogFactoryInterface`:
      `requestNewLayerDialog/requestEditLayerDialog(RS_LayerList*)` and
      `requestAttributesDialog(RS_AttributesData&, RS_LayerList&)` carry no graphic.
    - `requestModifyEntityDialog/requestMTextDialog/requestTextDialog(…, LC_GraphicViewport*)` pass
      a viewport and every entity dialog is constructed with it
      (`LC_EntityPropertiesDlg::m_viewport`
      for text, mtext, dimension and tolerance; a constructor argument of
      `LC_DlgEntityProperties`), so the graphic is reachable through
      `LC_GraphicViewport::getGraphic()`.
    - `entity->getGraphic()` is not enough: the `tmp` entity of `LC_ActionDrawText::init` /
      `LC_ActionDrawMText::init` has no parent. `LC_LayerDialogEx` is constructed by
      `LC_LayerTreeWidget` directly, outside the factory.
    - `RS_AttributesData` carries the layer by name **and a full `RS_Pen`**; the enum-only copy is
      in `RS_Modification::doChangeEntityAttributes/doChangeBlockAttributes`
      (`pen.setLineType(data.pen.getLineType())`).
    - Evidence: `librecad/src/ui/dialogs/qg_dialogfactory.{h,cpp}`,
      `librecad/src/lib/gui/rs_dialogfactoryinterface.h`; `lc_entitypropertiesdlg.h` ·
      `m_viewport`; `lc_dlg_entityproperties.h` · `LC_DlgEntityProperties`;
      `lc_action_draw_text.cpp`, `lc_action_draw_mtext.cpp` · `init`; `lc_layertreewidget.cpp`;
      `rs_modification.h` · `RS_AttributesData`.
- **Templates instantiated on the enum** — `LC_PropertySingle<RS2::LineType>`
  (`LC_PropertyLineType`), `LC_TypedPropertyMatchTypeDescriptor<RS2::LineType>`,
  `LC_GenericEntityMatcher<RS2::LineType>` (quick select, `==`), `addLineType_DS/addLineTypeDS` (dim
  styles).
  - Evidence: `lib/selection/metaentity/lc_propertymatchertypes.{h,cpp}` (`TLINE_TYPE`),
    `lib/selection/metaentity/lc_entitymatcher.h` (`LC_GenericEntityMatcher`, instantiated in
    `lc_dlgquickselection.cpp`), `lib/selection/metaentity/entities/lc_matchdescriptor_dimbase.h`
    (`addLineTypeDS`), `ui/dock_widgets/property_sheet/properties/linetype/lc_property_linetype.h`
    (`LC_PropertyLineType`),
    `ui/dock_widgets/property_sheet/metaentity/entities/lc_propertiesprovider_dim_base.{h,cpp}`
    (`addLineType_DS`).
- **Patterns to imitate** — `QG_LayerBox::init(RS_LayerList&, …)` (filled by name, `findText`,
  rebuilt per dialog), `QG_LayerWidget/QG_LayerModel` (listener + `beginResetModel`),
  `LC_PropertyLayer` (carries `RS_LayerList*`).
  - Run-time painted icons: `QG_ColorBox::addColor`, `LC_PenInfoRegistry::getColorIcon`; temporary
    row for a value not in the list: `QG_ColorBox::addTemporaryCustomColor`.
- **Missing entirely** — any "Linetypes" action, menu, dialog or translation.
  - Pattern to create them: `ui/main/init/lc_actionfactory.cpp`
    (`justCreateAction(map,"DimStyles",…)` + `_SetAsCurrentActionInView=false`), a tab in
    `QG_DlgOptionsDrawing` (`changeDrawingOptions(tab)`) or a dock via
    `lc_widgetfactory.cpp createLayerWidget` + `setGraphicView` fan-out in
    `qc_applicationwindow.cpp`.

### 0.5 Upstream and format context

- Upstream issues and what they say:
  - **#1738 "Line types customization"** (open since 2024-02-14, no maintainer comment; u2fly added
    `.lin` samples and the ISO 128 table).
  - **#1917** (sand1024, 2024-10-19): custom linetypes "planned"; simple dash/dot lists are easy
    and fit Qt's pen; complex ones (shapes/text) need "heavyweight painting logic" and `.shx`;
    suggests supporting acad.lin "from the beginning up to the complex types".
  - dxli (forum): "all line types are built-in. We can support reading in custom line type files,
    of course".
  - **#1476**: dashes are fixed on screen and print ("Maybe V3"). **#1734**: `NoPen` saved as
    CONTINUOUS. **#1959**: exploding a block loses ByBlock.
  - **PR #1922** (sand1024): render redesign with a pen cache — pen resolution and
    `QPainter::setPen` are "quite time consuming".
- **DXF**: R12 (what stone-cutting CAMs require) has LTYPE groups 2/70/3/72/73/40/49 only — **no**
  group 48, no `$CELTSCALE`; "the LTYPE table always precedes the LAYER table"; names upper-case;
  "every line type used in the drawing has to have a table entry, or the DXF drawing is invalid for
  AutoCAD"; complex linetypes are R13+. libdxfrw emits BYBLOCK/BYLAYER/CONTINUOUS itself before
  `iface->writeLTypes()` and refuses to write them as user records.
- **QCAD** (GPLv3+; LibreCAD is GPLv2 — **design reference only, no code**): `RLinetype` is a
  document object wrapping `RLinetypePattern`; entities and layers reference it by id; names unique
  case-insensitively (re-add = update); global `.lin` lists seed each new drawing; unknown entity
  name on import → BYLAYER with a warning, on a layer → CONTINUOUS; render scales pattern ×
  `$LTSCALE` × unit × entity scale ÷ view scale with a "screen-based" mode; **no pattern editor**.
- **Machines and CAM** (survey 2026-09-10): most encode machining in **layer names** (Alphacam,
  Biesse, HOMAG woodWOP, Zünd, ProNest, SheetCam, dxf2gcode), some in **colour** (OMAX, LightBurn),
  several in **linetype name** (stone/glass bridge saws; SigmaNEST maps by Layer/Colour/Line Type;
  press brakes use DASHED/CENTER on BEND_UP/BEND_DOWN; Inventor exports numeric linetype codes). A
  neutral CAD must round-trip layers, colours **and linetype names** as-is. Same request exists for
  AutoCAD (LinOut), BricsCAD, QCAD and FreeCAD (#23819).

---

## 1. Architecture Target

```
librecad/src/lib/engine/document/linetypes/                 ← NEW (Phase 1)
├── lc_linetype.h / .cpp              LC_LineType: name (key, case-insensitive NFC), description,
│                                     pattern (std::vector<double>: +dash, −gap, 0 = dot),
│                                     patternLength (group 40), flags (70), builtin, hasImportedRecord,
│                                     legacyType (enum cache); clone(), fromDrw()/toDrw(),
│                                     isByLayer()/isByBlock(), key() (NFC + ASCII-only upper-case fold);
│                                     static isValidName(name, QString* reason), added by Phase 4 (§7)
├── lc_linetypelist.h / .cpp          LC_LineTypeList: the document table (RS_LayerList shape; count/at/
│                                     begin/end; find() is case-insensitive; add() returns the survivor,
│                                     never deletes its argument, nullptr for a UI or .lin name that
│                                     isValidName() refuses (§7); edit(); seedBuiltins() from the ctor;
│                                     listeners; m_modified with isModified()/setModified(); remove/clear/
│                                     rename protected, friend RS_Graphic; unsigned revision(), bumped by
│                                     every mutator, invalidates the Phase-2 pattern cache (§5);
│                                     importReferenced(entities, source) for the Phase-5 copy paths (§8)).
│                                     Declares LC_LineTypeListListener { virtual void lineTypeListModified(bool) {} }
│                                     in this header, as lc_viewslist.h / lc_ucslist.h declare theirs — no separate
│                                     listener header, and not the eight-method RS_LayerListListener shape
├── lc_linetypenames.h / .cpp         built-in metrics table (name, description, 73, 40, 49…, enum) +
│                                     nameToLineType()/lineTypeToName() — moved OUT of the DXF filter so
│                                     lc_dimstyle.cpp / rs_filterjww.cpp / rs_filtershp.cpp stop depending on it;
│                                     plain static class or namespace (LC_DimArrowRegistry is the shape precedent,
│                                     minus its QObject base)
└── lc_linfile.h / .cpp               Phase 4: .lin reader/writer, beside the table it feeds — fonts/rs_font.cpp
                                      and patterns/rs_pattern.cpp parse their own definition files the same way;
                                      lib/fileio/ holds only RS_FileIO and LC_FileNameSelectionService

librecad/src/lib/engine/rs_pen.h                              ← + QString m_lineTypeName (authoritative);
                                                                 m_lineType stays as cache; ==, isSameAs, updateBy,
                                                                 setLineTypeFromPen carry the name
librecad/src/lib/engine/document/rs_graphic.{h,cpp}           ← + m_lineTypeList, wrappers, seeding in
                                                                 initForNewDocument(), isModified() includes it
librecad/src/lib/engine/document/rs_document.h                ← + virtual LC_LineTypeList* getLineTypeList() = 0
librecad/src/lib/engine/document/blocks/rs_block.{h,cpp}      ← delegates to the parent graphic

librecad/src/lib/filters/rs_filterdxfrw.cpp                   ← Phase 1: addLType() → table; set/getEntityAttributes(),
                                                                 attributesToPen(), writeLayers() by name; writeLTypes()
                                                                 iterates the table + synthesises missing records
librecad/src/lib/filters/rs_filterdxf1.cpp, rs_filtershp.cpp  ← Phase 1: same rule (keep the raw name)
librecad/src/lib/filters/rs_filterjww.cpp                     ← Phase 5: nearest built-in (JWW has no names)

librecad/src/lib/gui/render/rs_painter.cpp                    ← Phase 2: pattern-driven QPen; never dereferences nullptr
librecad/src/lib/gui/render/**/lc_*renderer.cpp               ← Phase 2: resolve name → pattern before the painter
librecad/src/lib/generators/makercamsvg/lc_makercamsvg.cpp    ← Phase 2: same pattern source

librecad/src/ui/components/comboboxes/qg_linetypebox.{h,cpp}  ← Phase 3: list-driven, itemData = name, never −1
librecad/src/ui/dock_widgets/pen_palette/lc_peninforegistry.* ← Phase 3: name path + run-time painted icons
librecad/src/ui/... (QG_WidgetPen, dialog factory, property sheet, quick select) ← Phase 3

librecad/src/ui/dialogs/settings/linetypes/                   ← Phase 4: NEW "Linetypes" dialog, sibling of
  lc_dlglinetypes.{h,cpp,ui} (+ list model)                      settings/dimstyles/ (Q7): list, new/edit/delete,
                                                                 import/export .lin, preview; appended as the LAST
                                                                 page of Drawing Preferences (see Placement below)
librecad/src/actions/... + lc_actionfactory.cpp + menu          ← Phase 4: action "LineTypes"

librecad/src/lib/filters/tests/dxf_roundtrip_tests.cpp        ← Phase 1 only: T1–T6, T8, T9, T12–T14, tag
                                                                 [linetype][named] (T6 extends the existing
                                                                 "Every DXF linetype name … maps back" case).
                                                                 Phase 2 only rewords the comment above its
                                                                 RS_LineTypePattern::getPattern() loop (T-R4);
                                                                 Phase 5 adds nothing here — the RS_FilterJWW
                                                                 name-table loop in it stays the oracle
librecad/src/lib/filters/tests/i18n_caret_nfc_tests.cpp       ← Phase 1: T13's NFC find() case
librecad/src/lib/filters/tests/shp_import_filter_tests.cpp    ← Phase 1: T15 (SHP DBF LTYPE column)
librecad/src/lib/modification/tests/                          ← Phase 1: T10 beside rs_modification_trim_tests.cpp
                                                                 (T11's QG_WidgetPen case is a librecad_tests TU
                                                                 too); Phase 5's [linetype][parity] cases sit
                                                                 beside the path each one covers
librecad/src/lib/engine/document/linetypes/tests/             ← NEW: lc_linetypelist_tests.cpp (Phase 1, T7: list
                                                                 semantics, pen equality) + lc_linfile_tests.cpp
                                                                 (Phase 4, .lin parser)
librecad/src/lib/gui/render/tests/                            ← NEW: lc_linetype_render_tests.cpp (Phase 2,
                                                                 [linetype][render]; T-R1…T-R8)
librecad/src/ui/components/comboboxes/tests/                  ← NEW: qg_linetypebox_tests.cpp (Phase 3,
                                                                 [linetype][ui])
```

**Naming**: new classes take the `LC_` prefix — the primary "Classes and structs" policy in
`code_style/LibreCAD CLion c++ codestyle.xml` (`Prefix="LC_"`, with `RS_`/`QG_` only as extra rules) and
the prefix of every document-model class added since 2024 (`LC_ViewList`, `LC_UCSList`, `LC_DimStylesList`,
`LC_TextStyleList`, `LC_DwgAdvancedMetadata`). `RS_LayerList` is the **shape** reference, not the naming
reference; existing classes (`RS_Pen`, `RS_Graphic`, `RS_Document`, `RS_Block`) keep their names.

**Placement**: definition-file parsers live beside the type they produce (`fonts/rs_font.cpp` reads
`.lff`/`.cxf`, `patterns/rs_pattern.cpp` loads pattern DXF), so `lc_linfile` belongs in `linetypes/`, not
in `lib/fileio/` (filter registry only), and its tests sit beside it. Editors for a document table live
under `ui/dialogs/settings/` (`settings/dimstyles/dimstyle_manager/`), and the new page is **appended
last** in `qg_dlgoptionsdrawing.ui`: `QG_DlgOptionsDrawing` drops the legacy page with `removeTab(3)` in
its ctor, and `changeDrawingOptions(3)` (`lc_actionfactory.cpp`, `lc_propertiesprovider_dim_base.cpp`)
plus `RS_ActionOptionsDrawing(ctx, tabIndex)` address pages by runtime index — inserting anything before
`tabDims` silently retargets them. Appended after `tabVars` = `.ui` index 10, runtime index 9.

**Data flow after this plan**: DXF/DWG `LTYPE` record → `RS_FilterDXFRW::addLType` → `LC_LineTypeList`
(+ raw `DRW_LType` kept in metadata for sidecars) · entity/layer group 6 → `RS_Pen::m_lineTypeName`
(enum cache = nearest built-in) · render: pen name → `LC_LineTypeList::find` → pattern → `RS_Painter`
· save: `writeLTypes()` walks the table and every name referenced by entities/layers/dim styles, then
entities write `pen.getLineTypeName()` verbatim.

**Why a document table rather than global `.lin` lists** (recap): the DXF/DWG file *is* the
contract with the machine — its LTYPE table must contain exactly the names the entities use; a
drawing (or a template) that carries its own table needs no per-machine installation. QCAD does
both (global lists seed the document); here the seed is the built-in set and `.lin` import is the
way to add more. The in-tree precedent for shipped definition files is the hatch-pattern / font model
(`RS_PatternList` over `support/patterns/*.dxf` + user dir, enumerated by `RS_System::getPatternList()`);
§2 Q1 proposes the same catalogue as a *source* of definitions, with one difference: a hatch is referenced
in the DXF by name only, while an LTYPE **definition** must travel inside the file — so the document table
stays the truth and the catalogue only feeds it. Open to maintainer preference (§2 Q1).

### 1.1 Dual Build-System Policy (qmake6 + CMake) — hard requirement

Both build systems are first-class and must stay green at every commit of the ladder. Any change
that adds, renames, moves or deletes a source file **touches `CMakeLists.txt` and
`librecad/src/src.pro` in the same commit**. Includes are directory-less (`#include "rs_layerlist.h"`),
so a new **product** directory also goes into `SHARED_INCLUDES` (CMakeLists.txt, consumed by
`librecad_lib`, `librecad` and every test target) and into `INCLUDEPATH` (`librecad/src/src.pro`) —
four lists per new product directory, not two. CMake lists no `.ui` file at all (`CMAKE_AUTOUIC ON`
finds it beside its source); qmake needs it in `FORMS`. **Tests are CMake-only, and a new test file is
one line, not four**: `src.pro` lists no Catch2 unit (its `test/lc_simpletests.{h,cpp}` is the
`LC_DEBUGGING` menu helper), `SHARED_SOURCES` is "All from SOURCES except main, consoles, and tests",
and `SHARED_INCLUDES` lists **no** `tests` directory — a test TU reaches product headers through the
product entries and its own directory through the quoted-include rule. A new `…/tests/` directory
therefore goes into no list; only the `.cpp` is added, by repo-root-relative path, to the explicit
source list of each target that must compile it (there is no glob, no per-directory `CMakeLists.txt`).
`librecad_tests` (`qt_add_executable`, `${MAIN_SOURCES}`, `CMAKE_AUTOMOC/AUTOUIC ON`, unlike the
`AUTOMOC OFF` micro lanes) is the default home and needs no `add_test`: the binary is registered once,
under `LIBRECAD_REGISTER_FULL_TESTS`, and runs every `TEST_CASE` minus `${LIBRECAD_FAST_TEST_EXCLUDES}`
(`phase_c_required_tags` pins only `[block-insert]` and `[wipeout-native-frame]`, so a new tag adds
nothing there). Only `librecad/src/lib/filters/tests/*.cpp` listed in `LIBRECAD_FAST_TEST_SOURCES` /
`LIBRECAD_DXF_FAST_TEST_SOURCES` reach the upstream MSVC lanes, and `LIBRECAD_FAST_TEST_SOURCES` also
generates the `librecad_<stem>` micro target the gate below uses
(`add_librecad_filter_micro_test` foreach → `librecad_dxf_roundtrip_tests`).

**What upstream CI actually runs**: only `.github/workflows/dwg-dxf-verify.yml` executes tests.
It configures with `-DLIBRECAD_REGISTER_FULL_TESTS=OFF`, builds `librecad_dwg_fast_tests` and
`librecad_dxf_fast_tests` on ubuntu-24.04, windows-2022 (MSVC x64) and windows-11-arm (MSVC ARM64), and
is path-filtered to `.github/workflows/dwg-dxf-verify.yml`, `CMakeLists.txt`, `libraries/libdxfrw/**`,
`librecad/src/lib/filters/**` and `scripts/dwg-fast-check.sh`. `librecad_tests` is `EXCLUDE_FROM_ALL`
and CTest-registered only under `LIBRECAD_REGISTER_FULL_TESTS` (default `OFF`), so no workflow builds
it; `run_pixi.yml` (5 platforms) runs `pixi run build` without `BUILD_TESTS`, `codeql.yml` is a `qmake6`
compile, `build-all.yml` packages master. Consequences: (a) PR-2 and PR-3 touch no filtered path and
trigger no test workflow at all, so the local gate below **is** the gate and each PR description carries
its output; (b) a test that must also run upstream and on MSVC belongs in a
`librecad/src/lib/filters/tests/*.cpp` unit listed in `LIBRECAD_DXF_FAST_TEST_SOURCES`
(`dxf_roundtrip_tests.cpp` is one; its anonymous-namespace `ensureSettings()` cannot be reused from
another translation unit); (c) this ladder edits no workflow file — see §2 Q9.

**Canonical per-commit verification pair**:
```bash
# CMake (tests live here) — configure once, with both test-registration options ON
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTS=ON \
      -DLIBRECAD_REGISTER_FULL_TESTS=ON -DLIBRECAD_REGISTER_INTEGRATION_TESTS=ON
cmake --build build --target install librecad_dxf_fast_tests librecad_dxf_roundtrip_tests \
      librecad_dwg_fast_tests librecad_dwg_write_fast_tests librecad_tests
# focused, while iterating
QT_QPA_PLATFORM=offscreen build/librecad_dxf_roundtrip_tests "[linetype]" -s
QT_QPA_PLATFORM=offscreen build/librecad_tests "[linetype]"
# gate, before every commit: the two dwg-dxf-verify.yml lanes + the FULL librecad_tests, all with
# ${LIBRECAD_FAST_TEST_EXCLUDES} ("~[.]" "~[.slow]" "~[slow]" "~[corpus]" "~[external]"
# "~[external-reader]" "~[fixture]"), plus the DWG write lane, which CTest does not register
ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen build/librecad_dwg_write_fast_tests "~[.]" "~[.slow]" "~[slow]" \
      "~[corpus]" "~[external]" "~[external-reader]" "~[fixture]" --reporter compact
git diff --check

# qmake6 (compile+link gate)
qmake6 librecad.pro && make -j12
```

Record the pass/fail set of this gate on `origin/master` **before** Phase 1 and require the same set
after every phase, minus intended changes. `librecad_dwg_write_fast_tests` is part of the gate because
`writeLTypes()` feeds DWG export and those cases are tagged `[dwg-write]`, not `[linetype]`; a
`"[linetype]"` selector alone would not run them.

**Per-phase touchpoint matrix**:

| Phase | CMake touchpoints | qmake6 touchpoints |
|---|---|---|
| 1 | new `linetypes/` include dir + three `.cpp`/`.h` pairs into `librecad_lib`, plus the new test units | new `INCLUDEPATH` + `HEADERS`/`SOURCES` in `src.pro` |
| 2 | one new test unit only (no product file added) | none (tests are CMake-only) |
| 3 | one new test unit only (no product file added) | none (tests are CMake-only) |
| 4 | `lc_linfile` + the dialog's include dir, sources and test unit | `INCLUDEPATH` + `HEADERS`/`SOURCES`/`FORMS` in `src.pro` |
| 5 | none | none |

*What each cell means, phase by phase:*

- **Phase 1 — CMake.** `librecad/src/lib/engine/document/linetypes` into `SHARED_INCLUDES` (beside
  `…/document/layers`); the `lc_linetype*`, `lc_linetypelist*`, `lc_linetypenames` `.cpp`/`.h` pairs
  into `SHARED_SOURCES` (feeds `librecad_lib`); `linetypes/tests/lc_linetypelist_tests.cpp` — plus
  any new `librecad_tests` TU of T10 and T11 — as a repo-root-relative path in the
  `qt_add_executable(librecad_tests …)` source list; the new `tests/` directory itself goes into no
  list.
  - **qmake6**: `INCLUDEPATH += lib/engine/document/linetypes` (beside `lib/engine/document/layers`)
    plus `HEADERS`/`SOURCES` in `src.pro`; the test file is not listed.
- **Phase 2 — CMake.** No product file added (edits to registered files); the new test unit
  `librecad/src/lib/gui/render/tests/lc_linetype_render_tests.cpp` into the
  `qt_add_executable(librecad_tests …)` source list only (§5 step 6) — `lib/gui/render` is already in
  `SHARED_INCLUDES`, the `tests/` directory needs no entry, and staying out of
  `LIBRECAD_FAST_TEST_SOURCES` means no micro target and no upstream lane.
- **Phase 3 — CMake.** No product file added; the new test unit
  `librecad/src/ui/components/comboboxes/tests/qg_linetypebox_tests.cpp` into the
  `qt_add_executable(librecad_tests …)` source list only (§6 step 9) — `ui/components/comboboxes` is
  already in `SHARED_INCLUDES`; a widget test can only live in `librecad_tests`, the one target that
  keeps `AUTOMOC`/`AUTOUIC` on.
- **Phase 4 — CMake.** `linetypes/lc_linfile.cpp/.h` into `SHARED_SOURCES` (its directory was added
  in Phase 1); `librecad/src/ui/dialogs/settings/linetypes` into `SHARED_INCLUDES` +
  `lc_dlglinetypes.cpp/.h` into `SHARED_SOURCES`; no `.ui` entry (CMake lists none; `CMAKE_AUTOUIC`
  finds it beside the source); `linetypes/tests/lc_linfile_tests.cpp` into the
  `qt_add_executable(librecad_tests …)` source list.
  - **qmake6**: `INCLUDEPATH += ui/dialogs/settings/linetypes` plus `HEADERS`/`SOURCES`/`FORMS` (the
    `.ui`) in `src.pro`; `TRANSLATIONS` untouched (strings picked up by `lupdate`); the test file is
    not listed.

**Full-rebuild warning**: `rs_pen.h` and `rs.h` are included by ~820 translation units; Phase 1
recompiles everything once (~40 min at `-j12` on the reference machine). Later phases are incremental.

---

## 2. Questions for maintainers (Phase 0 gate)

Posted on #1738 with a link to this document. No code lands in Phase 1 without an answer or a
reasonable silence plus explicit agreement to proceed.

| # | Question | Proposed answer | Who decides |
|---|---|---|---|
| 1 | Document-owned linetype table (like layers) or global `.lin` lists that seed every drawing (QCAD does both)? | **Document table as truth; `.lin` only imported into it**; the built-in set seeds every document | Maintainers — blocks Phase 1 |
| 2 | Pen identity: name + cached enum (**A**) or a sentinel enum value `RS2::CustomLine` (**B**)? | **A** | Maintainers — blocks Phase 1 |
| 3 | Units for custom patterns when drawing | **Device mm, identical to the built-ins (Phase 2, the phase that starts drawing them)**; `$LTSCALE`/drawing-unit/group-48 mode as an optional Phase 6 | Maintainers; default applies on silence |
| 4 | Aliases (`ACAD_ISO09W100`, `HIDDEN2`…): keep the literal name or canonicalise on save? | **Keep literal** (enum cache still points at the family for icons/legacy) | Maintainers; default applies on silence |
| 5 | Name referenced by an entity/layer but no LTYPE record in the file | **Create a marker record** (`73`=0, `40`=0 like CONTINUOUS; a drawable built-in alias copies its family's metrics, §4 step 6) and write it back | Maintainers; default applies on silence |
| 6 | Complex linetypes (text/shape segments) | Keep the `DRW_LType` opaque in metadata, render only the dash list (segments count as gaps of their length) | Maintainers; default applies on silence |
| 7 | Where should the "Linetypes" editor live: a tab in Drawing Preferences (like dimension styles) or a dock (like layers)? | Tab first; dock if preferred | Maintainers; default applies on silence |
| 8 | Backport to 2.2.1? | **No** | Maintainers; default applies on silence |
| 9 | `dwg-dxf-verify.yml` is path-filtered: a PR touching only `linetypes/**`, `gui/render/**` or `ui/**` is still compiled (Pixi builds, CodeQL) but never runs the DXF/DWG test lanes — add those paths or keep evidence local? | **Ask, don't push**: this ladder edits no workflow file; each PR carries §1.1's local gate | Maintainers — workflow edits are theirs |

Why each answer (the full reasoning the table indexes):

- **Q1 — Document table vs global `.lin` lists.** The file is the contract with the machine;
  templates carry the table; no per-install state. Unlike a hatch (the DXF carries only the pattern
  name, group 2) an LTYPE **definition** must travel in the file, so the document table stays the
  truth and the catalogue only feeds it.
  - The answer in full: the built-in set seeds every document (one ordered list in the UI: special
    rows, built-ins, custom).
  - Catalogue precedent already in the tree: hatch patterns (`RS_PatternList`,
    `support/patterns/*.dxf` + user dir via `RS_System::getPatternList()` → `getFileList()`) and
    fonts (`RS_FontList`).
  - Linetypes reuse that shape for *sourcing* definitions — `support/linetypes/*.lin` + user dir,
    enumerated by a new `RS_System::getLineTypeFileList()` — and the Phase 4 dialog imports from
    the catalogue or from any file.
- **Q2 — Name + cached enum (A) vs a `RS2::CustomLine` sentinel (B).** B still needs every consumer
  patched (`getPattern()` → nullptr, `findData()` → −1, `hasLineType()` false, `DPI::LineType` cast
  out of range, `readPen`) and loses the name anyway.
- **Q3 — Units for custom patterns.** Zero new semantics, consistent look, nothing changes for the
  built-in families; the AutoCAD mode changes how every existing drawing looks and deserves its own
  discussion (#1476). **Consequence stated up front**: group-49 values are drawn as device
  millimetres regardless of `$INSUNITS`, `$LTSCALE` or group 48 (`rs_painter.cpp` ·
  `rsToQDashPattern`: `k = dpmm / max(screenWidth,1)`, no zoom term).
  - (a) A record with 120 mm elements (`ACAD_ISO02W100` in the local acceptance file) draws 120
    device-mm dashes at every zoom — ~450 px at 96 dpi, so shorter entities look solid — and
    120 mm on paper.
  - (b) Inch-valued custom records (element size ≲ 0.5, e.g. a `DASHED` redefined in an imperial
    file) fall under the Phase 2 period threshold and render solid until Phase 6.
  - (c) Drawings using an ISO alias name that *has* a record change look, since today the name is
    drawn as its mapped family.
  - Canonical built-in names keep their static built-in pattern even when the file carries a record
    for them; that record is preserved for export only. Accepted for Phases 1–5 because the machine
    reads names.
- **Q4 — Aliases: literal or canonicalised.** Machines compare names; canonicalising is the same
  silent rewrite this plan removes. The current ISO09 → PHANTOM save test is inverted.
- **Q5 — Referenced name with no LTYPE record.** R12 allows files without a table; libdxfrw only
  publishes records at ENDTAB; AutoCAD rejects unregistered names; the DWG writer silently drops
  them.
  - The marker record in full: pattern empty, `73`=0, `40`=0 like CONTINUOUS — except a name that
    maps to a drawable built-in family, e.g. `ACAD_ISO02W100`, which copies that family's
    `73`/`40`/`49…` so the entity still renders dashed (§4 step 6).
- **Q6 — Complex linetypes.** sand1024 #1917; shapes need `.shx` — out of scope.
- **Q7 — Where the editor lives.** Smallest UI surface; the dialog is Phase 4.
- **Q8 — Backport to 2.2.1.** Architecture change; 2.2.1 receives fixes only.
- **Q9 — Path-filtered test workflow.** Workflow edits are a maintainer call; without them PR-2 and
  PR-3 have compilation as their only upstream gate.
  - The paths the filter does not list: `lib/engine/document/linetypes/**`, `lib/gui/render/**`
    and `ui/**`. What such a PR does still get: `run_pixi.yml` (no path filter, 5 OS) and
    `codeql.yml` — compilation and static analysis, no behavioural test.
  - Each PR description carries the full local gate of §1.1, and the fork runs `dwg-dxf-verify.yml`
    via `workflow_dispatch` for the two MSVC lanes.

Decided on our side (documented, easy to flip):

- **Document switch never mutates a document.** `QG_PenToolBar` is one application-wide instance
  (`std::unique_ptr<RS_Pen> m_currentPen`); `QG_PenToolBar::setGraphicView()` only rebinds the layer
  list, and `QC_ApplicationWindow::slotWindowActivated` → `QC_MDIWindow::slotPenChanged` →
  `RS_Document::setActivePen` only stores the active pen, which `RS_Graphic::isModified()` does not
  consider. Neither may write to the table: Phase 1 ORs `LC_LineTypeList::isModified()` into
  `RS_Graphic::isModified()`, so importing there would make merely clicking a window (or opening a file,
  which ends in the same slot) prompt "save changes?" on close and wake `autoSaveGraphic()`.
- **A name missing in the target is shown, not imported**: the temporary row of Phase 3 step 1,
  "NAME (not defined in this drawing)". `QG_PenToolBar` and `LC_PenItem` keep an `LC_LineType` snapshot
  (name, description, pattern) taken when the user picked that row, so no import ever depends on the
  source document still being open.
- **Lazy import on first use**: the record enters the table when an entity actually carries the name —
  `LC_UndoSection::setupAndUndoableAdd` (where `graphic->getActivePen()` is applied) and
  `RS_Modification::changeAttributes` — through `RS_Graphic::ensureLineType(const LC_LineType&)` called
  inside the undo batch, so the *edit* marks the document modified, not the activation. Collision
  (the target already has the name, case-insensitively): the target record wins, no write. With no
  snapshot available (a palette pen restored from file carries a name only) the Q5 marker rule applies
  at save.
- **Palette file**: `penpalette.lcpp` lines gain an optional 5th field (linetype name), written **only**
  when the name is not `lineTypeToName(cached enum)`; the parser accepts 4 or 5 fields. Older builds skip
  a 5-field line silently (`LC_PenPaletteData::fromStringRepresentation` requires exactly 4, `loadItems`
  drops the `nullptr`, and a later `saveItems` persists the loss), so a palette holding only built-in
  types stays byte-identical and fully interoperable, and only custom-type pens are invisible to 2.2.1 —
  stated in the PR.

---

## 3. Phase 0 — Plan, fixtures, red tests (no product code)

**Goal**: agree the architecture before touching `rs_pen.h`; lock the acceptance criteria as
failing tests so Phase 1 is measured, not argued.

### Steps
1. This document, on a branch of the fork; comment on #1738 with the link and §2.
2. **Neutral upstream fixture**, two inline strings in
   `librecad/src/lib/filters/tests/dxf_roundtrip_tests.cpp` (as the HIDDEN/PHANTOM tests do):
   - **(a) R12 (AC1009)** — T1–T5, T9, T13: a LTYPE table of invented names and simple
     patterns (`VENDOR_TAB` `[20,-20]`, `VENDOR_UTL` `[20,-20,2,-20]`, `VENDOR_STOP` `[2,-10]`,
     `Vendor_mixedCase` `[20,-20]`, `VENDOR_NEG` `[-20,-20]`, `VENDOR_ODD` `[10,-5,0]`,
     `VENDOR_ZERO` `[0,0]`, `Ölfarbe` `[20,-20]`), a `HIDDEN` record with foreign metrics
     `[64,-32]` and a name-only `DASHED` record (`2 DASHED / 70 0`, the shape the shipped
     template `support/library/templates/empty.dxf` carries); one layer on `VENDOR_TAB`, one
     entity per name (`6/ÖLFARBE` for the non-ASCII record), one entity referencing
     `VENDOR_NOREC` **without** a record, one entity whose group 6 is **empty**,
     `ACAD_ISO09W100` and `ACAD_ISO02W100` on entities (no record for either),
     `LAYER L_NOREC` with `6 VENDOR_LAYER_NOREC` (no record), and `BLOCK B` holding a `LINE`
     with `6 VENDOR_BLK_NOREC` (no record) plus an `INSERT` of it.
   - **(b) AC1015** — T8 and T12: every table record carries a `5` handle; a `VENDOR_CPLX` LTYPE
     with a text segment (`74`=2, `75`, `340` = the handle of a `STYLE` record present in the
     same fixture, `46/50/44/45`, `9 GAS`) and a `102 {APP … 310 … 102 }` group; a `VENDOR_TAB`
     record and a `DIMSTYLE` whose `345` points at its handle; one entity on `VENDOR_CPLX`.
     Complex segments and app data are R13+ groups and a record there needs a non-zero handle
     (`dxfTableEntryComplete`), so this case does not belong in the R12 string.
3. **Local acceptance fixture** (not upstream): the vendor "DXF drawing rules" file (R12, 46
   LTYPE records with absolute-mm patterns, `$LTSCALE`=1; entities use `HIDDEN`, `DASHEDM`,
   `DOTTED`, `POINTLINE`, `CONTINUOUS` and ByLayer) round-tripped through `RS_FilterDXFRW`
   **exporting `RS2::FormatDXFRW12`** (the machine format; a second run exports
   `RS2::FormatDXFRW`) — expected: equal group-6 values on every entity and layer compared
   **case-insensitively** (R12 output goes through `dxfWriter::writeUtf8Caps`, which upper-cases
   ASCII bytes), and each of the 46 source records present once with its 49-values. The record
   comparison is a **subset** check over the source's names: LibreCAD always writes its own 35
   built-ins with its own descriptions, and `DRW_LType::update()` recomputes group 40 as Σ|49| on
   read and on write (the vendor records are consistent, so 40 survives). Lives in the fork's
   `test-results/named-linetypes/fixtures/` (its real filename is recorded in that directory's
   `README.md`; this document stays vendor-free) with a reference dump — that dump lists 48 LTYPE
   lines because ezdxf adds synthetic `ByBlock`/`ByLayer` entries.
4. **Red tests T1–T15**, one line each in the table; their assertions, sub-cases and fixture strings
   are unchanged, in the collapsed block below it. Default home is `dxf_roundtrip_tests.cpp` tagged
   `[linetype][named]` — that TU is compiled into `librecad_tests`, `librecad_fast_tests`,
   `librecad_dxf_fast_tests` and the micro target `librecad_dxf_roundtrip_tests` (CMakeLists.txt ·
   `LIBRECAD_FAST_TEST_SOURCES`, `LIBRECAD_DXF_FAST_TEST_SOURCES`), so those cases also run in the
   upstream `dwg-dxf-verify.yml` lanes — that is what "dxf round-trip" means in the Home column, and
   any other home is named in its row. All of them reuse the existing helpers `ensureSettings()`,
   `tmpFile()`, `writeText()`, `recordGroupValues(out,"LINE","6")`,
   `namedRecordGroupValues(out,"LAYER",name,"6")` and `ltypeRecordGroupValues(out,name,"49")`.

   | # | What it proves | Home |
   |---|---|---|
   | T1 | An imported custom name reaches the pen, the enum cache and the list pattern | dxf round-trip |
   | T2 | Export, re-import and re-export are stable: one record, group 6 on entity and layer | dxf round-trip · both DXF versions |
   | T3 | A name with no record survives as an empty marker on entity, layer and block member (Q5) | dxf round-trip (+ DWG under `DWGSUPPORT`) |
   | T4 | Any spelling of one name folds to a single list entry and a single written record | dxf round-trip · both DXF versions |
   | T5 | An ISO alias keeps its literal name instead of being written back as `PHANTOM` | dxf round-trip |
   | T6 | Every `RS2::LineType` still round-trips through `lineTypeToName`/`nameToLineType` | dxf round-trip · extends the existing case |
   | T7 | The name takes part in pen equality and copying, and a fresh list is 35 unmodified entries | `librecad_tests` · new `lc_linetypelist_tests.cpp` |
   | T8 | A complex record — text segment, app data — travels through export opaque | dxf round-trip · AC1015 (R12 for T8b) |
   | T9 | A file that redefines a built-in renders seeded and writes the file's own metrics back | dxf round-trip |
   | T10 | Block-insert expansion and `changeAttributes` copy the name, not only the enum | `librecad_tests` · new TU |
   | T11 | `QG_WidgetPen` keeps an unknown name through both `setPen` overloads | `librecad_tests` · offscreen |
   | T12 | A `DIMSTYLE` `345` reference resolves back to the named linetype; DWG stays a gap | dxf round-trip · `[dimstyle]` |
   | T13 | Edge patterns and names are written verbatim, so the render normaliser never leaks | dxf round-trip (+ `i18n_caret_nfc_tests.cpp`) |
   | T14 | The QCad-1 reader keeps group 6 and synthesises a marker for it | dxf round-trip · `[dxf1]` |
   | T15 | The SHP `LTYPE` column reaches the pen and is exported as a marker | `librecad_tests` · `shp_import_filter_tests.cpp` |

   <details>
   <summary>T1–T15 — assertions, sub-cases and fixture strings (unchanged)</summary>

   - **T1 import keeps the name**: entity pen `getLineTypeName() == "VENDOR_TAB"`; enum cache
     `SolidLine`; `graphic->findLineType("VENDOR_TAB")->pattern == {20,-20}`.
   - **T2 round-trip**, run twice over the export version as `dxf_object_tests.cpp` does —
     `RS2::FormatDXFRW` (AC1021) and `RS2::FormatDXFRW12` (AC1009, the machine format): export
     writes `6/VENDOR_TAB` on the entity and on the layer, the LTYPE record with `73`=2, `40`=40,
     `49`=20,−20 **once**; re-import equals; second export identical. In the R12 lane
     `recordGroupValues(out12,"LTYPE","5").empty()` and no `74` group is written, and
     `BYLAYER`/`BYBLOCK`/`CONTINUOUS` are the library's own records, not user ones.
   - **T3 unregistered names** (markers, Q5): after import the list holds markers with an empty
     pattern for `VENDOR_NOREC` (entity), `VENDOR_LAYER_NOREC` (layer) and `VENDOR_BLK_NOREC`
     (block member); export writes all three with `73`=0
     (`CHECK(ltypeRecordGroupValues(out,"VENDOR_LAYER_NOREC","73") == std::vector<std::string>{"0"})`,
     same for the other two), the entity still says `VENDOR_NOREC`,
     `CHECK(namedRecordGroupValues(out,"LAYER","L_NOREC","6") == std::vector<std::string>{"VENDOR_LAYER_NOREC"})`
     and the member inside `BLOCK`/`ENDBLK` keeps `VENDOR_BLK_NOREC`. Sub-cases:
     (a) an entity whose group 6 is **empty** imports as ByLayer
     (`CHECK(line->getPen(false).isLineTypeByLayer())`) and writes **no** empty-named record
     (`CHECK(ltypeRecordGroupValues(out, "", "73").empty())`); `BYLAYER`/`BYBLOCK`/`CONTINUOUS`
     never get markers either; (b) `6/ACAD_ISO02W100` with no record → the synthesised record
     carries the DASHED metrics (`49` = 12.7,−6.35), so the file stays valid and the entity still
     renders dashed after Phase 2 — an empty marker would turn it solid; (c) a name that only
     ever reached a pen in memory (`setLineTypeName`, no table entry) whose single entity is
     deleted (undoable) → export synthesises no record for it, undo → it is synthesised again;
     an *imported* marker is a table entry and is still written, like an unused layer; (d) under
     `DWGSUPPORT`, after `FormatDWG2004` export and re-import,
     `REQUIRE(fromDwg.findLineType("VENDOR_NOREC") != nullptr)` with an empty pattern, the entity
     keeps `VENDOR_NOREC` with enum `SolidLine`,
     `fromDwg.findLayer("L_NOREC")->getPen().getLineTypeName() == "VENDOR_LAYER_NOREC"`
     (`dwgWriter15::emitLayerRecord` substitutes `CONTINUOUS` on a `ltypeMap` miss, silently) and
     the block member keeps `VENDOR_BLK_NOREC`; (e) R12 lane: a document whose archived record
     carries `102 {ACAD_REACTORS` still exports
     (`REQUIRE(filter.fileExport(graphic, out12, RS2::FormatDXFRW12))`) with that record written
     dash-only — `dxfRW::writeTableEntryAppData` fails the whole write below `AC1014`.
   - **T4 case folding** (both lanes): `Vendor_mixedCase` record + entity `6/VENDOR_MIXEDCASE`
     resolve to one list entry; export writes one record. AC1021: the record keeps
     `Vendor_mixedCase`, the entity its literal `VENDOR_MIXEDCASE`. R12: record, layer and entity
     group 6 come out `VENDOR_MIXEDCASE`, `ltypeRecordGroupValues(out12,"Vendor_mixedCase","49")`
     is empty (the helper is case-sensitive), `recordGroupValues(out12,"LTYPE","5").empty()`,
     re-import gives `findLineType("vendor_mixedcase") != nullptr` with the same
     `countLineTypes()`, and a second R12 export is byte-equal to the first. A second record
     `VENDOR_MIXEDCASE [30,-30]` in the same file is dropped and the first pattern survives
     (first record wins, §11). Under `DWGSUPPORT` the entity comes back with the **record**
     spelling (`dwgReader::parseAttribs` copies `lt->name`) and resolves to the same entry.
   - **T5 literal alias**: `ACAD_ISO09W100` entity → name `ACAD_ISO09W100`, enum `PhantomLine`;
     export writes `6/ACAD_ISO09W100` (replaces the current "writes it back as PHANTOM" test);
     the R12 lane writes the same name (already upper-case).
   - **T6 built-in invariant** (extend the existing test): for every `RS2::LineType` value,
     `getLineTypeName()` after `setLineType(v)` equals `lineTypeToName(v)`, and for the 35
     built-in names `nameToLineType(getLineTypeName()) == v`. `NoPen`, `LineTypeUnchanged` and
     `LineSelected` report `"CONTINUOUS"`, exactly as `lineTypeToName()` does today.
   - **T7 pen and list semantics** (`librecad_tests`): two pens with the same enum and different
     names are `!=` and not `isSameAs`; `updateBy`/`setLineTypeFromPen` copy the name;
     `RS_Pen(RS2::FlagInvalid)` with the flag cleared `== RS_Pen(black, Width00, SolidLine)`;
     `RS_Pen(c,w,NoPen) != RS_Pen(c,w,SolidLine)` and `isSameAs` another `NoPen` pen;
     `setLineTypeName("continuous")` equals a `SolidLine` pen; `setLineTypeName("")` equals the
     default pen, and so does a pen built from an empty imported name;
     `setLineTypeName("ACAD_ISO09W100")` → `getLineType() == RS2::PhantomLine`;
     `setLineTypeName("bylayer")` → `isLineTypeByLayer()`; an entity imported with `6/BYLAYER`
     equals `RS_Pen()` and is written back as `ByLayer`, unchanged from today. List: a
     **default-constructed** `RS_Graphic g;` (no `initForNewDocument()`, no import) has 35 entries
     with `isModified()` false; `initForNewDocument()` twice → still 35, no duplicates, still not
     modified; `addLineType` of a custom entry flips it; a bare `RS_Graphic` with one
     `RS2::DashLine` line exported to DXF writes
     `ltypeRecordGroupValues(out,"DASHED","73") == std::vector<std::string>{"2"}` with
     `49` = 12.7,−6.35, and under `DWGSUPPORT` the DWG round-trip returns it as `DashLine`.
   - **T8 complex record opaque** (fixture (b), export `RS2::FormatDXFRW2000`): the entity keeps
     `VENDOR_CPLX`; the exported record keeps the source `49` sequence and, per segment, its
     `74`/`75`/`9`/`46`/`50`/`44`/`45` values
     (`CHECK(ltypeRecordGroupValues(out,"VENDOR_CPLX","9") == std::vector<std::string>{"GAS"})`),
     the `102 {APP … 310 …}` group survives, `73` is written once, and
     `CHECK(recordGroupValues(out,"LINE","6") == std::vector<std::string>{"VENDOR_CPLX"})`.
     **Not byte-wise**: `5`/`330`/`100` are minted and `DRW_LType::update()` recomputes `73`/`40`.
     The `340` style handle is deliberately **not** asserted — `writeLineType` re-emits the source
     handle while `writeTextstyle` mints fresh STYLE handles (it runs after `writeLTypes`), so it
     dangles today; a documented gap (§2 Q6), fixable later by remapping through the text-style
     map or clearing `shapeFlags` on write. **T8b**: the same document exported with
     `RS2::FormatDXFRW12` writes the `49` list only and no `74` group (`writeLineType` gates the
     segment groups on `version > AC1009`), and a re-import keeps the name.
   - **T9 built-in name redefined by the file**: the `HIDDEN [64,-32]` record plus a `6/HIDDEN`
     entity → `findLineType("HIDDEN")->pattern` is the **seeded** one ({6.35,−3.175}) with
     `builtin == true` and `hasImportedRecord == true`; export writes `49` = 64,−32 **once** (the
     archived record still wins on write, as today) and a HIDDEN line renders with the built-in
     run lengths. The name-only `DASHED` record (no `73`/`40`/`49`, the shipped template's shape)
     plus a DASHED entity → export writes `DASHED` with `73`=2 and the built-in `49` values
     (today it writes `73`=0 — a deliberate fix, stated in the PR description).
   - **T10 name propagation through the engine copy sites** (`librecad_tests`,
     `[linetype][named]`): (a) a ByBlock line inside a block inserted with a pen
     `setLineTypeName("VENDOR_TAB")` → the expanded entity's
     `getPen(false).getLineTypeName() == "VENDOR_TAB"` (mirrors `block_insert_wipeout_tests.cpp`;
     `rs_insert.cpp` · file-local `updatePen()` copies the enum today); (b)
     `RS_Modification::changeAttributes` with a named `data.pen` and `changeLineType = true` →
     the replacement entity keeps `VENDOR_TAB` with enum `RS2::SolidLine`, in model space and in
     block members (`doChangeEntityAttributes`/`doChangeBlockAttributes`; test beside
     `lib/modification/tests/rs_modification_trim_tests.cpp`). Dimension pens
     (`RS_Dimension::getPen*Line`) are covered in Phase 5, the layer-tree copy
     (`LC_LayerTreeWidget::copyLayerAttributes`) in Phase 3.
   - **T11 pen widget keeps an unknown name** (`librecad_tests`, `QT_QPA_PLATFORM=offscreen`,
     using the `widgetApplication()` helper of `rs_hatch_tests.cpp`):
     `QG_WidgetPen w; RS_Pen p; p.setLineTypeName("VENDOR_TAB"); w.setPen(p, true, false, "");`
     `REQUIRE(w.getPen().getLineTypeName() == "VENDOR_TAB");` and the same through the
     `setPen(const RS_Entity*, const RS_Layer*, const QString&)` overload (step 4b).
   - **T12 dimension-style reference** (fixture (b), DXF only, `[linetype][named][dimstyle]`):
     export with `RS2::FormatDXFRW` writes a `345`
     (`CHECK(!recordGroupValues(out,"DIMSTYLE","345").empty())` — the group is only written for
     DXF > AC1018) and after re-import the style's `dimensionLine()->lineTypeName() == "VENDOR_TAB"`.
     The DWG branch (`findLineTypeHandleToWrite` returns `NoHandle` whenever `m_dxfW == nullptr`,
     §0.1) stays a **documented gap**, listed as out of scope in §8 step 2 — not a Phase-5
     deliverable.
   - **T13 edge names and patterns**: `VENDOR_NEG` `[-20,-20]`, `VENDOR_ODD` `[10,-5,0]` and
     `VENDOR_ZERO` `[0,0]` import and are exported **verbatim** —
     `ltypeRecordGroupValues(out,name,"49").size()` == 2/3/2 with the values compared as
     `std::stod(...) == Catch::Approx(...)`, the idiom of the HIDDEN test, and `73` == 2/3/2 — so
     the rendering normaliser never leaks into the written 49-values. A whitespace-only group 6 is
     trimmed to empty and lands as ByLayer (step 4 of §4). The record `Ölfarbe` with an entity
     `6/ÖLFARBE` gives **one** table entry and one written record
     (`ltypeRecordGroupValues(out,"Ölfarbe","73").size() == 1`,
     `ltypeRecordGroupValues(out,"ÖLFARBE","73").empty()`), and an R12 export re-imports to one
     entry (`writeUtf8Caps` leaves non-ASCII bytes alone); an NFD/NFC `find()` case goes beside
     the layer one in `i18n_caret_nfc_tests.cpp` (`[i18n][nfc][linetypes]`, the `QChar(0x0308)`
     idiom). A `73` without `49` is libdxfrw's pre-existing hard failure
     (`declaredSize != size` → `fileImport` returns false): the marker logic runs only after a
     successful read and never repairs it — stated, not asserted.
   - **T14 QCad-1 reader** (`[linetype][named][dxf1]`, `#include "rs_filterdxf1.h"`;
     `RS_FilterDXF1` is already in `librecad_lib`, no CMake change): write a minimal QCad-1 style
     buffer (`0 SECTION 2 ENTITIES 0 LINE 8 0 6 VENDOR_TAB 10 0 20 0 11 10 21 0 0 ENDSEC 0 EOF`,
     one pair per line) with `tmpFile()`/`writeText()`; `RS_FilterDXF1 f;`
     `REQUIRE(f.fileImport(graphic, src, RS2::FormatDXF1));`
     `CHECK(graphic.firstEntity()->getPen(false).getLineTypeName() == "VENDOR_TAB");`
     `REQUIRE(graphic.findLineType("VENDOR_TAB"));` with an **empty** pattern (that reader parses
     no LTYPE table, so it is a marker); then export through `RS_FilterDXFRW`:
     `CHECK(ltypeRecordGroupValues(out,"VENDOR_TAB","73") == std::vector<std::string>{"0"});`
     `CHECK(recordGroupValues(out,"LINE","6") == std::vector<std::string>{"VENDOR_TAB"});`.
   - **T15 SHP linetype column** (`shp_import_filter_tests.cpp`, `librecad_tests`,
     `[shp][filter][linetype]`), following the corpus convention — the SHP suites are read-only,
     fixtures are committed: extend `scripts/make_shp_fixtures.py` with
     `test_data/shp/ltype_point.{shp,shx,dbf}` (one POINT record; DBF with an `LTYPE` C(32) field
     holding `VENDOR_TAB`, `write_minimal_dbf` generalised to take a field list), listed in the
     script's generated set and in `update_inventory()`. Then
     `REQUIRE(filter.fileImport(graphic, corpusPath("ltype_point.shp"), RS2::FormatSHP));`
     `CHECK(firstPoint->getPen(false).getLineTypeName() == "VENDOR_TAB");`
     `CHECK(graphic.findLineType("VENDOR_TAB") != nullptr);` and, after an `RS_FilterDXFRW`
     export, `ltypeRecordGroupValues(out,"VENDOR_TAB","73") == std::vector<std::string>{"0"}`.

   </details>

5. Nothing else. T1–T15 are written against the Phase-1 API and therefore do not compile on their
   own — and they are never pushed as a standalone commit: `dxf_roundtrip_tests.cpp` is a TU of
   `librecad_tests`, `librecad_fast_tests`, `librecad_dxf_fast_tests` and the generated micro
   target `librecad_dxf_roundtrip_tests` (CMakeLists.txt · `LIBRECAD_FAST_TEST_SOURCES`,
   `LIBRECAD_DXF_FAST_TEST_SOURCES`, the `add_librecad_filter_micro_test` foreach), and
   `dwg-dxf-verify.yml` builds `librecad_dxf_fast_tests` on every PR touching
   `librecad/src/lib/filters/**`, so a non-compiling TU turns that lane red and leaves a
   non-building commit for `git bisect`. The Phase-0 commit stays fork-only and is folded into
   commit 2b of §10 (PR-1). If maintainers ask for tests-first, the new `TEST_CASE`s are wrapped in
   `#ifdef LC_NAMED_LINETYPES` (defined by the Phase-1 commit, removed in PR-2). T7's file
   `linetypes/tests/lc_linetypelist_tests.cpp` is registered by the Phase-1 commit (§4 step 8),
   so Phase 0 edits no build list.

### Validation
```bash
gh issue view 1738 -R LibreCAD/LibreCAD --comments | tail -40     # the comment is up
git grep -n '\[linetype\]\[named\]' -- librecad/src/lib/filters/tests   # the red tests are present
# the Phase-0 commit must still build every lane that compiles dxf_roundtrip_tests.cpp
cmake --build build --target librecad_dxf_fast_tests librecad_dwg_fast_tests librecad_tests
# no site may copy a linetype by enum outside the documented allow-list
git grep -n 'setLineType(.*getLineType()' -- librecad/src | grep -v 'no_used\|rs_filterdxf.cpp\|rs_filterjww.cpp'
```
The grep must list only the UI files still scheduled for Phase 3 (`qg_widgetpen.cpp`,
`qg_pentoolbar.cpp`, `lc_layertreewidget.cpp`); it is re-run at every PR of the ladder.

### Commit
```
test(linetype): red tests and neutral fixture for named linetypes (#1738)
```
(kept on the feature branch; folded into commit 2b of §10 — landed standalone only behind the
`#ifdef` of step 5, if maintainers prefer tests-first.)

---

## 4. Phase 1 — Engine + round-trip (PR-1)

**Goal**: a drawing can carry any linetype name with its pattern, and LibreCAD preserves it on
open and save (DXF and DWG). No rendering, no UI: custom types are drawn as their nearest
built-in (solid for vendor patterns) until Phase 2. After this PR the machine already receives
the right names from a file that was only opened and saved.

### Steps
1. **`LC_LineType`** (`linetypes/lc_linetype.{h,cpp}`): data struct + `clone()`,
   `fromDrw(const DRW_LType&)` (path → pattern, `size`/`length`/`flags`; segments ignored here,
   the raw record stays in metadata), `toDrw(DRW_LType&) const`, `isByLayer()/isByBlock()`,
   `key()` = NFC normalisation followed by **ASCII-only** upper-casing — the byte-wise
   `std::toupper` fold used by `normalizeDwgTableName`, `LC_DwgAdvancedMetadata::tableNameEquals`,
   `dxfRW::writeLineType` and `dwgWriter15::addLType` — so the table, the metadata archive, the
   `writeLTypes()` dedupe and both writers agree on what counts as one name. A Unicode fold would
   merge two names that the DWG writer still keeps apart, and the entity would be written ByLayer
   silently; `findLineTypeHandleToWrite()` (today `QString::toUpper()`) switches to the same fold
   in this PR.
2. **`LC_LineTypeList`** (`lc_linetypelist.{h,cpp}`, with the listener declared in the same
   header, §1): copy the shape of `RS_LayerList` — `QList<LC_LineType*>` + `QSet`,
   `count/at/begin/end`, `find(name)`
   **case-insensitive**, `add(LC_LineType*)` returns the survivor and does **not** delete its
   argument on merge (caller keeps ownership until accepted), `edit()`, listeners, `m_modified`,
   `remove/clear/rename` `protected` + `friend class RS_Graphic`. `seedBuiltins()` inserts the 35
   entries from `LC_LineTypeNames` with `builtin = true` (not removable/renamable) and is
   **called by the constructor** — the `LC_UCSList` precedent, whose ctor appends the WCS and ends
   with `setModified(false)`. Every `RS_Graphic` therefore holds the built-ins from birth,
   including the ones that never see `initForNewDocument()`: `RS_Clipboard`'s,
   `RS_ActionBlocksSave::createGraphicForBlock`'s, `LC_LayersExporter`'s, the XREF `RS_Graphic ext;`
   of `rs_filterdxfrw.cpp`, `console_dxf2dwg.cpp`'s and the bare `RS_Graphic graphic;` of ~87 test
   cases. `seedBuiltins()` is idempotent and leaves `m_modified == false`; `clear()` drops the
   custom entries and keeps the seed (as `LC_UCSList::clear()` keeps `m_wcs`).
   **`add()` precedence** — three sources can carry one name (the seed, the file's LTYPE record,
   the archived raw `DRW_LType`): a record whose `key()` matches a **seeded built-in** keeps the
   seeded pattern/description/enum (`builtin` stays true; the built-in look is fixed, §12 — and
   group-49 values are drawing units × `$LTSCALE`, not device mm, so a file's metrics must not
   drive the built-in dashes before Phase 6) and only sets `hasImportedRecord = true`; the raw
   record goes to metadata and still wins on write (step 7), so export stays byte-compatible with
   today. For a **non-built-in** key the **first record wins**: a later record whose `key()` already
   resolves leaves the entry's pattern/description/flags untouched (its raw copy is still archived
   for export) and the first spelling stays the key (§11, T4); only `edit()` replaces metrics.
   A **name-only** record (empty path — the 18 name-only records of the 21 in
   `support/library/templates/empty.dxf`, `2 DASHED / 70 0` with no `73`/`40`/`49`; only ByBlock,
   ByLayer and CONTINUOUS carry metrics there) never changes an entry's metrics: today such a record
   makes every template-started drawing export `DASHED` with `73`=0, which this rule deliberately
   fixes (T9, stated in the PR description).
3. **`LC_LineTypeNames`** (`lc_linetypenames.{h,cpp}`): one static table
   `{name, description, 73, 40, {49…}, RS2::LineType}` holding the metrics that today live as 35
   literal `writeLType(...)` calls in `writeLTypes()`; `nameToLineType(const QString&)` (same
   mapping as today, **including aliases**) and `lineTypeToName(RS2::LineType)`. The filter's
   two static functions become thin forwarders (keep their signatures — tests and
   `lc_dimstyle.cpp` call them). `rs_filterdxf1.cpp` and `rs_filtershp.cpp` call the new home.
4. **`RS_Pen`**: `QString m_lineTypeName`. **Invariant**: an empty `m_lineTypeName` means "the
   canonical name of `m_lineType`". `setLineType(enum)` *clears* the name (stays inline in
   `rs_pen.h`, no lookup, no new include — the renderers re-set pens per entity), so
   `RS_Pen(const unsigned int f)` (`RS_Pen(RS2::FlagInvalid)` on 20+ sub-entity sites: text
   letters, hatch loops, polyline arcs), the default ctor and every existing `setLineType()` call
   site keep today's equality. `getLineTypeName()` (out-of-line in `rs_pen.cpp`) returns
   `m_lineTypeName` when non-empty, else `LC_LineTypeNames::lineTypeToName(m_lineType)` — never
   empty, so step 7 needs no "empty → canonical" fallback and `NoPen`/`LineTypeUnchanged`/
   `LineSelected` keep exporting as `CONTINUOUS`, exactly as today (#1734 unchanged). New
   **one-argument** `setLineTypeName(const QString& name)` — the shape of
   `LC_DimStyle::DimensionLine::setLineType(const QString&)` — stores the literal name and sets
   `m_lineType = LC_LineTypeNames::nameToLineType(name)`, aliases included
   (`ACAD_ISO09W100` → `PhantomLine`); an empty or whitespace-only argument clears the name, and
   since `nameToLineType("")` is `LineByLayer` an empty group 6 — and a DWG entity whose linetype
   handle failed to resolve (`ltFlags == 3` → `lineType == ""`) — lands as the plain ByLayer pen:
   a pen never carries or reports an empty name, so no empty-named marker can be built. The enum
   is never settable independently of a name; the only writers of the pair are this setter and
   `setLineType(enum)`. **`operator==`, `isSameAs()`, `updateBy()`, `setLineTypeFromPen()` carry
   the name**: compare `m_lineType` first (so `NoPen`/`Unchanged`/`Selected` stay distinct from
   `SolidLine`), equal enums with both names empty are equal, otherwise compare
   `getLineTypeName()` with the ASCII fold of step 1. `isLineTypeByLayer/ByBlock()` keep testing
   the enum. `operator<<` prints the name. Default ctor: `LineByLayer`, name empty →
   `getLineTypeName() == "ByLayer"`, the spelling `lineTypeToName()` and `writeLTypes()` already
   use (R12 upper-cases it on write). Engine sites that copy a linetype **by enum** switch to
   `setLineTypeFromPen()` in this PR, so names survive with no UI involvement:
   `rs_modification.cpp` · `RS_Modification::doChangeEntityAttributes` and
   `doChangeBlockAttributes` (`pen.setLineTypeFromPen(data.pen)` — `RS_AttributesData::pen` is an
   `RS_Pen` and already carries the name, so the struct is unchanged and Pen Copy
   (`LC_ActionPenApply` copy mode) and Modify→Attributes preserve names from Phase 1) and
   `rs_insert.cpp` · the file-local `updatePen()` used by ByBlock resolution, which copies
   `blockPen.getLineType()` today (only `RS_Entity::getPenResolved()` already goes through
   `setLineTypeFromPen()`). Tests T7, T10.
   - **Step 4b — `QG_WidgetPen` keeps the pen it was given** (~10 lines,
     `ui/components/pen`): an `RS_Pen m_sourcePen` stored by
     `setPen(const RS_Pen&, bool, bool, const QString&)` (the `RS_Entity*` overload builds it
     with `entityResolvedPen.setLineTypeFromPen(entityPen)` instead of
     `setLineType(entityPen.getLineType())`); `getPen()` starts from `m_sourcePen`, applies
     colour and width from the combos, and calls `setLineType(cbLineType->getLineType())` only
     when that enum differs from `m_sourcePen.getLineType()`. Without it every accepted dialog
     rebuilds the pen from the combos and rewrites a custom name: `LC_DlgEntityProperties` (and
     its `onPenChanged`), `LC_DlgDimension`, `LC_DlgTolerance`, `QG_DlgMText`, `QG_DlgText`,
     `QG_LayerDialog::updateLayer` and `LC_LayerDialogEx::getPen` (layer tree) all call
     `setPen(ui->wPen->getPen())` on accept, so pressing OK after moving a vertex would turn
     `VENDOR_TAB` into `CONTINUOUS`. `QG_DlgAttributes` is already safe (combo on Unchanged) and
     `QG_PenToolBar` is unaffected. Test T11.
5. **`RS_Graphic` / `RS_Document` / `RS_Block` / `LC_PreviewDocument`**: `m_lineTypeList` value
   member; wrappers `addLineType/findLineType/editLineType/removeLineType/countLineTypes/
   lineTypeAt/addLineTypeListListener/removeLineTypeListListener`; `getLineTypeList()` pure
   virtual on `RS_Document`, override in `RS_Graphic` (returns `&m_lineTypeList`), delegate in
   `RS_Block` (the shape of `RS_Block::getTextStyleList`), and
   `LC_PreviewDocument::getLineTypeList() override { return nullptr; }` in
   `lib/engine/overlays/preview/rs_preview.h` — the **third** `RS_Document` subclass, whose other
   list getters already return `nullptr`; without it Phase 1 does not compile, and every consumer
   must tolerate a null list. `initForNewDocument()` resets the list to the seed (it runs twice on
   the app path — the `QC_MDIWindow` ctor, then `LC_DocumentsStorage::loadGraphic` /
   `loadGraphicFromTemplate` — so the reset is idempotent and leaves `isModified()` false);
   `RS_Clipboard::clear()` does the same; `fileImport()` neither clears nor re-seeds (records
   merge per step 6). **`isModified()` ORs `m_lineTypeList.isModified()`** and
   `setModified(false)` clears it. `addLineType/editLineType/removeLineType` (and the Phase 4
   rename) call `setModified(true)` when the list reports a change — the `RS_Graphic::addVariable`
   pattern — so `LC_DocumentModificationListener::graphicModified` fires and the window `[*]` and
   the Save button follow; `RS_LayerList` does **not** do this today (noted, not fixed here), and
   the load path ends with `setModified(false)`, so import is unaffected.
6. **Filter import** (`rs_filterdxfrw.cpp`): `addLType()` → also
   `m_graphic->addLineType(LC_LineType::fromDrw(data))` (for a built-in name it only archives the
   raw record, step 2); `setEntityAttributes()` and `attributesToPen()` →
   `pen.setLineTypeName(rawName)`. At the end of `fileImport()` a shared helper
   `RS_Graphic::collectReferencedLineTypeNames()` (case-folded set) walks model space **and every
   block definition** with `LC_ContainerTraverser{…, RS2::ResolveAll}` — dimension and polyline
   children included, because `writeBlocks()` writes the children of every dimension through
   `getEntityAttributes()`, so their names reach the file — plus every layer pen, every dim style
   (`DIMLTYPE`, `DIMLTEX1`, `DIMLTEX2`) and the `$CELTYPE` header string; a **marker record** is
   created for every referenced name the list does not have (Q5). The walk skips empty and
   whitespace-only names (step 4 canonicalises them to ByLayer) and names that `find()` already
   resolves case-insensitively, `BYLAYER`/`BYBLOCK`/`CONTINUOUS` included — an empty-named record
   is rejected by `dwgWriter15::addLType` and makes `dxfTableEntryComplete` fail the whole
   re-import. When `nameToLineType(name)` maps the name to a **drawable** built-in family — today
   exactly the `ACAD_ISO02/03/04/05/07/09/10/12W100` aliases; `HIDDEN2`, `DASHEDX2` and friends
   are seeded built-ins and never reach this branch — the marker copies that family's
   `73`/`40`/`49…` from `LC_LineTypeNames`, so the written record is valid and the entity keeps
   rendering dashed; otherwise the marker is empty (`73`=0, `40`=0). `RS_FilterDXF1` and
   `RS_FilterSHP` call the same helper at the end of their own `fileImport()`: `rs_filtershp.cpp`
   keeps the raw DBF name, and `rs_filterdxf1.cpp` (QCad-1 reader, import-only, its table parser
   handles `LAYER` records only) keeps the raw group-6 name — names arriving through it have **no
   pattern** and become empty markers, drawn solid from Phase 2. A stated limitation of that
   legacy reader, not a new LTYPE parser (T14, T15).
7. **Filter export**: `getEntityAttributes()` / `writeLayers()` write `pen.getLineTypeName()`
   (never empty, step 4). `writeLTypes()` emits, deduped by `normalizeDwgTableName` across every
   source: (a) the **35 built-ins unconditionally from `LC_LineTypeNames`** — today's behaviour,
   order, imported-record substitution and `m_builtinLTypeNames` dedupe intact, except that an
   archived record with an **empty path** no longer replaces a built-in's metrics (step 2); the
   table only ever *adds* names, it never gates the built-ins, so a bare `RS_Graphic` (Save Block,
   clipboard, layer export, tests) still writes a complete table; (b) the table's non-built-in
   entries, each emitted **from its archived raw `DRW_LType`** when
   `dwgAdvancedMetadata().lineTypeTableEntries()` holds one — the same case-insensitive lookup
   `writeLType()` does today — so handle, `102` app data, `310`/`330`/`360` sidecars, reactors and
   complex segments survive verbatim: the existing *"DXF unused LTYPE and STYLE application groups
   survive filter round trip"* test stays green and T8 becomes reachable. `LC_LineType::toDrw()`
   is used only where no archive exists (UI/`.lin`-created entries, markers) or the entry was
   edited, and Phase 4 must refresh or drop the archived copy on an edit so the raw record never
   masks it; (c) archived non-built-in records the table lacks; (d) **synthesised markers for any
   referenced name still missing** (`73`=0, `40`=0, empty path — `DRW_LType::validateDxf` requires
   `path.size() == size`), emitted **inside** `writeLTypes()`, which precedes LAYER and ENTITIES in
   DXF and the record gate of the DWG writer. The referenced-name collection reuses the step-6
   helper and **skips entities and blocks flagged `RS2::FlagDeleted`**, exactly like
   `writeEntities()`/`writeBlocks()`, so a name held only by undo memory produces no record (an
   *imported* marker is a table entry and is still written, like an unused layer). Version rule: an
   archived record is re-emitted verbatim only when the target version can carry its groups —
   `dxfRW::writeTableEntryAppData` fails the **whole** write below `AC1014` if the record has app
   data, reactors or an xDict, so R12 exports get the table's dash-only record instead. DWG path
   unchanged (`writeLTypeRecord`).
8. Register the new files in `CMakeLists.txt` and `src.pro`; new test file
   `linetypes/tests/lc_linetypelist_tests.cpp` into `librecad_tests` — this commit is also where
   the Phase-0 T7 file enters that list, together with any new `librecad_tests` TU introduced by
   T10 and T11 (build lists only, no Phase-0 CMake edit).
9. Turn the Phase-0 red tests green; invert the ISO09 save test; keep every existing
   `[linetype]` test green.

**Known and accepted in this phase** (stated in the PR): choosing a different built-in in a
linetype combo replaces a custom name with that built-in — by design, until Phase 3 lists custom
names. Accepting a dialog **without touching** the linetype combo (geometry edits, layer renames,
colour and width changes) keeps the name (step 4b), and Pen Copy, Modify→Attributes and block
expansion keep it from Phase 1 (step 4). The pen toolbar, the pen palette and the plugin API still
work on the enum until Phases 3/5. Opening and saving untouched preserves everything. Custom types
are drawn as their nearest built-in (solid for vendor patterns) until Phase 2.

### Validation
```bash
# build the canonical target list of §1.1 (install + the five test binaries), then:
QT_QPA_PLATFORM=offscreen build/librecad_dxf_roundtrip_tests "[linetype],[ltype]" -s  # all green, incl. [named]
QT_QPA_PLATFORM=offscreen build/librecad_tests "[linetype],[ltype]" -s
QT_QPA_PLATFORM=offscreen build/librecad_tests "[byteid]" | grep BYTEID | sort   # digests unchanged vs master
# the DWG write lane belongs to the gate too: writeLTypes() feeds DWG export (§1.1)
for t in librecad_dxf_fast_tests librecad_dwg_fast_tests librecad_dwg_write_fast_tests; do \
  QT_QPA_PLATFORM=offscreen build/$t --reporter compact | tail -2; done
qmake6 librecad.pro && make -j12
git diff --check
# Local acceptance (fork only). tools/ and test-results/ sit beside this checkout, in the fork
# root; the fixture's real filename is in test-results/named-linetypes/README.md (this document
# stays vendor-free). There is no dxf2dxf console command (main.cpp offers dxf2pdf/png/svg/dwg and
# dwg2dxf), so the round-tripped copies come from the fork GUI harness (open + Save As R12 and
# R2000) or from a fork-only Catch2 case tagged [.][fixture] reading LC_LINETYPE_FIXTURE /
# LC_LINETYPE_FIXTURE_OUT — never from a vendor test inside the upstream-tracked sources.
python3 ../tools/dxf_linetype_dump.py ../test-results/named-linetypes/fixtures/rules.dxf --layers > /tmp/a.txt
python3 ../tools/dxf_linetype_dump.py /tmp/rules_rt_r12.dxf --layers > /tmp/b.txt
# entity and layer group-6 names, case-folded (R12 upper-cases on write): must be empty
diff <(grep -v '^LTYPE ' /tmp/a.txt | tr a-z A-Z) <(grep -v '^LTYPE ' /tmp/b.txt | tr a-z A-Z)
# the 46 source records, desc= column dropped: must be empty (the writer adds its own built-ins)
for n in $(grep '^LTYPE ' /tmp/a.txt | cut -f1 | cut -d' ' -f2); do \
  diff <(grep -P "^LTYPE $n\t" /tmp/a.txt | cut -f1-5) <(grep -P "^LTYPE $n\t" /tmp/b.txt | cut -f1-5); done
```

### Commit
```
Keep custom linetype names and patterns across DXF/DWG round-trips (#1738)

Adds a document-owned linetype table (LC_LineTypeList, seeded with the
built-in families) and makes the name the identity of a pen's linetype;
RS2::LineType stays as a cache for settings, palettes and plugins. The
DXF/DWG filters keep every LTYPE record and every referenced name, and
synthesise a record for names that had none. Rendering and UI are
unchanged in this step: custom types draw as their nearest built-in.
```

---

## 5. Phase 2 — Draw custom patterns from the table (PR-2)

**Goal**: an entity whose pen names a table entry is drawn with that entry's dash list, on
screen, in print preview and in every export that goes through `RS_Painter`.

### Steps
1. `rs_painter.cpp`: `rsToQDashPattern` takes `const std::vector<double>& pattern` (+ `k`)
   instead of the enum; `rsToQtLineType` decides Solid/NoPen/Custom from the pen (special enum
   values) and the pattern (empty → solid). **Never dereference a null `getPattern()`**
   (`librecad/src/lib/gui/rs_linetypepattern.cpp` · `getPattern` returns `nullptr` for every key
   missing from its map — `RS2::LineTypeUnchanged` = 26 is one): fall back to solid; the
   empty-pattern → solid fallback already exists in `setPen`.
   `RS_Painter::setPen(const RS_Pen&)` **keeps its signature and resolves the pattern itself**,
   through the plug it already has for `getBackgroundColor()`:
   ```cpp
   const std::vector<double>* pattern = (m_renderer != nullptr) ? m_renderer->resolveDashPattern(pen) : nullptr;
   if (pattern == nullptr) {
       const auto* builtin = RS_LineTypePattern::getPattern(pen.getLineType());  // may be nullptr
       pattern = (builtin != nullptr) ? &builtin->pattern : nullptr;
   }
   ```
   null or empty → `Qt::SolidLine`, otherwise `rsToQDashPattern(*pattern, k, newDashOffset)`.
   `setPen` also records whether the `QPen` it actually built is dashed (`m_lpenDashed`, beside
   `m_lpen`) for the offset guard of step 2. No pattern field and no table pointer on `RS_Pen`,
   and no second `setPen` overload.
   Because resolution lives **inside** `setPen`, `getPen()`/`setPen()` round-trips keep the
   pattern — `lc_wipeout.cpp` · `LC_Wipeout::draw` saves `painter->getPen()`, fills, restores it
   and only then strokes its frame; `LC_GraphicViewRenderer::doDrawLayerBackground` does the same
   with `penSaved` — and every direct `painter->setPen(RS_Pen)` caller that never passes a
   `setPenFor*` site stays correct unchanged: `LC_GridSystem::drawGrid`/`drawMetaGrid`,
   `RS_OverlayBox::draw`, the UCS/crosshair/angle-basis/relative-zero overlays, the snap
   indicator, the plugin previews (`qc_actiongetpoint.cpp`). A painter with **no** renderer
   degrades to the static enum table; after this PR only the test rig is in that state
   (`QG_LibraryWidget::getPathToPixmap` does call `LC_GraphicViewportRenderer::setupPainter`,
   which does `painter->setRenderer(this)`).
2. Resolution and its cache live in `LC_GraphicViewportRenderer::resolveDashPattern(const RS_Pen&)`
   (new virtual, same null-guard shape as `getBackgroundColor()`, over the `RS_Graphic* m_graphic`
   the base already holds together with `updateGraphicRelatedSettings(RS_Graphic*)`); the six
   `setPenFor*` sites are **unchanged except for the dash-offset guard** below.
   **Resolution order**, one function: (a) enum `NoPen`/`SolidLine`/`LineByLayer`/`LineByBlock` →
   no pattern (solid or nothing), as today; (b) name found **and** the entry is a seeded built-in
   (`builtin == true`) → `RS_LineTypePattern::getPattern(entry->legacyType)` — the static screen
   table stays the only on-screen source for the 35 built-in names, and the entry's 49-values are
   used only for export. That keeps `DASHED`/`DIVIDE`/`DASHDOT`/`BORDER`/`HIDDEN` at today's run
   lengths (the seeded DXF metrics differ: `DIVIDE` screen `{12,-4.9,0.2,-4.9,0.2,-4.9}` vs DXF
   `{12.7,-6.35,0,-6.35,0,-6.35}`), puts the UI-only pens that `setLineType(enum)` labels with a
   canonical name on the static table without a second mechanism, and makes a file that redefines
   a built-in name (the neutral fixture carries `HIDDEN [64,-32]`; T-R7 adds `DOT2 [2,-32]`) change the
   exported record only, never the screen — today's behaviour, so §12 stays true; (c) name found,
   custom → the entry's canonical pattern (step 3); (d) miss → static pattern of the cached enum;
   (e) nothing → solid.
   **No listener below the UI layer.** `LC_LineTypeList` keeps a monotonically increasing
   `unsigned revision()` bumped by every mutator (add/edit/remove/rename/clear/seedBuiltins); the
   renderer keeps `m_patternCache` (`QHash<QString, std::vector<double>>` keyed by
   `LC_LineType::key()`) plus `m_patternCacheRevision` and `m_patternCacheGraphic`, and drops it at
   the start of `render()`/`setupPainter()` when `m_graphic` is null, differs from
   `m_patternCacheGraphic`, or the list revision moved. A renderer has no lifecycle hook,
   `QC_MDIWindow::~QC_MDIWindow` deletes the document **before** its child view and renderer (the
   #2764 class of bug), print-preview and block-edit windows share the parent graphic, and five
   renderers are stack objects (`LC_Printing`, `pdf_print_loop.cpp`,
   `LC_ImageExporter::renderGraphic`, `console_dxf2png.cpp`, `QG_LibraryWidget::getPathToPixmap`)
   — none of them could unregister. A `clear()` that does not notify (the `RS_LayerList` shape)
   is covered by the same counter.
   **Dash-offset continuity**: five enum gates decide it and must stop testing the cached enum, or
   a vendor pattern restarts at every segment while `DASHED` continues across the joint. The four
   `if (pen.getLineType() != RS2::SolidLine) pen.setDashOffset(patternOffset * m_defaultWidthFactor);`
   lines in `LC_GraphicViewRenderer::setPenForEntity`/`setPenForDraftEntity`,
   `LC_PrintPreviewViewRenderer::setPenForPrintingEntity` and
   `LC_PrintViewportRenderer::setPenForPrintingEntity` become unconditional (`setPen` reads
   `dashOffset()` only in its dashed branch, and `m_lastPaintEntityPen.updateBy(originalPen)`
   copies the pen **before** the offset is set, so the `isSameAs()` cache is unaffected);
   `RS_Painter::updateDashOffset` returns early on `!m_lpenDashed` instead of
   `m_lpen.getLineType() == RS2::SolidLine`. The sixth enum gate, `LC_MakerCamSVG::writeLine`, is
   handled in step 5.
3. Pattern normalisation, split in two so nothing device-dependent leaks into the table:
   **(i) canonicalisation** — one engine function, unit-free, unit-tested: merge consecutive
   same-sign values, rotate so the list starts with a dash, complex segments counted as gaps of
   their length, and — the list being cyclic — **if it is still odd, fold its last element into
   its first** (after merging, an odd list starts and ends with the same sign, so the period is
   preserved); never truncate, as `rsToQDashPattern` does today
   (`dashPattern.resize(size - size % 2)` silently drops the trailing dash of an odd `.lin`
   pattern such as `A,.5,-.25,.5`). The sign of an element is carried by its **position**, not by
   its value (`rsToQDashPattern` takes `std::abs(d)`), so the canonicaliser must produce the
   dash/gap alternation itself. **(ii) screen quantisation** — in `RS_Painter` only: the
   `k = dpmm / max(screenWidth, 1)` scaling, `0` (a dot) and any element shorter than one QPen
   dash unit → one dash unit (today's `std::max(k·|d|, 1.)` clamp, i.e. one pen width — what keeps
   RoundCap dots visible), and **fall back to solid when the on-screen period drops below a few
   pixels** (QDashStroker cost; AutoCAD does the same).
4. UI-only pens (selection `DashLineTiny`, the `RS2::DotLine2` snap guides of
   `LC_GraphicViewRenderer::setupRefSnapEntityPen`, `PATTERN_SELECTED`,
   `PATTERN_BLOCK_LINE`) keep the static table, and need no separate mechanism: they are set
   through `RS_Pen::setLineType(enum)`, carry the canonical built-in name (§4 step 4) and
   therefore resolve through rule (b) or (d) of step 2.
5. `LC_MakerCamSVG::writeLine`: the baking trigger becomes `m_convertLineTypes && hasPattern`
   instead of `m_convertLineTypes && RS2::SolidLine != pen.getLineType()` (a vendor pattern caches
   as `SolidLine`, so today it would leave the SVG as a plain `<line>`), the pattern being looked
   up in the graphic's table by the pen's name; the pen stays `line->getPen()`, as today. A
   **custom** entry's 49-values are drawing units and are converted like coordinates
   (`m_lengthFactor`) — no device-mm conversion and no pixel clamp, since the quantised pattern of
   step 3(ii) is meaningless in an SVG. A built-in entry or a miss keeps today's
   `svgPathAnyLineType`/`getLinePattern` path (family factor × `m_defaultDashLinePatternLength`),
   output byte-identical to master. Scope stays `RS_Line`, as today (`writePolyline`/`writeArc`/
   `writeCircle` ignore linetypes, and the pre-existing "ByLayer falls into the default branch"
   quirk is out of scope unless fixed in the same line).
6. Tests — new `librecad/src/lib/gui/render/tests/lc_linetype_render_tests.cpp` in
   `librecad_tests` (CMake only), tag `[linetype][render]`, `QT_QPA_PLATFORM=offscreen`. Render
   tests run through a **headless renderer rig**, not `TestPainter` alone: `TestPainter` is a
   file-local `struct` in the anonymous namespace of `rs_hatch_tests.cpp` with no renderer and no
   document, so name resolution and the renderer pen cache never run in it — a shape to copy for
   the renderer-less cases, not something to include. The rig mirrors
   `LC_ImageExporter::renderGraphic`'s order (painter, viewport, renderer, `loadSettings()`,
   `render()`) and pins the image resolution itself, which that exporter does not: a `QImage` with
   `setDotsPerMeterX/Y(1000)` **before** the `RS_Painter` is constructed (`RS_Painter::getDpmm` =
   `device()->width() / device()->widthMM()`, frozen into `m_cachedDpmm` by the ctor, ≈ 2.84 px/mm
   at QImage's default 2835 dots/m) → 1 px/mm; `LC_GraphicViewport viewport; viewport.setDocument(&graphic);`
   **before** `LC_PrintViewportRenderer r(&viewport, &painter);` (the base ctor captures
   `m_graphic` from the viewport), then `r.loadSettings(); r.render();`. Expected run lengths are
   derived from `painter.getDpmmCached()`, never from a hard-coded dpi.
   - **(T-R1)** zoom independence: a `VENDOR_TAB` `[20,-20]` line rendered at viewport factors 1.0
     and 4.0 has identical on/off run lengths of `round(20 · dpmm)` px.
   - **(T-R2)** pen-cache guard: two circles (circles do not advance the dash offset, so
     consecutive lines could miss the cache on the offset alone) with names `VENDOR_TAB`
     `{20,-20}` and `VENDOR_UTL` `{20,-20,2,-20}`, same colour, width and cached enum, drawn back
     to back → different run lengths; plus a direct `RS_Pen::isSameAs` unit case with two names
     and equal offsets.
   - **(T-R3)** a pattern whose on-screen period is below the threshold renders as one unbroken
     run.
   - **(T-R4)** null-pattern hardening on the settings-bound path (`LC_GridSystem::drawGrid`/
     `drawMetaGrid` and `RS_OverlayBox::draw`, which build a pen from
     `static_cast<RS2::LineType>(LC_GET_INT(...))` and reach `RS_Painter::setPen` directly,
     bypassing the six `setPenFor*` sites):
     `RS_Pen stale(RS_Color(0,0,0), RS2::Width00, static_cast<RS2::LineType>(99));`
     `REQUIRE_NOTHROW(tp.painter.setPen(stale));` and the same for
     `static_cast<RS2::LineType>(RS2::LineTypeUnchanged)` — neither has an entry in
     `getPattern()`'s map — each drawing an unbroken run, through a bare `RS_Painter` and through
     the rig. Update the comment above the existing `getPattern() != nullptr` loop in
     `dxf_roundtrip_tests.cpp`: the painter no longer dereferences unchecked, and the loop stays
     as a table-completeness check.
   - **(T-R5)** continuity: two consecutive collinear `VENDOR_TAB` lines continue the `[20,-20]`
     sequence across the joint with the same run-length vector as two `DASHED` lines do, and a
     solid line between them leaves the offset untouched; repeated through
     `LC_PrintPreviewViewRenderer` if a widget-free construction exists, else noted as the same
     code path.
   - **(T-R6)** `getPen()`/`setPen()` round-trip: a custom-named line, an `LC_Wipeout`, then a
     second line with the same pen → the third entity keeps the custom run lengths.
   - **(T-R7)** built-ins unchanged: with a fixture that redefines `HIDDEN [64,-32]` and
     `DOT2 [2,-32]`, a `HIDDEN` line and the snap guide render with master's run lengths (6/−3
     device mm for `HIDDEN`), `DASHED`/`DIVIDE`/`DASHDOT`/`BORDER` run lengths equal master, and
     the exported record still carries the file's 49-values.
   - **(T-R8)** MakerCam, with a capturing `LC_XMLWriterInterface` stub: a 100-unit `VENDOR_TAB`
     line with entry `[20,-20]` emits a path with three dash segments (0–20, 40–60, 80–100); a
     `DASHED` line's `d` attribute is byte-identical to master.
   - canonicalisation unit tests (step 3(i), no painter): `[20,-20,10]` → `[30,-20]` (truncation
     would give `[20,-20]` and shorten the period), `A,.5,-.25,.5` → `[1.0,-0.25]`, `VENDOR_NEG`
     `[-20,-20]` and `VENDOR_ZERO` `[0,0]` produce a drawable pattern or solid, never an empty
     `QVector` with a dashed style.
   - `librecad_dxf_fast_tests` unchanged.

**Known and accepted in this phase** (stated in the PR): preview graphics build a fresh
`LC_PreviewGraphic` and copy blocks only (`LC_DimStylePreviewGraphicView::init`, both overloads,
`copyBlocks`), so the dim style manager, the Drawing Preferences dimension tab and the dimension
entity dialog (`LC_DlgDimension::onPenChanged` → `updateDimStylePreview`) draw a custom name as its
nearest built-in until Phase 5 copies the table into them (§8 step 1). Redefining a built-in name
in a file changes the exported record only, never the on-screen pattern.

### Validation
```bash
cmake --build build --target librecad_tests    # new lc_linetype_render_tests.cpp TU
QT_QPA_PLATFORM=offscreen build/librecad_tests "[linetype][render]" -s
QT_QPA_PLATFORM=offscreen build/librecad_dxf_roundtrip_tests "[linetype]"
# manual: open the neutral fixture, zoom in/out — vendor patterns keep their mm size like DASHED does
# manual: two collinear vendor lines — the dashes continue across the joint, as DASHED does
```

### Commit
```
Draw custom linetypes from the document's linetype table (#1738)
```

---

## 6. Phase 3 — Choose custom linetypes in the UI (PR-3)

**Goal**: every place that shows or edits a pen offers the document's linetypes by name and
never destroys a name it does not know.

### Steps
1. `QG_LineTypeBox::init(LC_LineTypeList*, showByLayer, showUnchanged, showNoPen)` fills from
   the list (special rows, built-ins, custom); `itemData` = a small `QVariant`-registered value
   `{QString name; RS2::LineType legacy;}` with `operator==` by case-insensitive name;
   `setLineType(name)`; **never leave −1** (today `findData(t)` misses → index −1 →
   `itemData(-1).toInt() == 0` → `NoPen` is emitted): a temporary row "(not defined in this
   drawing)" like `QG_ColorBox::addTemporaryCustomColor`. The static
   `init(showByLayer, showUnchanged, showNoPen)` stays for the 6 settings combos. A name-based
   signal is added beside the enum one.
   - **Row text**: a built-in row keeps the translated label it has today, looked up by
     `legacyType` — the combo keeps its own `tr("Dash (small)")` strings, and the property-sheet
     cell (`LC_PropertyLineTypeComboboxView::doDrawValueDetails`),
     `LC_PenPaletteModel::setupItemForDisplay` and `LC_QuickInfoEntityData` keep
     `LC_PenInfoRegistry::getLineTypeText(legacyType)` — with the DXF name as tooltip; a custom
     row shows its `name` verbatim and, when present, `description` as tooltip. So a CAM user sees
     `VENDOR_TAB` verbatim while "Dash (small)" keeps its translation, no `.ts` file is touched and
     `lupdate` is not run in this PR (precedent: #2815/#2823 added `tr()` strings and left
     `librecad/ts` to the maintainers' regeneration commits), which is what keeps the §1.1
     "TRANSLATIONS untouched" line true.
   - **`init()` once, `rebuild()` silent**: `init()` today ends with
     `connect(this, &QG_LineTypeBox::activated, …); setCurrentIndex(0); slotLineTypeChanged(currentIndex());`
     and `setLineType()`/`setLayerLineType()` always end with `slotLineTypeChanged(currentIndex())`,
     which emits `lineTypeChanged` → `QG_PenToolBar::slotLineTypeChanged` → `penChanged` →
     `RS_Document::setActivePen`. So `init()` stays one-time (guarded by an `m_initialized` flag,
     as its header comment already claims) and a new `rebuild(const LC_LineTypeList*)` clears and
     refills the rows under a `QSignalBlocker`, restores the previous selection by name (adding the
     temporary row when the list lacks it) and **emits nothing**; `setLineType()`/
     `setLayerLineType()` emit only when the resulting value actually changed. Without this, each
     document switch adds another `activated` connection and resets the row to 0.
   - **Value type**: `LC_PropertyLineType::ValueType` becomes that value (`Q_DECLARE_METATYPE`),
     and every enum instantiation changes in the same commit — it does not compile otherwise:
     `lc_property_linetype.h`, `lc_property_linetype_combobox.{h,cpp}`,
     `lc_property_linetype_combobox_view.{h,cpp}`, `lc_entity_type_propertiesprovider.cpp` (the
     `"linetype"` get/set/equals lambdas), `lc_propertiesprovider_graphic_layer.cpp` ·
     `createLineType`, `lc_propertiesprovider_active_pen.cpp`,
     `lc_propertiesprovider_dim_base.cpp` · `addLineType_DS` (×5), `lc_matchdescriptor_base.h`
     (`"lineType"`, `"lineTypeR"`), `lc_matchdescriptor_dimbase.h` · `addLineTypeDS` (×5),
     `lc_propertymatchertypes.{h,cpp}` (`TLINE_TYPE`, `initLineType`, `LINE_TYPE`,
     `LINE_TYPE_RESOLVED`) and the `LC_GenericEntityMatcher<RS2::LineType>` cast in
     `lc_dlgquickselection.cpp` · `createMatcher`. PR-3 is sized accordingly (§10).
2. Icons for custom types painted at run time (`QPixmap` 32×12, `QPen(CustomDashLine)` with the
   painter's conversion, palette text colour, cached by name+pattern); built-ins keep their
   `.lci`. `LC_PenInfoRegistry`: name path with enum fallback (its `const` getters already return a
   default for an unknown enum without inserting, §0.4).
3. `QG_WidgetPen` gains `setLineTypeList(LC_LineTypeList*)`, called **before** the first
   `setPen()` (that is where `cbLineType->init()` runs — in
   `QG_DialogFactory::requestNewLayerDialog`/`requestEditLayerDialog` that means beside the
   existing `dlg.setLayer(layer); dlg.setLayerList(layerList);` pair, before `exec()`), or the
   widget rebuilds the combo when the list changes. `nullptr` = built-ins
   only, rows and enum values exactly as today: the mode used by the five settings-bound hosts
   (`LC_LayerTreeOptionsDialog` · `wPenNormal`/`wPenDimensional`/`wPenInfo`/`wPenAltPos`, persisted
   through `RS_Settings::writePen`/`readPen` as ints, and `LC_QuickInfoWidgetOptionsDialog` ·
   `wHighlightPen`). `LC_DlgEntityProperties`, `LC_DlgDimension`, `LC_DlgTolerance`, `QG_DlgText`
   and `QG_DlgMText` pass `m_entity->getGraphic()->getLineTypeList()`. The factory methods
   `requestNewLayerDialog`/`requestEditLayerDialog`/`requestAttributesDialog` gain an `RS_Graphic*`
   (precedent: `requestOptionsDrawingDialog(RS_Graphic&, …)`; they carry an `RS_LayerList` today
   and `RS_LayerList` keeps no back-pointer, while the callers `RS_ActionLayersAdd`/`Edit` and
   `LC_ActionModifyAttributes` already own `m_graphic`), and `QG_LayerDialog`, `QG_DlgAttributes`
   and `LC_LayerDialogEx` (constructed by `LC_LayerTreeWidget`, which owns the document) receive
   it. `RS_AttributesData` needs no new field — its `pen` is an `RS_Pen` and already carries the
   name, and `RS_Modification::doChange*Attributes` copy it through `setLineTypeFromPen()` from
   Phase 1 (§4 step 4, proved by the undo test of §8 step 5). A settings pen injected by
   `LC_LayerDialogEx::layerTypeChanged` comes from `RS_Settings::readPen` and carries an
   enum-canonical name, so it always matches a built-in row.
4. Pen toolbar: `QG_PenToolBar` has no `RS_Pen` entry point today — `setLineType(RS2::LineType)`,
   `setLayerLineType(RS2::LineType, bool)` and `updateByLayer()` (which passes only
   `pen.getLineType()` to `QG_LineTypeBox::setLayerLineType`) become `setLineType(const RS_Pen&)`,
   `setLayerLineType(const RS_Pen& layerPen, bool updateSelection)` and an `updateByLayer()` that
   hands over the whole layer pen, so the By-Layer row can be painted from the layer's pattern.
   Migrate the 9 enum-only feeders (`git grep -n 'penToolBar->set\(LineType\|LayerLineType\)'`):
   `LC_ActionPenPick::applyPenToPenToolBar` (×2), `LC_ActionPenSyncActiveByLayer::init`,
   `lc_penpalettewidget.cpp` (×5: `updatePenToolbarByActiveLayer`, `applyEditorPenToPenToolBar`,
   `applySelectedPenItemToPenToolBar`) and `lc_propertiesprovider_active_pen.cpp` — otherwise
   "Pen > Pick" from a `VENDOR_TAB` line leaves a `CONTINUOUS` active pen while the user believes
   the picked pen is active, and every entity drawn next is silently rewritten. Field-wise pen
   copies inside the UI must copy the name too (`setLineTypeFromPen()`/`updateBy()`):
   `QG_WidgetPen::setPen(const RS_Entity*, const RS_Layer*, const QString&)` and
   `LC_LayerTreeWidget::copyLayerAttributes`. The toolbar calls the silent `rebuild()` of step 1
   from `setGraphicView()` and from `lineTypeListModified()`, keeps `m_currentPen` untouched, and
   adds/removes its `LC_LineTypeListListener` exactly as it does with `setLayerList()` (`nullptr`
   on close, so `setupWidgetsByWindow(nullptr)` detaches it before the document is deleted). On
   document switch, a name missing in the target is **shown, not imported** — the temporary row of
   step 1, the record entering the table only on first use (§2). The
   `RS_Settings` default pens stay on the enum; the pen palette has its own step 7.
5. Property sheet: `LC_PropertyLineType` carries the list (like `LC_PropertyLayer`), equality by
   name (its `ValueType` and the ~15 instantiation sites that change with it are listed in
   step 1); quick select compares names (`LC_PropertyMatcherTypes::STRING` exists); quick info
   shows the name.
6. GUI harness (fork, VNC/xdotool): open the fixture, read the name in Properties, assign a
   custom type from the pen toolbar, save, verify with `check_dxf_linetype.py`; and Pen > Pick
   from a custom-typed line, draw a line, save — `check_dxf_linetype.py` must report the custom
   name on both `LINE`s.
7. Pen palette (`penpalette.lcpp`, **not** `.lcp`): `LC_PenItem` gains the linetype name as
   identity beside `m_lineType` (its current `m_lineTypeName` is registry display text, not an
   identity); `doApplyPenAttributesToSelection`, `createPenByEditor`, `createPenByPenItem`,
   `doFillPenEditorByPen` and `doUpdatePenEditorByPenAttributes` take/produce a name-carrying
   `RS_Pen` instead of `RS2::LineType`; `doSelectEntitiesThatMatchToPenAttributes` compares names
   case-insensitively (`RS2::LineTypeUnchanged` keeps meaning "any"), so selecting by `CONTINUOUS`
   no longer selects every custom-named entity whose cache is `SolidLine`;
   `LC_PenPaletteModel::setupItemForDisplay` resolves text and icon by name first, registry
   fallback. `cbType` is connected to `currentIndexChanged`, so it is refilled only through the
   silent `rebuild()` of step 1 and the rebuild must not mark the editor dirty. Persistence:
   `toStringRepresentation` appends the name as an optional **5th** field after the pen name,
   written only for non-built-in names; `fromStringRepresentation` accepts `size() == 4` (legacy,
   name = `lineTypeToName(enum)`) or `size() == 5` with a syntactically valid DXF table name in
   field 5 — the palette is read before any document exists, so no table lookup is possible there
   and the name is resolved against the document when the pen is applied. A legacy line whose pen
   name contains a comma is still rejected, as today, and released builds (`size() == 4` only)
   drop the 5-field lines — stated in the PR.
8. Dimension styles: `LC_DlgDimStyleManager::init` feeds `cbDimLineLineType`, `cbExtLineType1` and
   `cbExtLineType2` from `m_originalGraphic->getLineTypeList()`; `fillLinesTab` selects by raw name
   (`lineTypeName()`, `lineTypeFirstRaw()`, `lineTypeSecondRaw()`); `onDimLineTypeChanged`,
   `onExtLineType1Changed` and `onExtLineType2Changed` call the **existing** `QString` setters
   (`LC_DimStyle::DimensionLine::setLineType(const QString&)`,
   `ExtensionLine::setLineTypeFirst`/`setLineTypeSecond(const QString&)`), never the
   `RS2::LineType` overloads, which rewrite `DIMLTYPE`/`DIMLTEX1`/`DIMLTEX2` to
   `lineTypeToName(enum)`. Together with the non-emitting `setLineType()` of step 1 this stops the
   dialog from rewriting a custom name merely by being opened (`connectLinesTab()` runs before
   `fillLinesTab()`, whose `setLineType()` emits into the enum setters today). The property and
   matcher rows (`addLineType_DS` ×5, `addLineTypeDS` ×5) move with the value type of step 1.
9. Tests — new `librecad/src/ui/components/comboboxes/tests/qg_linetypebox_tests.cpp` in
   `librecad_tests` (CMake only; the Phase-3 row of the §1.1 matrix is no longer "none"), with its
   own anonymous-namespace `application()` helper copied from `rs_graphicview_close_tests.cpp` as
   the other widget test files do, `QT_QPA_PLATFORM=offscreen`, tag `[linetype][ui]`:
   - unknown name survives: `RS_Graphic g; g.initForNewDocument();`
     `g.addLineType(new LC_LineType("VENDOR_TAB", {20,-20})); QG_LineTypeBox box;`
     `box.init(g.getLineTypeList(), true, false, false);`
     `box.setLineType(QStringLiteral("NOT_IN_LIST")); CHECK(box.currentIndex() >= 0);`
     `CHECK(box.getLineTypeName() == "NOT_IN_LIST"); CHECK(box.getLineType() != RS2::NoPen);` then
     `box.setLineType(QStringLiteral("vendor_tab"))` → name `"VENDOR_TAB"`, enum `SolidLine`.
   - static combo unchanged: `QG_LineTypeBox s; s.init(true, true, true);` has **37** rows
     (`- Unchanged -`, By Layer, By Block, No Pen + 33 built-in patterns), and for every value it
     lists `s.setLineType(t); CHECK(s.getLineType() == t);`.
   - row text: the `DASHED2` row's `itemText` equals `QG_LineTypeBox::tr("Dash (small)")` and its
     tooltip is `"DASHED2"`; a `VENDOR_TAB` row's text is `"VENDOR_TAB"`.
   - no signal storm: `QSignalSpy` on `QG_LineTypeBox::lineTypeChanged` and
     `QG_PenToolBar::penChanged` across `setGraphicView(A)`, `setGraphicView(B)`,
     `setGraphicView(A)` with a custom pen selected → 0 emissions, `getPen().getLineTypeName()`
     unchanged and neither document's `getActivePen()` changed; one simulated `activated`
     afterwards emits exactly once.
   - toolbar identity: a toolbar bound to a graphic holding `VENDOR_TAB`, fed the matching pen,
     reports `getPen().getLineTypeName() == "VENDOR_TAB"`.
   - property equality and matchers: the `"linetype"` equality lambda reports "multiple values"
     for `VENDOR_TAB` vs `VENDOR_UTL` (same cached enum); the quick-select matcher for
     `VENDOR_TAB` does not match a `CONTINUOUS` entity, and vice versa.
   - palette persistence: `LC_PenPaletteData::fromStringRepresentation("6,0,#ff0000,Pen A")`
     parses with name `"DASHED"`; `"1,0,#ff0000,Pen B,VENDOR_TAB"` parses with
     `getLineTypeName() == "VENDOR_TAB"`; both round-trip through `toStringRepresentation`;
     `"1,0,#ff0000,Pen,C"` is still rejected. Select-by-pen distinguishes `VENDOR_TAB` from
     `CONTINUOUS` with the same cached enum.
   - `QG_WidgetPen` with a **null** list shows only the built-in rows (33, plus By Layer/By Block
     and `- Unchanged -` when asked for) and still returns the name of a custom-named pen through
     the temporary row; harness: the Layer-tree options dialog and the Quick info options dialog
     open and save with no document open.
   - dim styles: a style with `DIMLTEX1 = VENDOR_TAB`, open the manager, change the ext-line-2
     type → `extensionLine()->lineTypeFirstRaw() == "VENDOR_TAB"`.

### Validation
```bash
cmake --build build --target librecad_tests    # new qg_linetypebox_tests.cpp TU
QT_QPA_PLATFORM=offscreen build/librecad_tests "[linetype][ui]" -s     # combo never returns -1; itemData round-trips
# a document switch must emit no pen change: QSignalSpy on QG_PenToolBar::penChanged == 0
# harness: base vs fix screenshots + saved DXF group-6 check (fork test-results/)
```

### Commit
```
Offer the document's linetypes in every pen selector (#1738)
```

---

## 7. Phase 4 — Create, edit and import linetypes (PR-4)

**Goal**: a user creates a linetype or imports a vendor `.lin` inside LibreCAD, saves a template,
and never needs an external script again.

### Steps
1. `lc_linfile.{h,cpp}`: `.lin` reader/writer written from the format description (`;`
   comments, `*NAME,description`, `A,` line with signed decimals, `[...]` elements kept opaque or
   rejected with a diagnostic; per-file metric/imperial choice, ×25.4 when imperial). Tests with
   synthetic acad.lin/acadiso.lin excerpts and edge cases (CRLF, missing description, negative
   first element, element blocks).
   **Name validation** — `LC_LineType::isValidName(const QString& name, QString* reason)`
   (engine, beside the record type; the shape of `LC_UCS::isValidName`; declared in §4 step 1 if
   Phase 1 already needs it, otherwise added here): trimmed and non-empty, no control character
   `U+0000`–`U+001F`, none of the DXF symbol-name characters `< > / \ " : ; ? * | , =` or the
   backquote, at most 255 characters. The control characters are not cosmetic: `libdxfrw` ·
   `isSafeDxfRecordText()` rejects any `\0`, `\r` or `\n` in an entity's linetype, which makes
   `isValidDxfEntityFields()`/`preflightEntity()` set `m_writeError` and lose the **whole** file
   at save time with nothing pointing at the linetype name, while `dxfRW::writeLineType()`
   performs no such check on the record itself. `LC_LineTypeList::add()` applies the rule to
   records created by the dialog or by `.lin` import and returns `nullptr` with the reason, so no
   pen can carry a name the writer refuses; names arriving through the DXF/DWG/DXF1/SHP filters
   are **kept verbatim** (§4 step 6 — the file is the authority, and layer and block names are
   not validated either). The `.lin` reader trims names and names the offending line in its
   diagnostic; a record whose name folds onto a built-in under `LC_LineType::key()` (`*dashed` vs
   `DASHED`) is skipped with a diagnostic rather than offered as a replacement — built-ins are
   read-only (§4 step 2) and their on-screen look is fixed (§12).
2. "Linetypes" dialog: document list; new/edit (description, pattern as a signed number
   list, live preview painted with the same conversion); import `.lin` (choose entries);
   export `.lin`; built-ins are read-only. The name field refuses invalid names inline (step 1),
   the reserved `BYLAYER`/`BYBLOCK`/`CONTINUOUS`, and duplicates under `LC_LineType::key()` —
   the shape of `QG_DlgOptionsDrawing::askForUniqueDimStyleName`. Saving as R12 with a custom
   name longer than 31 characters or carrying non-ASCII letters draws one warning
   (`dxfWriter::writeUtf8Caps` folds ASCII only and the codec escapes the rest as `\U+XXXX`, so
   a CAM reader matching by name may not find it).
   **Rename and delete follow the Dimension Styles tab, not `removeLayer()`**: they are offered
   only for custom entries that are **not in use**, usage being computed as
   `QG_DlgOptionsDrawing::collectStylesUsage`/`updateActionButtons` do for dim styles — the pens
   of entities (recursively through blocks), layer pens, every dim style's
   `DIMLTYPE`/`DIMLTEX1`/`DIMLTEX2` and `RS_Document`'s active pen, i.e. exactly the walk of
   `RS_Graphic::collectReferencedLineTypeNames()` (§4 step 6) with the `RS2::FlagDeleted` skip of
   §4 step 7. No pen is ever rewritten, so nothing has to be undone; edits of pattern and
   description apply in place and the listeners of §4 step 5 refresh the combos. Not undoable,
   like layers and dim styles (§12) — stated in the PR. *Note for the PR*:
   `RS_Graphic::removeLayer()` is **not** a model here — it re-homes the layer's entities to
   layer `"0"` and `undoableDelete`s them inside `startUndoCycle()`/`endUndoCycle()` (the entity
   half **is** undoable; only the table row removal is not), and deletes block members in place.
   Linetype removal must delete nothing.
   **If maintainers pick the standalone-dialog variant of Q7** (step 3), rename and delete may be
   offered on entries in use; they then go through one helper
   `RS_Graphic::replaceLineTypeReferences(oldName, newName)` (delete = replace by `CONTINUOUS` or
   a chosen substitute) that rewrites by name: top-level entities the way
   `rs_modification.cpp` · `RS_Modification::doChangeEntityAttributes` does — clone,
   `pen.setLineTypeFromPen()`, `undoableDelete(original)` + `undoableAdd(clone)` in one undo
   cycle, so the pen rewrite is undoable and marks the document modified (not via
   `RS_Document::undoableModify`, which wants a viewport and skips locked layers) — block members
   in place walking `m_blockList` as `doChangeBlockAttributes` does (not undoable, stated in the
   PR), layers via `RS_Graphic::editLayer`, the dim-style linetype fields,
   `RS_Document::setActivePen`, and notifies the listeners so the pen toolbar re-selects by name.
   Entities held only by undo memory (`RS2::FlagDeleted`) are not rewritten: an Undo after a
   rename or delete restores the old name and the next save synthesises a marker record for it
   (§4 step 7) — stated in the PR. Either way the table row change itself stays outside undo.
3. Action `LineTypes` in `lc_actionfactory.cpp`, menu entry beside Dimension Styles, tab in
   Drawing Preferences (Q7), translations via `tr()`. **Commit model, stated in the PR**: a tab
   in Drawing Preferences inherits `QG_DlgOptionsDrawing`'s contract — the dim-style tab edits
   copies (`prepareDimStyleItems` works on `dimStyle->getCopy()`) and touches the document only
   in `validate()` on OK, where `validateDimensionsTab()` installs the result with one
   `RS_Graphic::replaceDimStylesList()`. The Linetypes tab does the same: it edits a copy of
   `LC_LineTypeList`, commits on OK through one `RS_Graphic::replaceLineTypeList()`, and Cancel
   discards everything — which is why step 2 keeps rename and delete to unused entries, a live
   pen rewrite being impossible to take back on Cancel. Stated limitation: the dim-style manager
   opened from the neighbouring tab works on `m_originalGraphic`, the live table, so a linetype
   created in the same dialog session is offered there only after OK. If maintainers prefer live
   editing, the same widget goes into a standalone modal dialog opened by the `LineTypes` action
   and applies each change immediately as the layer dialogs do (not undoable, listeners fire per
   change) — the variant step 2 covers.
4. Optional (maintainer's call): a sample template in `librecad/support/` with a few generic
   dashed patterns.

### Validation

Tests (`librecad_tests`, `[linfile]` and `[linetype][ui]`):
- a `.lin` record whose name contains `\r`, `\n` or NUL is rejected with a diagnostic naming the
  line, and `LC_LineTypeList::add()` returns `nullptr` for the same name — the refused name never
  reaches the table or an entity pen; a CRLF file yields names without a trailing `\r`.
- `CHECK_FALSE(filter.fileExport(...))` for a document whose pen name was injected directly with
  an embedded newline: documents today's failure mode (the whole export is lost) that the
  validation prevents — the shape of the existing *"DXF export rejects malformed typed conversion
  sidecars"* case.
- the dialog's name field refuses the same names, `BYLAYER`/`BYBLOCK`/`CONTINUOUS`, and a
  duplicate under `LC_LineType::key()`.
- `*dashed` in a `.lin` folds onto the built-in `DASHED`: skipped with a diagnostic, the
  built-in's pattern and screen look unchanged (§12).
- rename and delete are disabled while an entry is referenced by an entity, a layer pen, a dim
  style or the active pen, and enabled once the last reference is gone — the
  `updateActionButtons` contract; Cancel leaves the document's table byte-identical.

```bash
QT_QPA_PLATFORM=offscreen build/librecad_tests "[linfile]" -s
# harness: import a .lin, assign, save, reopen — names and 49-values intact
```

### Commit
```
Add a Linetypes dialog with .lin import/export (#1738)
```

---

## 8. Phase 5 — Parity in the remaining paths (PR-5)

**Goal**: no path left that silently drops a name.

Until this PR lands, every path in step 1 keeps the **name** on the pen but not its pattern: the
destination table holds no record, so the save-time synthesis of §4 step 7 writes a marker
(`73`=0, `40`=0). The file stays valid and the machine still reads the right name, but the type
is *declared* continuous and Phase 2 draws it solid — stated in the PR descriptions of PR-1…PR-4.

### Steps
1. **Every path that builds a second `RS_Graphic` out of the entities of the first** copies the
   custom records those entities, their layers and their dim styles reference, through **one
   shared helper** —
   `LC_LineTypeList::importReferenced(const RS_EntityContainer& entities, const LC_LineTypeList& source)`,
   built on `RS_Graphic::collectReferencedLineTypeNames()` (§4 step 6), the destination entry
   winning a `LC_LineType::key()` clash and built-ins never being replaced (§4 step 2). The
   built-ins themselves need no copying: `LC_LineTypeList`'s constructor seeds them, so even a
   bare `new RS_Graphic()` has them (§4 step 2). The paths:
   - `lc_copyutils.cpp` · `LC_CopyUtils::doCopyEntity` / `doCopyEntityLayer` / `doCopyBlock` —
     add the records referenced by the entity, by its layer and by the block members to the
     clipboard graphic; `rs_clipboard.cpp` · `RS_Clipboard::clear()` already resets the clipboard's
     table beside `clearLayers()` from Phase 1 (§4 step 5).
   - `lc_copyutils.cpp` · `LC_CopyUtils::paste` — **both** branches, which clone entities only.
     There is no layer counterpart to imitate here: `pasteLayers()` is never called from
     `paste()`, which carries sand1024's *"JUST A TEMPORARY IMPLEMENTATION"* FIXME. So this is
     new code, kept to a single helper call so it survives the pending paste-with-externals
     rework.
   - `rs_modification.cpp` · `RS_Modification::libraryInsert` — merge the custom entries of
     `data.source->getLineTypeList()` beside the existing `destination->addLayer(layer->clone())`
     loop.
   - `lc_layersexporter.cpp` · `LC_LayersExporter::exportLayersToIndividualDocuments` /
     `exportLayersToSingleDocument` — copy the referenced records into each export graphic after
     its `initForNewDocument()`.
   - `rs_actionblockssave.cpp` · `RS_ActionBlocksSave::createGraphicForBlock` (Save Block As) —
     the temporary graphic shares the block's entities with `setOwner(false)` and `clearLayers()`
     and copies no table at all; clone the referenced custom records into it before
     `saveBlockAs()`.
   - `rs_filterdxfrw.cpp` · `RS_FilterDXFRW::embedXref` (XREF attach) — after
     `xrefFilter.fileImport(ext, …)` succeeds and before `cloneAndRedirect` clones the entities,
     `add()` a clone of every non-built-in entry of `ext.getLineTypeList()` that the copied layers
     and entities reference; `ext` is otherwise destroyed with its table at the end of the scope,
     so today the host would keep the names and lose every pattern.
     **Open point for maintainers** (proposed answer: raw names): keep the name verbatim, with
     the host record winning a `key()` clash — what CAM matching, the reason this plan exists,
     needs — or namespace it `BLOCK|NAME`, the way this same function already namespaces the
     xref's layers and AutoCAD namespaces xref-dependent objects, which additionally means
     rewriting the cloned pens' names inside `cloneAndRedirect`.
   - `doc_plugin_interface.cpp` · `Doc_plugin_interface::addBlockfromFromdisk` — the plugin API
     loads a file into a local `RS_Graphic g` through `LC_DocumentsStorage::loadDocument` (so
     `g`'s table is filled) and then clones layers, blocks and entities into `m_docGr` one by one;
     merge `g.getLineTypeList()`'s custom entries beside that layer loop. The public
     `document_interface.h` API is unchanged.
   The block-library **preview** graphic (`lc_action_block_library_insert.cpp`) needs no plan
   item: it is filled through the filter and owns the table that arrives with it. Table entries
   added by paste or `libraryInsert` are not removed when the batch is undone — as layers today
   (§12).
2. **Dimension styles**: the `QString` setters already exist (`lc_dimstyle.cpp` ·
   `LC_DimStyle::DimensionLine::setLineType(const QString&)`,
   `ExtensionLine::setLineTypeFirst/Second(const QString&)`) and the filter already reads and
   writes groups 345/347/348 by name through `findLineTypeHandleToWrite()`. What is missing:
   - `rs_dimension.cpp` · `RS_Dimension::getPenDimensionLine()` / `getPenExtensionLine(first)`
     build the sub-entity pen from the **enum cache only**
     (`RS_Pen result(getDimensionLineColor(), getDimensionLineWidth(), getDimensionLineType())`,
     where `getDimensionLineType()` returns `m_dimStyleTransient->dimensionLine()->lineType()`),
     so a style whose `DIMLTYPE` names a custom type draws its dimension and extension lines as
     the nearest built-in — solid for vendor patterns — on screen, on paper and in every export,
     while the file round-trips the name correctly. They must set the name on the pen:
     `result.setLineTypeName(dimensionLine()->lineTypeName())`, resp.
     `extensionLine()->lineTypeFirstRaw()` / `lineTypeSecondRaw()`, calling the setter only when
     the raw name is non-empty so that the `""` default of `DIMLTEX1/2` keeps the enum-built pen.
     This is the cheapest and most visible win of the ladder — the name is already persisted —
     so it lands here, beside the dim-style work it belongs to (T10 leaves dimension pens to this
     phase); pulling it forward into PR-1, next to §4 step 4b, is a two-line change if that PR has
     room. `lc_dimensionsbuilder.cpp` · `LC_DimensionsBuilder::getPen*Line()` (dim-style preview,
     hard-coded `RS2::LineByBlock`) is compiled by neither build system and is left alone.
   - `LC_DlgDimStyleManager`'s three linetype combos move to the name-based `QG_LineTypeBox` of
     Phase 3 and call the `QString` setters (Phase 3 fallout, landed here if not already).
   - `lc_dimstyletovariablesmapper.cpp`: the mapper asymmetry — `$DIMLTYPE` is **read** as an int
     (`dimLineFromVars`, `getInt`) and **written** as the enum under code 70 (`dimLine2Vars`),
     while `$DIMLTEX1/2` are read as strings (`extensionLineFromVars`) but written from the enum
     accessors `lineTypeFirst()`/`lineTypeSecond()` under code 6 (`extensionLine2Vars`, already
     flagged `// check code, it's string...`).
   - **Out of scope here** (pre-existing, built-in names included): nothing sets
     `DRW_Dimstyle::dimltypeH`/`dimltex1H`/`dimltex2H`, so a DWG 2007+ save writes null dim-style
     linetype handles (§0.1). Stated in the PR, not fixed: it needs the DWG handle map and is its
     own change (T12).
3. Plugins: expose names through `DPI::LTYPE` (string, already exists); `DPI::LineType` enum
   left as is.
4. **JWW**: nothing degrades, because nothing can be written — export is **unreachable**.
   `libraries/jwwlib/src/dl_jww.cpp` · `DL_Jww::out()` returns `NULL`, so
   `rs_filterjww.cpp` · `RS_FilterJWW::fileExport` always fails at its `if (!dw)` gate, and
   `JWW_WRITE_SUPPORT` is stripped in `librecad/src/src.pro` (`DEFINES -= JWW_WRITE_SUPPORT`) and
   never defined by CMake.
   Import maps the format's pen styles through `RS_FilterJWW::nameToLineType()`, a fixed table of
   built-in names, so no custom name can arise there either; the dead export path keeps
   `lineTypeToName(pen.getLineType())` (`RS_FilterJWW::getEntityAttributes`). The sentence for the
   PR is therefore "JWW is
   import-only in practice and can only produce built-in names", not "custom names degrade to
   the nearest built-in". The existing name-table agreement test
   (`dxf_roundtrip_tests.cpp` · the `RS_FilterJWW::nameToLineType` loop of *"Every DXF linetype
   name LibreCAD writes maps back to the same RS2::LineType"*) stays the oracle; a JWW round-trip
   test is not possible.
5. **Tests**, one per path (`librecad_tests`, `[linetype][parity]`), each against a source graphic
   whose table holds `VENDOR_TAB` `{20,-20}`:
   - **copy/paste**: `LC_CopyUtils::copy(ref, entities, &src)` of a line whose pen carries
     `setLineTypeName("VENDOR_TAB")` → `RS_CLIPBOARD->getGraphic()->findLineType("VENDOR_TAB")->pattern == {20,-20}`;
     `LC_CopyUtils::paste(RS_PasteData{…}, &dest, ctx)` into a fresh `RS_Graphic` → the pasted
     clone's `getPen(false).getLineTypeName() == "VENDOR_TAB"` **and**
     `dest.findLineType("VENDOR_TAB")->pattern == {20,-20}`, and `dest`'s export writes
     `49`=20,−20, not a `73`=0 marker. The destination-merge assertion is repeated on
     `RS_Modification::libraryInsert`, the stable path (`paste()` still carries its
     temporary-implementation FIXME).
   - **layer duplication**: duplicate a layer whose pen names `VENDOR_TAB` through
     `lc_layertreewidget.cpp` · `LC_LayerTreeWidget::copyLayerAttributes` — the field-by-field
     pen rebuild that drops the name, fixed in §4 step 4 — →
     `copy->getPen().getLineTypeName() == "VENDOR_TAB"`.
   - **Save Block As**: save a block holding a `VENDOR_TAB` line through the action's own
     `RS_ActionBlocksSave::createGraphicForBlock`, re-import →
     `findLineType("VENDOR_TAB")->pattern == {20,-20}`.
   - **layer export**: `LC_LayersExporter::exportLayersToSingleDocument` of a layer carrying a
     `VENDOR_TAB` entity → the same assertion on the export graphic.
   - **XREF**: a host file plus an XREF whose entity uses `VENDOR_TAB` → after import the host
     holds the record with `{20,-20}` (spelled `EXT|VENDOR_TAB` under the namespaced answer to
     step 1's open point) and the host export writes its `49`-values, not a marker.
   - **plugin**: `Doc_plugin_interface::addBlockfromFromdisk` of a file whose entity uses
     `VENDOR_TAB` → `m_docGr->findLineType("VENDOR_TAB")->pattern == {20,-20}`.
   - **dimensions**: a dimension whose effective style has `DIMLTYPE=VENDOR_TAB` draws its
     dimension line with the `[20,-20]` run lengths and its extension lines solid (the headless
     renderer rig of §5), its dimension-line child reports
     `getPen(false).getLineTypeName() == "VENDOR_TAB"`, and a DXF round-trip keeps group 345
     pointing at the `VENDOR_TAB` record.
   - **undo of an attribute change**:
     `RS_AttributesData d; d.pen.setLineTypeName("VENDOR_UTL"); d.changeLineType = true;` applied
     through `RS_Modification::changeAttributes(...)` inside an undo cycle, then
     `CHECK(name == "VENDOR_UTL"); g.undo(); CHECK(name == "VENDOR_TAB"); g.redo(); CHECK(name == "VENDOR_UTL")`.
     It proves the name survives `RS_Entity::clone()` and undo memory, and that
     `RS_AttributesData` needs no extra `QString`: `data.pen` is an `RS_Pen` and already carries
     the name (§4 step 4).

### Commit
```
Carry linetype names through copy/paste, dimension styles and plugins (#1738)
```

---

## 9. Phase 6 (optional, maintainer-gated) — Pattern scale

`$LTSCALE`, group 48 / `$CELTSCALE`, drawing-unit mode (zoom-dependent on screen, consistent on
paper). Closes #1476. Changes how every existing drawing looks, so it ships behind a document
option defaulting to today's behaviour. Not started without an explicit go from maintainers.

---

## 10. Commit Ladder (summary)

| # | Phase | Commit |
|---|---|---|
| 1 | 0 | `test(linetype): red tests and neutral fixture for named linetypes (#1738)` (folded into 2b unless tests-first is preferred) |
| 2a | 1 | `refactor(linetype): move the built-in linetype table out of RS_FilterDXFRW into the engine (#1738)` |
| 2b | 1 | `Keep custom linetype names and patterns across DXF/DWG round-trips (#1738)` |
| 3 | 2 | `Draw custom linetypes from the document's linetype table (#1738)` |
| 4 | 3 | `Offer the document's linetypes in every pen selector (#1738)` |
| 5 | 4 | `Add a Linetypes dialog with .lin import/export (#1738)` |
| 6 | 5 | `Carry linetype names through copy/paste, dimension styles and plugins (#1738)` |
| 7 | 6 (opt.) | `Scale linetype patterns by $LTSCALE and drawing units (#1476)` |

**Why row 2 is split** (dxli's cadence — #2815 and #2823 each landed one family): Phase 1 as
written touches ~20 files including `rs_pen.h`, and two of its concerns are separable, the first
being a pure refactor with no behaviour change and existing test coverage. **2a and 2b are two
commits of PR-1** (§4), not two PRs: the ladder stays at 5 PRs (+1 optional), one per phase, and
each commit builds green on its own (§1.1), so `git bisect` still lands between them. If
maintainers would rather review the refactor by itself, 2a is the natural standalone PR — the
ladder is then 6 PRs and the PR numbers of §4–§8 shift by one.

- **2a** moves the 35-record metrics table and `nameToLineType()`/`lineTypeToName()` out of
  `RS_FilterDXFRW` into `LC_LineTypeNames` (§4 step 3); the filter keeps thin forwarders, and
  `lc_dimstyle.cpp`, `lc_dimstyletovariablesmapper.cpp`, `rs_filterdxf1.cpp` and
  `rs_filtershp.cpp` include the engine header instead — which also removes today's
  engine→filters include inversion. Gate: the existing `[linetype]` tests plus T6; one new `.cpp`
  in both build lists. It must land **before** 2b, because `RS_Pen` (engine) needs
  `lineTypeToName()` without including `rs_filterdxfrw.h`. It does **not** fold
  `rs_linetypepattern.cpp` into the table: the screen run lengths differ from the DXF metrics on
  purpose (§5 step 2, §12).
- **2b** is the rest of Phase 1 — `RS_Pen::m_lineTypeName` and the copy idioms, the filters
  reading and writing the raw name, the marker records, the document-owned `LC_LineTypeList` —
  and is the commit that pays the full `rs_pen.h` rebuild (§1.1, §11).

If maintainers find 2b still too large, the natural second cut is the table itself: its first
consumers are the renderers (row 3) and the UI (row 4), and the round-trip goal is already
reached without it by pushing marker `DRW_LType` records through the existing
`LC_DwgAdvancedMetadata::addLineTypeName()` archive that `writeLTypes()` re-emits — T1's
`findLineType` assertion and T7's list assertions would then move to row 3 with the table.

Every commit builds green under both systems and keeps every test lane green; each PR is
rebased on `origin/master` the day it opens, carries its evidence (base-vs-fix table) and adds
no compiler warnings.

---

## 11. Risks & Mitigations

| Risk | Severity | Mitigation |
|---|---|---|
| Maintainers prefer another architecture (global `.lin` lists, ids instead of names) | High | Phase 0 gate before any product code; §2 offers both options |
| Renderer pen cache ignores the name → custom types drawn with the previous `QPen` | High | name in `isSameAs()`/`operator==`, enum first — see the render-cost row below |
| Renderer or view outlives — or predeceases — its document → dangling table pointer or stale pattern cache | High | revision counter on the table, no listener below the UI layer |
| Any pen edit through the enum-only combo before Phase 3 rewrites a custom name to its cached built-in | Medium | "Known and accepted" in PR-1/PR-2 plus an open+save test; Phase 3's combo removes it |
| Render-loop cost of a `QString` in `RS_Pen`: copied per entity per frame, compared in the #1922 pen cache | Low–Medium | null `QString` for built-ins; enum, width and colour compared first; PR-1 measures, target ≤ 5 % |
| LTYPE record written twice (table + metadata archive) | Medium | `writeLTypes()` emits from the table; the metadata loop only what the table lacks; test "record written once" |
| DWG: name without record → entity ByLayer silently | Medium | synthesise records for every referenced name before layers/entities; DWG round-trip test under `DWGSUPPORT` |
| Performance: thousands of 1 px dashes with tiny patterns | Medium | device-mm units in Phase 1; minimum on-screen period → solid (Phase 2) |
| Integer persistence (settings, `.lcpp`, `$DIMLTYPE`, plugins) breaks | Medium | enum never renumbered; name only where a document exists; `.lcpp` 5th field optional |
| CMake/qmake drift | Medium | every commit touches both lists; fork CI (Pixi ×5) before the upstream PR |
| `rs_filterdxfrw.cpp` reformatted between rebase and merge | Low | rebase the day of the PR; anchor by symbol |
| Full rebuild cost of `rs_pen.h` (~820 TUs) | Low | one-off in Phase 1; later phases incremental |
| Case-insensitive collisions (`Dashed` vs `DASHED` in one file) | Low | list keys fold case; first record wins; test T4 |

The long mitigations in full, keyed by the risk wording of the table:

- **Maintainers prefer another architecture.** Phase 0 before any product code; §2 offers both
  options with trade-offs; Phase 1 is shaped so the table can be seeded from global lists later
  without touching the pen.
- **Renderer pen cache ignores the name.** `isSameAs()`/`operator==` include the name (Phase 1),
  enum first and the name last — see the render-cost row of the table; pixel test with two custom
  types back to back (Phase 2).
- **Renderer or view outlives — or predeceases — its document.** The failure is a dangling table
  pointer, or a pattern cache left stale by a table edit (`qc_mdiwindow.cpp` · `~QC_MDIWindow` frees
  the document before its child view and renderer, the #2764 class of bug).
  - No listener below the UI layer, because nothing could unregister one: no renderer has a
    lifecycle hook, `lc_graphicviewportrenderer.cpp` · the ctor captures `m_graphic` from the
    viewport once, a print-preview window shares its parent's document (`qc_applicationwindow.cpp` ·
    the preview `QC_MDIWindow` is built on `parent->getDocument()`), and five renderers are stack
    objects.
  - Instead `LC_LineTypeList` exposes a monotonic `unsigned revision()` bumped by every mutator, and
    the renderer's `m_patternCache` keeps `m_patternCacheGraphic` plus `m_patternCacheRevision` and
    drops it at the start of `render()`/`setupPainter()` when `m_graphic` is null, differs, or the
    revision moved (the `getBackgroundColor()` null-guard shape) — which also covers a `clear()`
    that never notifies (`rs_layerlist.cpp` · `RS_LayerList::clear`); a renderer-less painter
    degrades to the static enum table, per T-R4.
- **Any pen edit through the enum-only combo before Phase 3.** PR-1 and PR-2 state it under "Known
  and accepted" and a test proves open+save without UI keeps names; the Phase 3 list-driven combo
  with the temporary row removes it.
  - Not "No Pen": option A never yields −1, because every enum `RS_FilterDXFRW::nameToLineType`
    returns — down to its `SolidLine` fallback — has a row in `QG_LineTypeBox::init`; the −1 path
    needs an enum with no row, `NoPen` today (cf. §0.4).
- **Render-loop cost of a `QString` in `RS_Pen`.** Where the cost sits: copied per entity per frame
  (`rs_entity.cpp` · `RS_Entity::getPenResolved` returns by value; `lc_graphicviewrenderer.cpp` ·
  `LC_GraphicViewRenderer::setPenForEntity` copies it again into `originalPen` and calls
  `updateBy`) and compared in the #1922 pen cache (`rs_pen.h` · `RS_Pen::isSameAs`);
  `RS_Color::m_colorName` is the precedent, but it is outside equality and outside rendering.
  - The member stays a null `QString` for every built-in enum (`getLineTypeName()` returns
    `lineTypeToName(enum)` when null), so ordinary pens copy a null `d` pointer with no refcount
    traffic.
  - `operator==`/`isSameAs` test enum, width and colour first and reach the names only when the
    enums match and at least one is non-null, then compare with exact `QString::operator==`
    (size + memcmp) on the already-folded spelling — never `Qt::CaseInsensitive` per entity.
  - Built-in names come from static `QStringLiteral`s in `LC_LineTypeNames` (today `lineTypeToName`
    builds a fresh `QString` from a `const char*` on every call), so `setLineType(enum)` never
    allocates; the filters hand the pen the table entry's own `QString`, so one shared buffer per
    spelling.
  - PR-1 reports `getPenTime`/`setPenTime`/`painterSetPenTime` base vs fix on a generated
    ~100k-entity drawing (drop the underscore in `#define DEBUG_RENDERING_`,
    `lib/gui/render/lc_graphicviewportrenderer.h`), target ≤ 5 %.

## 12. Explicit Non-Goals

- Rendering shapes/text of complex linetypes; `.shx` support.
- `$PSLTSCALE` / paper-space scaling; continuous pattern across polyline vertices (PLINEGEN).
- Backport to 2.2.1.
- Any vendor/machine-specific name or rule in upstream code (vendor files are local acceptance
  fixtures only).
- Undo for linetype table edits (matches layers today).
- Changing the on-screen look of the existing built-in families.
