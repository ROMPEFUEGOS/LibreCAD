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

// Quick selection filters by the line type a pen names, not by the built-in its
// enum resolves to. A drawing can define its own line type, and the combos must
// offer it beside the built-in rows.

#include <array>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <QComboBox>
#include <QListWidget>
#include <QString>

#include "lc_actioncontext.h"
#include "lc_actiontestsupport.h"
#include "lc_dlgquickselection.h"
#include "lc_linetype.h"
#include "lc_propertymatchtypedescriptor.h"
#include "qg_linetypebox.h"
#include "rs_color.h"
#include "rs_entity.h"
#include "rs_graphic.h"
#include "rs_layer.h"
#include "rs_layerlist.h"
#include "rs_line.h"
#include "rs_pen.h"
#include "rs_selection.h"
#include "rs_vector.h"

namespace {

constexpr int ENTITY_COUNT = 5;

// An imported record as the filter will make it from an LTYPE entry.
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

// A drawing with VENDOR_TAB and the five lines the line type cases filter.
struct SelectionModel {
    const bool m_qtReady{lc::test::application() != nullptr};
    RS_Graphic m_graphic;
    lc::test::TestGraphicView m_view;
    LC_ActionContext m_context;
    std::array<RS_Entity*, ENTITY_COUNT> m_entities{};

    SelectionModel() {
        m_graphic.initForNewDocument();
        addLineType(m_graphic, QStringLiteral("VENDOR_TAB"), {10.0, -5.0});

        auto* vendorLayer = new RS_Layer(QStringLiteral("VENDOR_LAYER"));
        vendorLayer->setPen(namedPen(QStringLiteral("VENDOR_TAB")));
        m_graphic.getLayerList()->add(vendorLayer);

        m_view.setDocument(&m_graphic);
        m_context.setDocumentAndView(&m_graphic, &m_view);

        // The last line keeps the default By Layer pen; its layer decides.
        m_entities[0] = addLine(m_graphic, namedPen(QStringLiteral("VENDOR_TAB")), nullptr);
        m_entities[1] = addLine(m_graphic, solidPen(), nullptr);
        m_entities[2] = addLine(m_graphic, dashPen(), nullptr);
        m_entities[3] = addLine(m_graphic, namedPen(QStringLiteral("DASHED")), nullptr);
        m_entities[4] = addLine(m_graphic, RS_Pen(), vendorLayer);
    }
};

QString comboObjectName(const QString& propertyName) {
    return propertyName == QStringLiteral("lineType")
        ? QStringLiteral("cbLineType")
        : QStringLiteral("cbLineTypeResolved");
}

// Runs the dialog's own selection call for one property, operator and row.
std::array<bool, ENTITY_COUNT> selectedEntities(SelectionModel& model, const QString& propertyName,
                                                const QString& rowText,
                                                const LC_PropertyMatchOperation operation) {
    LC_DlgQuickSelection dialog(nullptr, &model.m_context,
                                LC_ActionContext::InteractiveInputInfo::NOTNEEDED, nullptr, 0.0, 0.0);

    auto* properties = dialog.findChild<QListWidget*>(QStringLiteral("lvProperties"));
    REQUIRE(properties != nullptr);
    int propertyRow = -1;
    for (int row = 0; row < properties->count(); ++row) {
        if (properties->item(row)->data(Qt::UserRole).toString() == propertyName) {
            propertyRow = row;
            break;
        }
    }
    REQUIRE(propertyRow >= 0);
    properties->setCurrentRow(propertyRow);

    auto* operators = dialog.findChild<QComboBox*>(QStringLiteral("cbOperator"));
    REQUIRE(operators != nullptr);
    const int operationIndex = operators->findData(static_cast<int>(operation));
    REQUIRE(operationIndex >= 0);
    // Step away first so the change emits even when the index was already set.
    operators->setCurrentIndex(operationIndex == 0 ? 1 : 0);
    operators->setCurrentIndex(operationIndex);

    auto* combo = dialog.findChild<QG_LineTypeBox*>(comboObjectName(propertyName));
    REQUIRE(combo != nullptr);
    const int comboRow = combo->findText(rowText);
    CHECK(comboRow >= 0);
    if (comboRow < 0) {
        // The row is missing; the case fails here and its expectations below.
        return {};
    }
    combo->setCurrentIndex(comboRow);
    // A user's choice reports itself; the combo's reported enum follows it.
    emit combo->activated(comboRow);

    RS_Selection::ConditionalSelectionOptions options;
    dialog.getSelectionOptions(options);
    REQUIRE(options.funEntityMatcher);

    std::array<bool, ENTITY_COUNT> result{};
    for (int i = 0; i < ENTITY_COUNT; ++i) {
        result[i] = options.funEntityMatcher(model.m_entities[i]);
    }
    if (options.funCleanup) {
        options.funCleanup();
    }
    return result;
}

} // namespace

