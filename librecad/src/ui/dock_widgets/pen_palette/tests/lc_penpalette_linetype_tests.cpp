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

// A pen carries its line type by name, and the palette holds built-in line
// types only. Selecting and filling by a pen must therefore compare names: the
// enum of a drawing's line type stands in for it and must not pass for the
// built-in it resembles.

#include <array>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <QList>
#include <QString>

#include "lc_actiontestsupport.h"
#include "lc_linetype.h"
#include "lc_penpalettewidget.h"
#include "qg_colorbox.h"
#include "qg_linetypebox.h"
#include "qg_widthbox.h"
#include "rs_color.h"
#include "rs_entity.h"
#include "rs_graphic.h"
#include "rs_layer.h"
#include "rs_layerlist.h"
#include "rs_line.h"
#include "rs_pen.h"
#include "rs_vector.h"

namespace {

constexpr int ENTITY_COUNT = 5;

// An imported record as the drawing's table holds it.
void addLineType(RS_Graphic& graphic, const QString& name, const std::vector<double>& pattern) {
    auto* entry = new LC_LineType(name);
    entry->pattern = pattern;
    entry->origin = LC_LineType::Origin::Imported;
    graphic.addLineType(entry);
}

RS_Pen namedPen(const QString& name) {
    RS_Pen pen(RS_Color(Qt::black), RS2::Width00, RS2::SolidLine);
    pen.setLineTypeName(name);
    return pen;
}

RS_Pen solidPen() {
    return RS_Pen(RS_Color(Qt::black), RS2::Width00, RS2::SolidLine);
}

RS_Pen dashPen() {
    return RS_Pen(RS_Color(Qt::black), RS2::Width00, RS2::DashLine);
}

RS_Line* addLine(RS_Graphic& graphic, const RS_Pen& pen, RS_Layer* layer) {
    auto* line = new RS_Line(&graphic, RS_Vector(0.0, 0.0), RS_Vector(10.0, 0.0));
    line->setPen(pen);
    if (layer != nullptr) {
        line->setLayer(layer);
    }
    graphic.addEntity(line);
    return line;
}

// A drawing with VENDOR_TAB and the five lines the palette cases select.
struct PaletteFixture {
    const bool m_qtReady{lc::test::application() != nullptr};
    RS_Graphic m_graphic;
    lc::test::TestGraphicView m_view;
    LC_PenPaletteWidget m_palette{QStringLiteral("Pen Palette"), nullptr};
    std::array<RS_Line*, ENTITY_COUNT> m_entities{};
    RS_Layer* m_vendorLayer{nullptr};

    PaletteFixture() {
        m_graphic.initForNewDocument();
        addLineType(m_graphic, QStringLiteral("VENDOR_TAB"), {10.0, -5.0});

        m_vendorLayer = new RS_Layer(QStringLiteral("VENDOR_LAYER"));
        m_vendorLayer->setPen(namedPen(QStringLiteral("VENDOR_TAB")));
        m_graphic.getLayerList()->add(m_vendorLayer);

        m_view.setDocument(&m_graphic);
        m_palette.setGraphicView(&m_view);

        // e1 carries a drawing's line type and attributes of its own.
        RS_Pen vendorPen = namedPen(QStringLiteral("VENDOR_TAB"));
        vendorPen.setColor(RS_Color(Qt::red));
        vendorPen.setWidth(RS2::Width02);
        m_entities[0] = addLine(m_graphic, vendorPen, nullptr);
        m_entities[1] = addLine(m_graphic, solidPen(), nullptr);
        m_entities[2] = addLine(m_graphic, dashPen(), nullptr);
        m_entities[3] = addLine(m_graphic, namedPen(QStringLiteral("DASHED")), nullptr);
        // e5 keeps the default By Layer pen; its layer decides.
        m_entities[4] = addLine(m_graphic, RS_Pen(), m_vendorLayer);
    }
};

void deselectAll(PaletteFixture& fixture) {
    fixture.m_graphic.getSelection()->clear();
}

void selectOnly(PaletteFixture& fixture, const int index) {
    deselectAll(fixture);
    fixture.m_graphic.select(QList<RS_Entity*>{fixture.m_entities[index]}, true);
}

std::array<bool, ENTITY_COUNT> selectedFlags(const PaletteFixture& fixture) {
    std::array<bool, ENTITY_COUNT> result{};
    for (int i = 0; i < ENTITY_COUNT; ++i) {
        result[i] = fixture.m_entities[i]->isSelected();
    }
    return result;
}

} // namespace

