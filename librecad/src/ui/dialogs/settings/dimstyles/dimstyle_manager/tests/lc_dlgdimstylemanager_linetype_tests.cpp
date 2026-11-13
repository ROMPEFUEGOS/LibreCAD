/****************************************************************************
**
** This file is part of the LibreCAD project, a 2D CAD program
**
** Copyright (C) 2026 LibreCAD.org
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
**********************************************************************/

// The dimension style manager offers the drawing's line types beside the
// built-ins, keeps a style's own spelling when it opens, writes a drawing row
// back as a name, and hands the drawing's line types to its preview.

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <QString>

#include "lc_actiontestsupport.h"
#include "lc_dimstyle.h"
#include "lc_dimstylepreviewgraphicview.h"
#include "lc_dlgdimstylemanager.h"
#include "lc_linetype.h"
#include "lc_linetypelist.h"
#include "lc_linetypenames.h"
#include "qg_graphicview.h"
#include "qg_linetypebox.h"
#include "rs_graphic.h"

namespace {

// An imported record as the filter will make it from an LTYPE entry.
void addLineType(RS_Graphic& graphic, const QString& name, const std::vector<double>& pattern) {
    auto* entry = new LC_LineType(name);
    entry->pattern = pattern;
    entry->origin = LC_LineType::Origin::Imported;
    graphic.addLineType(entry);
}

void addVendorLineTypes(RS_Graphic& graphic) {
    addLineType(graphic, QStringLiteral("VENDOR_TAB"), {10.0, -5.0});
    addLineType(graphic, QStringLiteral("VENDOR_DOT"), {0.0, -5.0});
    addLineType(graphic, QStringLiteral("VENDOR_DASHDOT"), {20.0, -5.0, 0.0, -5.0});
}

// A style whose dimension line names VENDOR_TAB, whose first extension line
// names VENDOR_DOT and whose second extension line carries no name.
void fillVendorStyle(LC_DimStyle& style) {
    style.dimensionLine()->setLineType(QStringLiteral("VENDOR_TAB"));
    style.extensionLine()->setLineTypeFirst(QStringLiteral("VENDOR_DOT"));
    style.extensionLine()->setLineTypeSecond(QString());
}

QG_LineTypeBox* lineTypeBox(LC_DlgDimStyleManager& manager, const char* name) {
    return manager.findChild<QG_LineTypeBox*>(QString::fromLatin1(name));
}

int chooseRow(QG_LineTypeBox& combo, const QString& text) {
    const int row = combo.findText(text);
    if (row >= 0) {
        combo.setCurrentIndex(row);
        emit combo.activated(row);
    }
    return row;
}

} // namespace

TEST_CASE("The dimension style manager shows a style's drawing line types",
          "[gui][linetype][dialog]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addVendorLineTypes(graphic);

    LC_DimStyle style(QStringLiteral("VENDOR_STYLE"));
    fillVendorStyle(style);
    const QString dimLineRaw = style.dimensionLine()->lineTypeName();
    const QString extLine1Raw = style.extensionLine()->lineTypeFirstRaw();
    const QString extLine2Raw = style.extensionLine()->lineTypeSecondRaw();

    LC_DlgDimStyleManager manager(nullptr, &style, &graphic, RS2::EntityDimLinear);
    QG_LineTypeBox* dimLine = lineTypeBox(manager, "cbDimLineLineType");
    QG_LineTypeBox* extLine1 = lineTypeBox(manager, "cbExtLineType1");
    QG_LineTypeBox* extLine2 = lineTypeBox(manager, "cbExtLineType2");
    REQUIRE(dimLine != nullptr);
    REQUIRE(extLine1 != nullptr);
    REQUIRE(extLine2 != nullptr);

    CHECK(dimLine->currentText() == QStringLiteral("VENDOR_TAB"));
    CHECK(extLine1->currentText() == QStringLiteral("VENDOR_DOT"));
    CHECK(extLine2->currentText() == QStringLiteral("By Layer"));

    // Filling happens before the handlers are connected, so opening rewrites nothing.
    CHECK(style.dimensionLine()->lineTypeName() == dimLineRaw);
    CHECK(style.dimensionLine()->lineType() == LC_LineTypeNames::nameToLineType(dimLineRaw));
    CHECK(style.extensionLine()->lineTypeFirstRaw() == extLine1Raw);
    CHECK(style.extensionLine()->lineTypeFirst() == LC_LineTypeNames::nameToLineType(extLine1Raw));
    CHECK(style.extensionLine()->lineTypeSecondRaw() == extLine2Raw);
}

