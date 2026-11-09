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

// The linetype combo lists built-ins only, so the pen widget hands back a name
// it was given while the combo still shows that pen's linetype.

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <QString>

#include "lc_actiontestsupport.h"
#include "lc_linetype.h"
#include "qg_linetypebox.h"
#include "qg_widgetpen.h"
#include "rs_color.h"
#include "rs_graphic.h"
#include "rs_layer.h"
#include "rs_line.h"
#include "rs_pen.h"

namespace {

RS_Pen namedPen() {
    RS_Pen pen(RS_Color(Qt::red), RS2::Width05, RS2::SolidLine);
    pen.setLineTypeName(QStringLiteral("VENDOR_TAB"));
    return pen;
}

// An imported record as the filter will make it from an LTYPE entry.
void addLineType(RS_Graphic& graphic, const QString& name, const std::vector<double>& pattern) {
    auto* entry = new LC_LineType(name);
    entry->pattern = pattern;
    entry->origin = LC_LineType::Origin::Imported;
    graphic.addLineType(entry);
}

QG_LineTypeBox* lineTypeBox(QG_WidgetPen& widget) {
    return widget.findChild<QG_LineTypeBox*>();
}

} // namespace

TEST_CASE("The pen widget keeps a linetype name its combo cannot show",
          "[gui][pen][linetype]") {
    (void)lc::test::application();
    const RS_Pen named = namedPen();
    REQUIRE(named.hasLineTypeName());
    QG_WidgetPen widget;

    SECTION("set from a pen") {
        widget.setPen(named, false, false, QString());
        const RS_Pen pen = widget.getPen();
        CHECK(pen.getLineTypeId() == named.getLineTypeId());
        CHECK(pen.getLineTypeName() == QStringLiteral("VENDOR_TAB"));
        CHECK(pen == named);
    }

    SECTION("set from an entity") {
        RS_Line line{nullptr, RS_Vector{0.0, 0.0}, RS_Vector{1.0, 0.0}};
        line.setPen(named);
        widget.setPen(&line, nullptr, QString());
        const RS_Pen pen = widget.getPen();
        CHECK(pen.getLineTypeId() == named.getLineTypeId());
        CHECK(pen.getLineTypeName() == QStringLiteral("VENDOR_TAB"));
    }

    SECTION("a ByLayer entity on a named layer stays ByLayer") {
        RS_Graphic graphic;
        graphic.initForNewDocument();
        auto *layer = new RS_Layer(QStringLiteral("NAMED"));
        layer->setPen(named);
        graphic.addLayer(layer);
        RS_Line line{&graphic, RS_LineData(RS_Vector{0.0, 0.0}, RS_Vector{1.0, 0.0})};
        line.setLayer(layer);
        line.setPen(RS_Pen(RS2::FlagByLayer, RS2::WidthByLayer, RS2::LineByLayer));
        REQUIRE(line.getPen(true).getLineTypeId() == named.getLineTypeId());

        widget.setPen(&line, layer, QString());
        const RS_Pen pen = widget.getPen();
        CHECK(pen.getLineType() == RS2::LineByLayer);
        CHECK_FALSE(pen.hasLineTypeName());
    }

    SECTION("a new colour keeps the name") {
        widget.setPen(named, false, false, QString());
        widget.cbColor->setColor(RS_Color(Qt::blue));
        const RS_Pen pen = widget.getPen();
        CHECK(pen.getColor() == RS_Color(Qt::blue));
        CHECK(pen.getLineTypeId() == named.getLineTypeId());
    }

    SECTION("a new linetype drops the name") {
        widget.setPen(named, false, false, QString());
        widget.cbLineType->setLineType(RS2::DashLine);
        const RS_Pen pen = widget.getPen();
        CHECK_FALSE(pen.hasLineTypeName());
        CHECK(pen.getLineType() == RS2::DashLine);
    }

    SECTION("picking another linetype and back keeps the name") {
        widget.setPen(named, false, false, QString());
        widget.cbLineType->setLineType(RS2::DashLine);
        widget.cbLineType->setLineType(RS2::SolidLine);
        CHECK(widget.getPen().getLineTypeId() == named.getLineTypeId());
    }
}

TEST_CASE("The pen widget returns a pen without a name as it was given",
          "[gui][pen][linetype]") {
    (void)lc::test::application();
    const RS_Pen plain(RS_Color(Qt::red), RS2::Width05, RS2::DashLine);

    QG_WidgetPen widget;
    widget.setPen(plain, false, false, QString());
    const RS_Pen pen = widget.getPen();
    CHECK(pen == plain);
    CHECK_FALSE(pen.hasLineTypeName());
}