TEST_CASE("Selecting by the editor's attributes pen compares effective line type names",
          "[gui][linetype][palette]") {
    PaletteFixture fixture;

    // Continuous: only the unnamed solid pen matches, not the drawing's line type.
    deselectAll(fixture);
    fixture.m_palette.cbType->setLineType(RS2::SolidLine);
    fixture.m_palette.cbWidth->setCurrentIndex(0);
    fixture.m_palette.cbColor->setCurrentIndex(0);
    fixture.m_palette.selectEntitiesWithAttributesPenByPenEditor();
    auto flags = selectedFlags(fixture);
    CHECK_FALSE(flags[0]);
    CHECK(flags[1]);
    CHECK_FALSE(flags[2]);
    CHECK_FALSE(flags[3]);
    CHECK_FALSE(flags[4]);

    // Dash: the unnamed DashLine pen and the pen named DASHED are one line type.
    deselectAll(fixture);
    fixture.m_palette.cbType->setLineType(RS2::DashLine);
    fixture.m_palette.selectEntitiesWithAttributesPenByPenEditor();
    flags = selectedFlags(fixture);
    CHECK_FALSE(flags[0]);
    CHECK_FALSE(flags[1]);
    CHECK(flags[2]);
    CHECK(flags[3]);
    CHECK_FALSE(flags[4]);
}

TEST_CASE("Selecting by the editor's drawing pen follows the resolved names",
          "[gui][linetype][palette]") {
    PaletteFixture fixture;

    deselectAll(fixture);
    fixture.m_palette.cbType->setLineType(RS2::SolidLine);
    fixture.m_palette.cbWidth->setCurrentIndex(0);
    fixture.m_palette.cbColor->setCurrentIndex(0);
    fixture.m_palette.selectEntitiesWithDrawingPenByPenEditor();
    const auto flags = selectedFlags(fixture);
    CHECK_FALSE(flags[0]);
    CHECK(flags[1]);
    CHECK_FALSE(flags[2]);
    CHECK_FALSE(flags[3]);
    // The By Layer pen resolves to the layer's VENDOR_TAB.
    CHECK_FALSE(flags[4]);
}

TEST_CASE("Filling from an entity keeps a drawing's line type out of the editor",
          "[gui][linetype][palette]") {
    PaletteFixture fixture;

    // e1's line type has no built-in row, so the editor stays on "unchanged".
    selectOnly(fixture, 0);
    fixture.m_palette.cbType->setLineType(RS2::DashLine);
    fixture.m_palette.fillPenEditorBySelectedEntityAttributesPen();
    CHECK(fixture.m_palette.cbType->currentIndex() == 0);
    CHECK(fixture.m_palette.cbWidth->currentIndex() > 0);
    CHECK(fixture.m_palette.cbWidth->getWidth() == RS2::Width02);
    CHECK(fixture.m_palette.cbColor->currentIndex() > 0);
    CHECK(fixture.m_palette.cbColor->getColor() == RS_Color(Qt::red));

    // A built-in DashLine pen fills the Dash row.
    selectOnly(fixture, 2);
    fixture.m_palette.cbType->setLineType(RS2::SolidLine);
    fixture.m_palette.fillPenEditorBySelectedEntityAttributesPen();
    CHECK(fixture.m_palette.cbType->currentIndex() > 0);
    CHECK(fixture.m_palette.cbType->getLineType() == RS2::DashLine);

    // A pen named DASHED selects the same built-in row.
    selectOnly(fixture, 3);
    fixture.m_palette.cbType->setLineType(RS2::SolidLine);
    fixture.m_palette.fillPenEditorBySelectedEntityAttributesPen();
    CHECK(fixture.m_palette.cbType->currentIndex() > 0);
    CHECK(fixture.m_palette.cbType->getLineType() == RS2::DashLine);
}

TEST_CASE("Filling from a drawing pen and from the active layer keeps the unchanged row",
          "[gui][linetype][palette]") {
    PaletteFixture fixture;

    // e5 is By Layer over VENDOR_TAB, so its drawing pen names a drawing line type.
    selectOnly(fixture, 4);
    fixture.m_palette.cbType->setLineType(RS2::SolidLine);
    fixture.m_palette.fillPenEditorBySelectedEntityDrawingPen();
    CHECK(fixture.m_palette.cbType->currentIndex() == 0);

    deselectAll(fixture);
    fixture.m_graphic.activateLayer(fixture.m_vendorLayer);
    fixture.m_palette.cbType->setLineType(RS2::SolidLine);
    fixture.m_palette.fillPenEditorByActiveLayer();
    CHECK(fixture.m_palette.cbType->currentIndex() == 0);
}

TEST_CASE("Applying the editor to the active layer after filling from it keeps its line type",
          "[gui][linetype][palette]") {
    PaletteFixture fixture;
    deselectAll(fixture);
    fixture.m_graphic.activateLayer(fixture.m_vendorLayer);

    fixture.m_palette.fillPenEditorByActiveLayer();
    REQUIRE(fixture.m_palette.cbType->currentIndex() == 0);

    fixture.m_palette.cbColor->setColor(RS_Color(Qt::blue));
    REQUIRE(fixture.m_palette.cbColor->currentIndex() > 0);
    fixture.m_palette.applyEditorPenToActiveLayer();

    const RS_Pen pen = fixture.m_vendorLayer->getPen();
    CHECK(pen.hasLineTypeName());
    CHECK(pen.getLineTypeName() == QStringLiteral("VENDOR_TAB"));
    CHECK(pen.getColor() == RS_Color(Qt::blue));
}