TEST_CASE("The dimension style manager writes a chosen drawing line type by name",
          "[gui][linetype][dialog]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addVendorLineTypes(graphic);

    LC_DimStyle style(QStringLiteral("VENDOR_STYLE"));
    fillVendorStyle(style);

    LC_DlgDimStyleManager manager(nullptr, &style, &graphic, RS2::EntityDimLinear);
    QG_LineTypeBox* dimLine = lineTypeBox(manager, "cbDimLineLineType");
    QG_LineTypeBox* extLine1 = lineTypeBox(manager, "cbExtLineType1");
    QG_LineTypeBox* extLine2 = lineTypeBox(manager, "cbExtLineType2");
    REQUIRE(dimLine != nullptr);
    REQUIRE(extLine1 != nullptr);
    REQUIRE(extLine2 != nullptr);

    REQUIRE(chooseRow(*dimLine, QStringLiteral("VENDOR_DASHDOT")) >= 0);
    CHECK(style.dimensionLine()->lineTypeName() == QStringLiteral("VENDOR_DASHDOT"));
    CHECK(style.dimensionLine()->lineType() == LC_LineTypeNames::nameToLineType(QStringLiteral("VENDOR_DASHDOT")));

    // A built-in row still writes what the enum setter writes today.
    REQUIRE(chooseRow(*extLine1, QStringLiteral("Dash")) >= 0);
    CHECK(style.extensionLine()->lineTypeFirst() == RS2::DashLine);
    CHECK(style.extensionLine()->lineTypeFirstRaw() == LC_LineTypeNames::lineTypeToName(RS2::DashLine));

    CHECK(style.extensionLine()->lineTypeSecondRaw().isEmpty());
}

TEST_CASE("The dimension style manager keeps a line type the drawing lacks",
          "[gui][linetype][dialog]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addVendorLineTypes(graphic);

    LC_DimStyle style(QStringLiteral("VENDOR_STYLE"));
    style.dimensionLine()->setLineType(QStringLiteral("VENDOR_GONE"));

    LC_DlgDimStyleManager manager(nullptr, &style, &graphic, RS2::EntityDimLinear);
    QG_LineTypeBox* dimLine = lineTypeBox(manager, "cbDimLineLineType");
    REQUIRE(dimLine != nullptr);
    // A missing name gets the combo's temporary row, not a built-in row.
    CHECK(dimLine->currentText().startsWith(QStringLiteral("VENDOR_GONE")));
    CHECK(dimLine->currentText() != QStringLiteral("VENDOR_GONE"));
    CHECK(style.dimensionLine()->lineTypeName() == QStringLiteral("VENDOR_GONE"));
}

TEST_CASE("The dimension style preview takes the drawing's line types",
          "[gui][linetype][dialog]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addVendorLineTypes(graphic);

    LC_DimStyle style(QStringLiteral("VENDOR_STYLE"));
    fillVendorStyle(style);

    LC_DlgDimStyleManager manager(nullptr, &style, &graphic, RS2::EntityDimLinear);
    // The preview class carries no Q_OBJECT of its own; find it through its base.
    LC_DimStylePreviewGraphicView* preview = nullptr;
    for (QG_GraphicView* view : manager.findChildren<QG_GraphicView*>()) {
        preview = dynamic_cast<LC_DimStylePreviewGraphicView*>(view);
        if (preview != nullptr) {
            break;
        }
    }
    REQUIRE(preview != nullptr);
    RS_Graphic* previewGraphic = preview->getGraphic();
    REQUIRE(previewGraphic != nullptr);

    LC_LineType* entry = previewGraphic->getLineTypeList().find(QStringLiteral("VENDOR_TAB"));
    REQUIRE(entry != nullptr);
    CHECK(entry->pattern == std::vector<double>{10.0, -5.0});
}
