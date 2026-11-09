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

// The entity dialogs hand the drawing their viewport shows to the pen widget, so
// its line type combo offers the drawing's own line types and selects the one
// the entity carries.

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <QString>

#include "lc_actiontestsupport.h"
#include "lc_dlg_entityproperties.h"
#include "lc_graphicviewport.h"
#include "lc_linetype.h"
#include "qg_linetypebox.h"
#include "qg_widgetpen.h"
#include "rs_color.h"
#include "rs_graphic.h"
#include "rs_line.h"
#include "rs_pen.h"

namespace {

// An imported record as the filter will make it from an LTYPE entry.
void addLineType(RS_Graphic& graphic, const QString& name, const std::vector<double>& pattern) {
    auto* entry = new LC_LineType(name);
    entry->pattern = pattern;
    entry->origin = LC_LineType::Origin::Imported;
    graphic.addLineType(entry);
}

} // namespace

TEST_CASE("The entity properties dialog offers its drawing's line types", "[gui][dialog][linetype]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, "VENDOR_TAB", {10.0, -5.0});

    RS_Pen named(RS_Color(Qt::red), RS2::Width00, RS2::SolidLine);
    named.setLineTypeName(QStringLiteral("VENDOR_TAB"));
    auto* line = new RS_Line(&graphic, RS_LineData(RS_Vector{0.0, 0.0}, RS_Vector{1.0, 0.0}));
    line->setPen(named);
    graphic.addEntity(line);

    lc::test::TestGraphicView view;
    view.setDocument(&graphic);
    LC_DlgEntityProperties dialog(nullptr, view.getViewPort(), line,
                                  LC_ActionContext::InteractiveInputInfo::NOTNEEDED,
                                  QString(), 0.0, 0.0);

    auto* penWidget = dialog.findChild<QG_WidgetPen*>();
    REQUIRE(penWidget != nullptr);
    auto* combo = penWidget->findChild<QG_LineTypeBox*>();
    REQUIRE(combo != nullptr);
    CHECK(combo->currentText() == QStringLiteral("VENDOR_TAB"));
    dialog.reject();
}
