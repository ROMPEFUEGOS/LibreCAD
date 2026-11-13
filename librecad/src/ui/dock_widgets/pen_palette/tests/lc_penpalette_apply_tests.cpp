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

// Applying the pen editor's pen to the selection must keep both the entities' layer and the
// attributes the editor leaves unchanged: the clones it adds already carry them.

#include <QList>

#include <catch2/catch_test_macros.hpp>

#include "lc_actiontestsupport.h"
#include "lc_penpalettewidget.h"
#include "qg_colorbox.h"
#include "qg_linetypebox.h"
#include "qg_widthbox.h"
#include "rs_graphic.h"
#include "rs_layer.h"
#include "rs_line.h"
#include "rs_pen.h"

namespace {

class PaletteDrawing {
public:
    PaletteDrawing() {
        m_graphic.initForNewDocument();
        m_layerA = new RS_Layer(QStringLiteral("A"));
        m_layerB = new RS_Layer(QStringLiteral("B"));
        m_graphic.addLayer(m_layerA);
        m_graphic.addLayer(m_layerB);
        m_graphic.activateLayer(m_layerB);
        m_graphic.setActivePen(RS_Pen(RS_Color(Qt::green), RS2::Width05, RS2::DotLine));
        m_line = new RS_Line(&m_graphic, RS_Vector(0, 0), RS_Vector(10, 0));
        m_line->setLayer(m_layerA);
        m_line->setPen(RS_Pen(RS_Color(Qt::red), RS2::Width02, RS2::DashLine));
        m_graphic.addEntity(m_line);
    }

    RS_Graphic m_graphic;
    RS_Layer* m_layerA{nullptr};
    RS_Layer* m_layerB{nullptr};
    RS_Line* m_line{nullptr};
};

// The batch replaces the selected line with a clone and marks the original deleted.
RS_Line* liveLine(const RS_Graphic& graphic) {
    RS_Line* result = nullptr;
    for (RS_Entity* entity : graphic.getEntityList()) {
        if (entity->rtti() == RS2::EntityLine && !entity->isDeleted()) {
            result = static_cast<RS_Line*>(entity);
        }
    }
    return result;
}

} // namespace

TEST_CASE("Applying the editor color keeps the selected line's layer, width and line type", "[gui][palette]") {
    (void)lc::test::application();
    PaletteDrawing drawing;
    drawing.m_graphic.select(QList<RS_Entity*>{drawing.m_line}, true);
    REQUIRE(drawing.m_graphic.isSingleEntitySelected());

    lc::test::TestGraphicView view;
    view.setDocument(&drawing.m_graphic);
    LC_PenPaletteWidget palette(QStringLiteral("Pen Palette"), nullptr);
    palette.setGraphicView(&view);

    palette.cbWidth->setCurrentIndex(0);
    palette.cbType->setCurrentIndex(0);
    palette.cbColor->setColor(RS_Color(Qt::blue));
    palette.applyEditorPenToSelection();

    CHECK(drawing.m_graphic.getActiveLayer() == drawing.m_layerB);
    RS_Line* changed = liveLine(drawing.m_graphic);
    REQUIRE(changed != nullptr);
    CHECK(changed->getLayer(false) == drawing.m_layerA);
    const RS_Pen pen = changed->getPen(false);
    CHECK(pen.getColor() == RS_Color(Qt::blue));
    CHECK(pen.getWidth() == RS2::Width02);
    CHECK(pen.getLineType() == RS2::DashLine);
}

TEST_CASE("Applying the editor line type keeps the selected line's layer, color and width", "[gui][palette]") {
    (void)lc::test::application();
    PaletteDrawing drawing;
    drawing.m_graphic.select(QList<RS_Entity*>{drawing.m_line}, true);
    REQUIRE(drawing.m_graphic.isSingleEntitySelected());

    lc::test::TestGraphicView view;
    view.setDocument(&drawing.m_graphic);
    LC_PenPaletteWidget palette(QStringLiteral("Pen Palette"), nullptr);
    palette.setGraphicView(&view);

    palette.cbColor->setCurrentIndex(0);
    palette.cbWidth->setCurrentIndex(0);
    palette.cbType->setLineType(RS2::DashDotLine);
    palette.applyEditorPenToSelection();

    CHECK(drawing.m_graphic.getActiveLayer() == drawing.m_layerB);
    RS_Line* changed = liveLine(drawing.m_graphic);
    REQUIRE(changed != nullptr);
    CHECK(changed->getLayer(false) == drawing.m_layerA);
    const RS_Pen pen = changed->getPen(false);
    CHECK(pen.getColor() == RS_Color(Qt::red));
    CHECK(pen.getWidth() == RS2::Width02);
    CHECK(pen.getLineType() == RS2::DashDotLine);
}

TEST_CASE("Undoing an applied editor pen restores the selected line's attributes", "[gui][palette]") {
    (void)lc::test::application();
    PaletteDrawing drawing;
    drawing.m_graphic.select(QList<RS_Entity*>{drawing.m_line}, true);
    REQUIRE(drawing.m_graphic.isSingleEntitySelected());

    lc::test::TestGraphicView view;
    view.setDocument(&drawing.m_graphic);
    LC_PenPaletteWidget palette(QStringLiteral("Pen Palette"), nullptr);
    palette.setGraphicView(&view);

    palette.cbWidth->setCurrentIndex(0);
    palette.cbType->setCurrentIndex(0);
    palette.cbColor->setColor(RS_Color(Qt::blue));
    palette.applyEditorPenToSelection();
    REQUIRE(drawing.m_graphic.undo());

    RS_Line* restored = liveLine(drawing.m_graphic);
    REQUIRE(restored != nullptr);
    CHECK(restored->getLayer(false) == drawing.m_layerA);
    const RS_Pen pen = restored->getPen(false);
    CHECK(pen.getColor() == RS_Color(Qt::red));
    CHECK(pen.getWidth() == RS2::Width02);
    CHECK(pen.getLineType() == RS2::DashLine);
}