TEST_CASE("A pen's effective line type name folds its spelling",
          "[gui][linetype][dialog]") {
    (void)lc::test::application();
    RS_Pen named = namedPen(QStringLiteral("vendor_tab"));
    CHECK(named.getEffectiveLineTypeName() == QStringLiteral("VENDOR_TAB"));

    const RS_Pen solid = solidPen();
    CHECK(solid.getEffectiveLineTypeName() == QStringLiteral("CONTINUOUS"));
}

TEST_CASE("The dialog's line type combos list the drawing's line types",
          "[gui][linetype][dialog]") {
    SelectionModel model;
    LC_DlgQuickSelection dialog(nullptr, &model.m_context,
                                LC_ActionContext::InteractiveInputInfo::NOTNEEDED, nullptr, 0.0, 0.0);

    auto* stored = dialog.findChild<QG_LineTypeBox*>(QStringLiteral("cbLineType"));
    auto* resolved = dialog.findChild<QG_LineTypeBox*>(QStringLiteral("cbLineTypeResolved"));
    REQUIRE(stored != nullptr);
    REQUIRE(resolved != nullptr);
    CHECK(stored->findText(QStringLiteral("VENDOR_TAB")) >= 0);
    CHECK(resolved->findText(QStringLiteral("VENDOR_TAB")) >= 0);
}

TEST_CASE("The entity line type uses the pen's effective name",
          "[gui][linetype][dialog]") {
    SelectionModel model;

    const auto vendorTab = selectedEntities(model, QStringLiteral("lineType"),
                                            QStringLiteral("VENDOR_TAB"), MATCH_OPERATION_EQUALS);
    CHECK(vendorTab[0]);
    CHECK_FALSE(vendorTab[1]);
    CHECK_FALSE(vendorTab[2]);
    CHECK_FALSE(vendorTab[3]);
    CHECK_FALSE(vendorTab[4]);

    const auto continuous = selectedEntities(model, QStringLiteral("lineType"),
                                             QStringLiteral("Continuous"), MATCH_OPERATION_EQUALS);
    CHECK_FALSE(continuous[0]);
    CHECK(continuous[1]);
    CHECK_FALSE(continuous[2]);
    CHECK_FALSE(continuous[3]);
    CHECK_FALSE(continuous[4]);

    // A pen named DASHED and an unnamed DashLine pen are one line type.
    const auto dash = selectedEntities(model, QStringLiteral("lineType"),
                                       QStringLiteral("Dash"), MATCH_OPERATION_EQUALS);
    CHECK_FALSE(dash[0]);
    CHECK_FALSE(dash[1]);
    CHECK(dash[2]);
    CHECK(dash[3]);
    CHECK_FALSE(dash[4]);

    const auto notVendorTab = selectedEntities(model, QStringLiteral("lineType"),
                                               QStringLiteral("VENDOR_TAB"), MATCH_OPERATION_NOT_EQUALS);
    CHECK_FALSE(notVendorTab[0]);
    CHECK(notVendorTab[1]);
    CHECK(notVendorTab[2]);
    CHECK(notVendorTab[3]);
    // The last line is By Layer, so its own pen names no line type.
    CHECK(notVendorTab[4]);
}

TEST_CASE("The resolved entity line type follows the layer's pen",
          "[gui][linetype][dialog]") {
    SelectionModel model;

    const auto vendorTab = selectedEntities(model, QStringLiteral("lineTypeR"),
                                            QStringLiteral("VENDOR_TAB"), MATCH_OPERATION_EQUALS);
    CHECK(vendorTab[0]);
    CHECK_FALSE(vendorTab[1]);
    CHECK_FALSE(vendorTab[2]);
    CHECK_FALSE(vendorTab[3]);
    CHECK(vendorTab[4]);
}