TEST_CASE("The pen widget offers the line types of a drawing", "[gui][pen][linetype]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, "VENDOR_TAB", {10.0, -5.0});
    const RS_Pen named = namedPen();

    QG_WidgetPen widget;
    widget.setLineTypesOf(&graphic);
    widget.setPen(named, false, false, QString());

    QG_LineTypeBox* combo = lineTypeBox(widget);
    REQUIRE(combo != nullptr);
    CHECK(combo->currentText() == QStringLiteral("VENDOR_TAB"));
    const RS_Pen pen = widget.getPen();
    CHECK(pen.getLineTypeName() == QStringLiteral("VENDOR_TAB"));
    CHECK(pen.getLineTypeId() == named.getLineTypeId());
}

TEST_CASE("The pen widget returns the line type the user chose", "[gui][pen][linetype]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, "VENDOR_TAB", {10.0, -5.0});
    const RS_Pen plain(RS_Color(Qt::red), RS2::Width05, RS2::DashLine);

    QG_WidgetPen widget;
    widget.setLineTypesOf(&graphic);
    widget.setPen(plain, false, false, QString());
    QG_LineTypeBox* combo = lineTypeBox(widget);
    REQUIRE(combo != nullptr);

    const int vendorRow = combo->findText(QStringLiteral("VENDOR_TAB"));
    REQUIRE(vendorRow >= 0);
    combo->setCurrentIndex(vendorRow);
    emit combo->activated(vendorRow);
    RS_Pen pen = widget.getPen();
    CHECK(pen.hasLineTypeName());
    CHECK(pen.getLineTypeName() == QStringLiteral("VENDOR_TAB"));

    const int dashRow = combo->findData(static_cast<int>(RS2::DashLine));
    REQUIRE(dashRow >= 0);
    combo->setCurrentIndex(dashRow);
    emit combo->activated(dashRow);
    pen = widget.getPen();
    CHECK_FALSE(pen.hasLineTypeName());
    CHECK(pen.getLineType() == RS2::DashLine);
}

TEST_CASE("A redefined built-in comes back by name from the pen widget", "[gui][pen][linetype]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, "DASHED", {64.0, -32.0});
    const RS_Pen plain(RS_Color(Qt::red), RS2::Width05, RS2::SolidLine);

    QG_WidgetPen widget;
    widget.setLineTypesOf(&graphic);
    widget.setPen(plain, false, false, QString());
    QG_LineTypeBox* combo = lineTypeBox(widget);
    REQUIRE(combo != nullptr);

    const int dashRow = combo->findData(static_cast<int>(RS2::DashLine));
    REQUIRE(dashRow >= 0);
    combo->setCurrentIndex(dashRow);
    emit combo->activated(dashRow);
    const RS_Pen pen = widget.getPen();
    CHECK(pen.hasLineTypeName());
    CHECK(pen.getLineTypeName() == QStringLiteral("DASHED"));
    CHECK(pen.getLineType() == RS2::DashLine);
}

TEST_CASE("The pen widget without a drawing is unchanged", "[gui][pen][linetype]") {
    (void)lc::test::application();
    const RS_Pen named = namedPen();

    QG_WidgetPen reference;
    reference.setPen(named, false, false, QString());
    QG_LineTypeBox* referenceCombo = lineTypeBox(reference);
    REQUIRE(referenceCombo != nullptr);

    QG_WidgetPen widget;
    widget.setLineTypesOf(nullptr);
    widget.setPen(named, false, false, QString());
    QG_LineTypeBox* combo = lineTypeBox(widget);
    REQUIRE(combo != nullptr);
    CHECK(combo->count() == referenceCombo->count());
    const RS_Pen pen = widget.getPen();
    CHECK(pen.getLineTypeName() == QStringLiteral("VENDOR_TAB"));
    CHECK(pen.getLineTypeId() == named.getLineTypeId());
}

TEST_CASE("A drawing given after the pen keeps the selection", "[gui][pen][linetype]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, "VENDOR_TAB", {10.0, -5.0});

    QG_WidgetPen widget;
    widget.setPen(namedPen(), false, false, QString());
    QG_LineTypeBox* combo = lineTypeBox(widget);
    REQUIRE(combo != nullptr);
    REQUIRE(combo->findText(QStringLiteral("VENDOR_TAB")) < 0);
    const RS_Pen before = widget.getPen();

    int emissions = 0;
    QObject::connect(&widget, &QG_WidgetPen::penChanged, [&emissions] { ++emissions; });
    widget.setLineTypesOf(&graphic);

    CHECK(emissions == 0);
    CHECK(combo->findText(QStringLiteral("VENDOR_TAB")) >= 0);
    CHECK(widget.getPen() == before);
    CHECK(widget.getPen().getLineTypeId() == before.getLineTypeId());
}
