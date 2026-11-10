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

// The layer dialogs and Modify > Attributes hand the drawing to their pen
// widget, so a layer or a selection can take a line type the drawing defines.
// A dialog given no drawing keeps the built-in rows.

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDialog>
#include <QString>
#include <QTimer>

#include "lc_actiontestsupport.h"
#include "lc_layerdialog_ex.h"
#include "lc_layertreewidget.h"
#include "lc_linetype.h"
#include "qg_dlg_attributes.h"
#include "qg_layerdialog.h"
#include "qg_linetypebox.h"
#include "rs_color.h"
#include "rs_graphic.h"
#include "rs_layer.h"
#include "rs_layerlist.h"
#include "rs_modification.h"
#include "rs_pen.h"

namespace {

// An imported record as the filter will make it from an LTYPE entry.
void addLineType(RS_Graphic& graphic, const QString& name, const std::vector<double>& pattern) {
    auto* entry = new LC_LineType(name);
    entry->pattern = pattern;
    entry->origin = LC_LineType::Origin::Imported;
    graphic.addLineType(entry);
}

RS_Pen namedPen(const QString& name) {
    RS_Pen pen(RS_Color(Qt::red), RS2::Width00, RS2::SolidLine);
    pen.setLineTypeName(name);
    return pen;
}

QG_LineTypeBox* lineTypeBox(QWidget& dialog) {
    return dialog.findChild<QG_LineTypeBox*>();
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

TEST_CASE("The layer dialog offers the drawing's line types", "[gui][layers][linetype]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, "VENDOR_DOT", {0.0, -5.0, 0.0, -5.0, 1.0, -5.0});

    RS_Layer layer(QStringLiteral("LAYER"));
    QG_LayerDialog dialog;
    dialog.setLineTypesOf(&graphic);
    dialog.setLayer(&layer);
    QG_LineTypeBox* combo = lineTypeBox(dialog);
    REQUIRE(combo != nullptr);
    REQUIRE(chooseRow(*combo, QStringLiteral("VENDOR_DOT")) >= 0);
    dialog.updateLayer();
    CHECK(layer.getPen().getLineTypeName() == QStringLiteral("VENDOR_DOT"));
}

TEST_CASE("A layer that names a drawing line type shows it", "[gui][layers][linetype]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, "VENDOR_DOT", {0.0, -5.0, 0.0, -5.0, 1.0, -5.0});

    RS_Layer layer(QStringLiteral("LAYER"));
    const RS_Pen named = namedPen(QStringLiteral("VENDOR_DOT"));
    layer.setPen(named);
    QG_LayerDialog dialog;
    dialog.setLineTypesOf(&graphic);
    dialog.setLayer(&layer);
    QG_LineTypeBox* combo = lineTypeBox(dialog);
    REQUIRE(combo != nullptr);
    CHECK(combo->currentText() == QStringLiteral("VENDOR_DOT"));

    const auto id = layer.getPen().getLineTypeId();
    dialog.updateLayer();
    CHECK(layer.getPen().getLineTypeId() == id);
    CHECK(layer.getPen().getLineTypeName() == QStringLiteral("VENDOR_DOT"));
}

TEST_CASE("The layer dialog without a drawing lists the built-in rows",
          "[gui][layers][linetype]") {
    (void)lc::test::application();
    RS_Layer layer(QStringLiteral("LAYER"));
    QG_LayerDialog dialog;
    dialog.setLayer(&layer);
    QG_LineTypeBox* combo = lineTypeBox(dialog);
    REQUIRE(combo != nullptr);
    QG_LineTypeBox reference;
    reference.init(false, false);
    CHECK(combo->count() == reference.count());

    const int row = combo->findData(static_cast<int>(RS2::DashLine));
    REQUIRE(row >= 0);
    combo->setCurrentIndex(row);
    emit combo->activated(row);
    dialog.updateLayer();
    CHECK_FALSE(layer.getPen().hasLineTypeName());
    CHECK(layer.getPen().getLineType() == RS2::DashLine);
}

TEST_CASE("The attributes dialog assigns a drawing line type", "[gui][layers][linetype]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, "VENDOR_DOT", {0.0, -5.0, 0.0, -5.0, 1.0, -5.0});

    RS_AttributesData data;
    QG_DlgAttributes dialog;
    dialog.setData(&data, *graphic.getLayerList(), &graphic);
    QG_LineTypeBox* combo = lineTypeBox(dialog);
    REQUIRE(combo != nullptr);
    REQUIRE(chooseRow(*combo, QStringLiteral("VENDOR_DOT")) >= 0);
    dialog.updateData();
    CHECK(data.pen.getLineTypeName() == QStringLiteral("VENDOR_DOT"));
    CHECK(data.changeLineType);

    RS_AttributesData untouched;
    QG_DlgAttributes untouchedDialog;
    untouchedDialog.setData(&untouched, *graphic.getLayerList(), &graphic);
    untouchedDialog.updateData();
    CHECK_FALSE(untouched.changeLineType);
}

TEST_CASE("The layer tree dialog offers the drawing's line types",
          "[gui][layers][linetype]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, "VENDOR_DOT", {0.0, -5.0, 0.0, -5.0, 1.0, -5.0});

    LC_LayerDialogEx dialog(nullptr, QStringLiteral("layerDialog"), nullptr, nullptr,
                            graphic.getLayerList());
    dialog.setLineTypesOf(&graphic);
    RS_Layer layer(QStringLiteral("LAYER"));
    dialog.setLayer(&layer);
    QG_LineTypeBox* combo = lineTypeBox(dialog);
    REQUIRE(combo != nullptr);
    REQUIRE(chooseRow(*combo, QStringLiteral("VENDOR_DOT")) >= 0);
    CHECK(dialog.getPen().getLineTypeName() == QStringLiteral("VENDOR_DOT"));
}

TEST_CASE("The layer tree hands its drawing to the layer dialog", "[gui][layers][linetype]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, "VENDOR_DOT", {0.0, -5.0, 0.0, -5.0, 1.0, -5.0});
    lc::test::TestGraphicView view;
    view.setDocument(&graphic);

    LC_LayerTreeWidget tree(nullptr, nullptr);
    tree.setGraphicView(&view);
    tree.show();
    QCoreApplication::processEvents();

    bool answered = false;
    bool hasRow = false;
    QTimer::singleShot(0, &tree, [&] {
        // The add dialog is modal and parented to the tree; reject it whatever
        // the lookup finds so the case cannot hang.
        QWidget* modal = QApplication::activeModalWidget();
        if (modal == nullptr) {
            modal = tree.findChild<QDialog*>();
        }
        if (modal != nullptr) {
            if (QG_LineTypeBox* combo = modal->findChild<QG_LineTypeBox*>(); combo != nullptr) {
                hasRow = combo->findText(QStringLiteral("VENDOR_DOT")) >= 0;
            }
            if (QDialog* dialog = qobject_cast<QDialog*>(modal); dialog != nullptr) {
                dialog->reject();
            }
            else {
                modal->close();
            }
        }
        answered = true;
    });
    tree.addLayer();
    REQUIRE(answered);
    CHECK(hasRow);
}
