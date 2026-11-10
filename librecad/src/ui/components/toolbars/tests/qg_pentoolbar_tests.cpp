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

// The pen toolbar holds the active pen: its line type combo offers the active
// drawing's line types, a chosen row reaches the active pen by name, and a
// document switch refills the rows without touching the pen.

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <QImage>
#include <QString>

#include "lc_actiontestsupport.h"
#include "lc_linetype.h"
#include "lc_linetypelist.h"
#include "qg_linetypebox.h"
#include "qg_pentoolbar.h"
#include "rs_color.h"
#include "rs_graphic.h"
#include "rs_layer.h"
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

QG_LineTypeBox* lineTypeBox(QG_PenToolBar& toolbar) {
    return toolbar.findChild<QG_LineTypeBox*>();
}

int countText(const QG_LineTypeBox* box, const QString& text) {
    int found = 0;
    for (int i = 0; i < box->count(); ++i) {
        if (box->itemText(i) == text) {
            ++found;
        }
    }
    return found;
}

QImage rowImage(const QG_LineTypeBox& box, int row) {
    return box.itemIcon(row).pixmap(box.iconSize()).toImage().convertToFormat(QImage::Format_ARGB32);
}

} // namespace

TEST_CASE("The pen toolbar offers the line types of its drawing",
          "[gui][pen][linetype][toolbar]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, "VENDOR_TAB", {10.0, -5.0});
    lc::test::TestGraphicView view;
    view.setDocument(&graphic);

    QG_PenToolBar toolbar(QStringLiteral("pen"));
    toolbar.setGraphicView(&view);
    QG_LineTypeBox* combo = lineTypeBox(toolbar);
    REQUIRE(combo != nullptr);
    const int row = combo->findText(QStringLiteral("VENDOR_TAB"));
    REQUIRE(row >= 0);

    int emissions = 0;
    RS_Pen lastPen;
    QObject::connect(&toolbar, &QG_PenToolBar::penChanged, [&](const RS_Pen& pen) {
        ++emissions;
        lastPen = pen;
    });
    combo->setCurrentIndex(row);
    emit combo->activated(row);

    CHECK(toolbar.getPen().hasLineTypeName());
    CHECK(toolbar.getPen().getLineTypeName() == QStringLiteral("VENDOR_TAB"));
    CHECK(emissions == 1);
    CHECK(lastPen.getLineTypeName() == QStringLiteral("VENDOR_TAB"));
}

TEST_CASE("A whole pen reaches the active pen with its name",
          "[gui][pen][linetype][toolbar]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, "VENDOR_TAB", {10.0, -5.0});
    lc::test::TestGraphicView view;
    view.setDocument(&graphic);

    QG_PenToolBar toolbar(QStringLiteral("pen"));
    toolbar.setGraphicView(&view);
    // Another spelling of the table row's key.
    const RS_Pen source = namedPen(QStringLiteral("vendor_tab"));
    toolbar.setLineType(source);

    CHECK(toolbar.getPen().hasLineTypeName());
    CHECK(toolbar.getPen().getLineTypeId() == source.getLineTypeId());
    CHECK(toolbar.getPen().getLineTypeName() == QStringLiteral("vendor_tab"));

    toolbar.setLineType(RS2::DashLine);
    CHECK_FALSE(toolbar.getPen().hasLineTypeName());
    CHECK(toolbar.getPen().getLineType() == RS2::DashLine);

    // A set by enum forgets a pen set before, even when it selects that row.
    toolbar.setLineType(namedPen(QStringLiteral("dashed")));
    CHECK(toolbar.getPen().hasLineTypeName());
    toolbar.setLineType(RS2::DashLine);
    CHECK_FALSE(toolbar.getPen().hasLineTypeName());
    CHECK(toolbar.getPen().getLineType() == RS2::DashLine);
}

TEST_CASE("A layer pen reaches the active pen whole",
          "[gui][pen][linetype][toolbar]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, "VENDOR_TAB", {10.0, -5.0});
    lc::test::TestGraphicView view;
    view.setDocument(&graphic);

    QG_PenToolBar toolbar(QStringLiteral("pen"));
    toolbar.setGraphicView(&view);
    QG_LineTypeBox* combo = lineTypeBox(toolbar);
    REQUIRE(combo != nullptr);

    std::vector<RS_Pen> emitted;
    QObject::connect(&toolbar, &QG_PenToolBar::penChanged, [&emitted](const RS_Pen& pen) {
        emitted.push_back(pen);
    });
    const RS_Pen layerPen = namedPen(QStringLiteral("VENDOR_TAB"));
    toolbar.setLayerLineType(layerPen, true);
    REQUIRE(emitted.size() == 1);
    CHECK(emitted.at(0).hasLineTypeName());
    CHECK(emitted.at(0).getLineTypeName() == QStringLiteral("VENDOR_TAB"));
    CHECK(combo->currentIndex() == 0);
    CHECK(toolbar.getPen().hasLineTypeName());
    CHECK(toolbar.getPen().getLineTypeName() == QStringLiteral("VENDOR_TAB"));
    CHECK(toolbar.getPen().getLineTypeId() == layerPen.getLineTypeId());

    // The enum overload keeps its meaning: the layer's line type, without a name.
    toolbar.setLayerLineType(RS2::DashLine, true);
    CHECK_FALSE(toolbar.getPen().hasLineTypeName());
    CHECK(toolbar.getPen().getLineType() == RS2::DashLine);
}

TEST_CASE("A document switch refills the rows without changing the pen",
          "[gui][pen][linetype][toolbar]") {
    (void)lc::test::application();
    RS_Graphic first;
    first.initForNewDocument();
    addLineType(first, "VENDOR_TAB", {10.0, -5.0});
    RS_Graphic second;
    second.initForNewDocument();
    lc::test::TestGraphicView firstView;
    firstView.setDocument(&first);
    lc::test::TestGraphicView secondView;
    secondView.setDocument(&second);

    QG_PenToolBar toolbar(QStringLiteral("pen"));
    toolbar.setGraphicView(&firstView);
    toolbar.setLineType(namedPen(QStringLiteral("VENDOR_TAB")));
    QG_LineTypeBox* combo = lineTypeBox(toolbar);
    REQUIRE(combo != nullptr);
    const QString temporary =
        QG_LineTypeBox::tr("%1 (not in this drawing)").arg(QStringLiteral("VENDOR_TAB"));

    SECTION("a switch emits only the pen it keeps") {
        const RS_Pen before = toolbar.getPen();
        REQUIRE(before.hasLineTypeName());
        std::vector<RS_Pen> emitted;
        QObject::connect(&toolbar, &QG_PenToolBar::penChanged, [&emitted](const RS_Pen& pen) {
            emitted.push_back(pen);
        });
        const auto checkSwitch = [&emitted, &toolbar]() {
            // The new layer list reports its layer, so a switch to a drawing
            // always sends the pen.
            CHECK_FALSE(emitted.empty());
            for (const RS_Pen& pen : emitted) {
                CHECK(pen == toolbar.getPen());
                // The spelling of a line type is not part of the pen's equality.
                CHECK(pen.getLineTypeId() == toolbar.getPen().getLineTypeId());
            }
            emitted.clear();
        };

        toolbar.setGraphicView(&secondView);
        checkSwitch();
        CHECK(toolbar.getPen() == before);
        CHECK(toolbar.getPen().getLineTypeId() == before.getLineTypeId());
        // The second drawing has taken the definition: the real row, no temporary one.
        CHECK(combo->findText(QStringLiteral("VENDOR_TAB")) >= 0);
        CHECK(countText(combo, temporary) == 0);
        CHECK(combo->currentText() == QStringLiteral("VENDOR_TAB"));

        toolbar.setGraphicView(&firstView);
        checkSwitch();
        CHECK(toolbar.getPen() == before);
        CHECK(toolbar.getPen().getLineTypeId() == before.getLineTypeId());
        CHECK(countText(combo, temporary) == 0);
        CHECK(combo->findText(QStringLiteral("VENDOR_TAB")) >= 0);
        CHECK(combo->currentText() == QStringLiteral("VENDOR_TAB"));

        toolbar.setGraphicView(nullptr);
        CHECK(emitted.empty());
        emitted.clear();
        CHECK(toolbar.getPen() == before);
        CHECK(toolbar.getPen().getLineTypeId() == before.getLineTypeId());
        CHECK(countText(combo, temporary) == 0);
        // Without a drawing there are no name rows: the pen's enum row shows.
        CHECK(combo->currentText() == QG_LineTypeBox::tr("Continuous"));
    }

    SECTION("a view without a drawing does not cost the pen its name") {
        const RS_Pen before = toolbar.getPen();
        REQUIRE(before.hasLineTypeName());

        toolbar.setGraphicView(nullptr);
        CHECK(toolbar.getPen().getLineTypeId() == before.getLineTypeId());
        // Back on the first drawing the table row is shown again.
        toolbar.setGraphicView(&firstView);
        CHECK(toolbar.getPen().getLineTypeId() == before.getLineTypeId());
        CHECK(combo->currentText() == QStringLiteral("VENDOR_TAB"));

        toolbar.setGraphicView(nullptr);
        // The second drawing has taken the definition by now: its real row shows.
        toolbar.setGraphicView(&secondView);
        CHECK(toolbar.getPen().getLineTypeId() == before.getLineTypeId());
        CHECK(toolbar.getPen().hasLineTypeName());
        CHECK(combo->currentText() == QStringLiteral("VENDOR_TAB"));
        CHECK(countText(combo, temporary) == 0);
    }

    SECTION("a drawing that redefines the active built-in names the pen") {
        REQUIRE(first.getActiveLayer() != nullptr);
        RS_Graphic redefining;
        redefining.initForNewDocument();
        REQUIRE(redefining.getActiveLayer() != nullptr);
        addLineType(redefining, "DASHED", {64.0, -32.0});
        lc::test::TestGraphicView redefiningView;
        redefiningView.setDocument(&redefining);

        const int dashRow = combo->findData(static_cast<int>(RS2::DashLine));
        REQUIRE(dashRow >= 0);
        combo->setCurrentIndex(dashRow);
        emit combo->activated(dashRow);
        REQUIRE_FALSE(toolbar.getPen().hasLineTypeName());
        REQUIRE(toolbar.getPen().getLineType() == RS2::DashLine);

        toolbar.setGraphicView(&redefiningView);
        CHECK(toolbar.getPen().hasLineTypeName());
        CHECK(toolbar.getPen().getLineTypeName() == QStringLiteral("DASHED"));
        CHECK(toolbar.getPen().getLineType() == RS2::DashLine);

        // Back on the first drawing the pen is that built-in again: the same
        // enum and no line type of its own behind the name.
        toolbar.setGraphicView(&firstView);
        CHECK(toolbar.getPen().getLineType() == RS2::DashLine);
        CHECK(toolbar.getPen().getLineTypeFoldId() == 0);
    }

    SECTION("a By Layer selection survives a switch") {
        toolbar.setLayerLineType(namedPen(QStringLiteral("VENDOR_TAB")), true);
        REQUIRE(combo->currentIndex() == 0);
        const RS_Pen before = toolbar.getPen();
        REQUIRE(before.getLineType() == RS2::SolidLine);

        toolbar.setGraphicView(&secondView);
        // The refill must not turn the By Layer row into the named row.
        CHECK(combo->currentIndex() == 0);
        // updateByLayer() rewrites the active pen to the row it shows.
        CHECK(toolbar.getPen().getLineType() == RS2::LineByLayer);
        CHECK(toolbar.getPen().getColor() == before.getColor());
        CHECK(toolbar.getPen().getWidth() == before.getWidth());
    }
}

TEST_CASE("A plain built-in row still gives an enum pen",
          "[gui][pen][linetype][toolbar]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    lc::test::TestGraphicView view;
    view.setDocument(&graphic);

    QG_PenToolBar toolbar(QStringLiteral("pen"));
    toolbar.setGraphicView(&view);
    QG_LineTypeBox* combo = lineTypeBox(toolbar);
    REQUIRE(combo != nullptr);
    const int row = combo->findData(static_cast<int>(RS2::DashLine));
    REQUIRE(row >= 0);
    combo->setCurrentIndex(row);
    emit combo->activated(row);

    CHECK_FALSE(toolbar.getPen().hasLineTypeName());
    CHECK(toolbar.getPen().getLineType() == RS2::DashLine);
}

TEST_CASE("The active line type travels to the next drawing",
          "[gui][pen][linetype][toolbar]") {
    (void)lc::test::application();
    RS_Graphic first;
    first.initForNewDocument();
    addLineType(first, "VENDOR_TAB", {10.0, -5.0});
    first.findLineType(QStringLiteral("VENDOR_TAB"))->description = QStringLiteral("Tabulated");
    RS_Graphic second;
    second.initForNewDocument();
    second.setModified(false);
    lc::test::TestGraphicView firstView;
    firstView.setDocument(&first);
    lc::test::TestGraphicView secondView;
    secondView.setDocument(&second);

    QG_PenToolBar toolbar(QStringLiteral("pen"));
    toolbar.setGraphicView(&firstView);
    toolbar.setLineType(namedPen(QStringLiteral("VENDOR_TAB")));
    QG_LineTypeBox* combo = lineTypeBox(toolbar);
    REQUIRE(combo != nullptr);
    const RS_Pen before = toolbar.getPen();
    REQUIRE(before.hasLineTypeName());
    const QString temporary =
        QG_LineTypeBox::tr("%1 (not in this drawing)").arg(QStringLiteral("VENDOR_TAB"));

    const auto checkSecond = [&]() {
        LC_LineType* adopted = second.findLineType(QStringLiteral("VENDOR_TAB"));
        REQUIRE(adopted != nullptr);
        CHECK(adopted->pattern == std::vector<double>({10.0, -5.0}));
        CHECK(adopted->description == QStringLiteral("Tabulated"));
        CHECK_FALSE(second.isModified());
        CHECK(combo->currentText() == QStringLiteral("VENDOR_TAB"));
        CHECK(countText(combo, temporary) == 0);
        CHECK(toolbar.getPen().getLineTypeId() == before.getLineTypeId());
    };

    SECTION("directly") {
        toolbar.setGraphicView(&secondView);
        checkSecond();
    }

    SECTION("through a null view") {
        toolbar.setGraphicView(nullptr);
        toolbar.setGraphicView(&secondView);
        checkSecond();
    }
}

TEST_CASE("The next drawing's own definition wins",
          "[gui][pen][linetype][toolbar]") {
    (void)lc::test::application();
    RS_Graphic first;
    first.initForNewDocument();
    addLineType(first, QStringLiteral("VENDOR_TAB"), {10.0, -5.0});
    RS_Graphic second;
    second.initForNewDocument();
    addLineType(second, QStringLiteral("VENDOR_TAB"), {1.0, -1.0});
    lc::test::TestGraphicView firstView;
    firstView.setDocument(&first);
    lc::test::TestGraphicView secondView;
    secondView.setDocument(&second);

    QG_PenToolBar toolbar(QStringLiteral("pen"));
    toolbar.setGraphicView(&firstView);
    toolbar.setLineType(namedPen(QStringLiteral("VENDOR_TAB")));

    SECTION("a definition with dashes wins") {
        toolbar.setGraphicView(&secondView);

        LC_LineType* entry = second.findLineType(QStringLiteral("VENDOR_TAB"));
        REQUIRE(entry != nullptr);
        CHECK(entry->pattern == std::vector<double>({1.0, -1.0}));
    }

    SECTION("a record without dashes takes them") {
        RS_Graphic naming;
        naming.initForNewDocument();
        LC_LineType* record = naming.addLineType(new LC_LineType(QStringLiteral("vendor_tab")));
        REQUIRE(record != nullptr);
        naming.setModified(false);
        lc::test::TestGraphicView namingView;
        namingView.setDocument(&naming);
        const unsigned rows = naming.countLineTypes();

        toolbar.setGraphicView(&namingView);

        LC_LineType* filled = naming.findLineType(QStringLiteral("VENDOR_TAB"));
        REQUIRE(filled != nullptr);
        CHECK(filled == record);
        // The drawing's spelling and its record stay its own.
        CHECK(filled->name == QStringLiteral("vendor_tab"));
        CHECK(filled->pattern == std::vector<double>({10.0, -5.0}));
        CHECK(naming.countLineTypes() == rows);
        CHECK_FALSE(naming.isModified());
        CHECK_FALSE(naming.getLineTypeList().isModified());
    }
}

TEST_CASE("A layer's line type stays with its drawing", "[gui][pen][linetype][toolbar]") {
    (void)lc::test::application();
    RS_Graphic first;
    first.initForNewDocument();
    addLineType(first, QStringLiteral("VENDOR_TAB"), {10.0, -5.0});
    auto* layer = new RS_Layer(QStringLiteral("NAMED"));
    layer->setPen(namedPen(QStringLiteral("VENDOR_TAB")));
    first.addLayer(layer);
    RS_Graphic second;
    second.initForNewDocument();
    second.setModified(false);
    const unsigned secondCount = second.countLineTypes();
    lc::test::TestGraphicView firstView;
    firstView.setDocument(&first);
    lc::test::TestGraphicView secondView;
    secondView.setDocument(&second);

    QG_PenToolBar toolbar(QStringLiteral("pen"));
    toolbar.setGraphicView(&firstView);

    // The next drawing's own layer decides there: the pen is By Layer again,
    // and the line type of the layer left behind does not come along.
    const auto checkSecond = [&]() {
        CHECK(toolbar.getPen().getLineType() == RS2::LineByLayer);
        CHECK_FALSE(toolbar.getPen().hasLineTypeName());
        CHECK(second.findLineType(QStringLiteral("VENDOR_TAB")) == nullptr);
        CHECK(second.countLineTypes() == secondCount);
        CHECK_FALSE(second.isModified());
    };

    SECTION("from the layer alone") {
        // What Pen Pick and Sync by Layer call: the row shows By Layer and the
        // pen holds the layer's line type.
        toolbar.setLayerLineType(layer->getPen(), true);
        REQUIRE(toolbar.getPen().hasLineTypeName());
        CHECK(toolbar.getPen().getLineTypeName() == QStringLiteral("VENDOR_TAB"));
        toolbar.setGraphicView(&secondView);
        checkSecond();
    }

    SECTION("after its row had been the active one") {
        toolbar.setLineType(namedPen(QStringLiteral("VENDOR_TAB")));
        toolbar.setLayerLineType(layer->getPen(), true);
        REQUIRE(toolbar.getPen().hasLineTypeName());
        toolbar.setGraphicView(&secondView);
        checkSecond();
    }
}

TEST_CASE("The definition last shown is the one that travels on",
          "[gui][pen][linetype][toolbar]") {
    (void)lc::test::application();
    RS_Graphic first;
    first.initForNewDocument();
    addLineType(first, QStringLiteral("VENDOR_TAB"), {10.0, -5.0});
    RS_Graphic second;
    second.initForNewDocument();
    addLineType(second, QStringLiteral("VENDOR_TAB"), {1.0, -1.0});
    RS_Graphic third;
    third.initForNewDocument();
    third.setModified(false);
    lc::test::TestGraphicView firstView;
    firstView.setDocument(&first);
    lc::test::TestGraphicView secondView;
    secondView.setDocument(&second);
    lc::test::TestGraphicView thirdView;
    thirdView.setDocument(&third);

    QG_PenToolBar toolbar(QStringLiteral("pen"));
    toolbar.setGraphicView(&firstView);
    QG_LineTypeBox* combo = lineTypeBox(toolbar);
    REQUIRE(combo != nullptr);
    const int row = combo->findText(QStringLiteral("VENDOR_TAB"));
    REQUIRE(row >= 0);
    combo->setCurrentIndex(row);
    emit combo->activated(row);
    REQUIRE(toolbar.getPen().getLineTypeName() == QStringLiteral("VENDOR_TAB"));

    toolbar.setGraphicView(&secondView);
    toolbar.setGraphicView(&thirdView);

    // The third drawing takes what the second one showed, not the first one's.
    const LC_LineType* inThird = third.findLineType(QStringLiteral("VENDOR_TAB"));
    REQUIRE(inThird != nullptr);
    CHECK(inThird->pattern == std::vector<double>({1.0, -1.0}));
    const LC_LineType* inSecond = second.findLineType(QStringLiteral("VENDOR_TAB"));
    REQUIRE(inSecond != nullptr);
    CHECK(inSecond->pattern == std::vector<double>({1.0, -1.0}));
}

TEST_CASE("A redefined built-in stays where it is", "[gui][pen][linetype][toolbar]") {
    (void)lc::test::application();
    RS_Graphic first;
    first.initForNewDocument();
    addLineType(first, QStringLiteral("DASHED"), {64.0, -32.0});
    RS_Graphic second;
    second.initForNewDocument();
    second.setModified(false);
    lc::test::TestGraphicView firstView;
    firstView.setDocument(&first);
    lc::test::TestGraphicView secondView;
    secondView.setDocument(&second);

    QG_PenToolBar toolbar(QStringLiteral("pen"));
    toolbar.setGraphicView(&firstView);
    QG_LineTypeBox* combo = lineTypeBox(toolbar);
    REQUIRE(combo != nullptr);
    const int row = combo->findText(QStringLiteral("DASHED"));
    REQUIRE(row >= 0);
    combo->setCurrentIndex(row);
    emit combo->activated(row);
    REQUIRE(toolbar.getPen().getLineTypeName() == QStringLiteral("DASHED"));

    LC_LineType* seed = second.findLineType(QStringLiteral("DASHED"));
    REQUIRE(seed != nullptr);
    const std::vector<double> seeded = seed->pattern;
    REQUIRE_FALSE(LC_LineTypeList::redefinesBuiltIn(*seed));
    const unsigned rows = second.countLineTypes();

    toolbar.setGraphicView(&secondView);

    LC_LineType* dashed = second.findLineType(QStringLiteral("DASHED"));
    REQUIRE(dashed != nullptr);
    CHECK(dashed == seed);
    CHECK(dashed->pattern == seeded);
    CHECK_FALSE(LC_LineTypeList::redefinesBuiltIn(*dashed));
    CHECK(second.countLineTypes() == rows);
    CHECK_FALSE(second.isModified());
}

TEST_CASE("A table spelling with blanks around travels too",
          "[gui][pen][linetype][toolbar]") {
    (void)lc::test::application();
    RS_Graphic first;
    first.initForNewDocument();
    addLineType(first, QStringLiteral("VENDOR_PAD "), {10.0, -5.0});
    RS_Graphic second;
    second.initForNewDocument();
    second.setModified(false);
    lc::test::TestGraphicView firstView;
    firstView.setDocument(&first);
    lc::test::TestGraphicView secondView;
    secondView.setDocument(&second);

    QG_PenToolBar toolbar(QStringLiteral("pen"));
    toolbar.setGraphicView(&firstView);
    toolbar.setLineType(namedPen(QStringLiteral("VENDOR_PAD")));
    QG_LineTypeBox* combo = lineTypeBox(toolbar);
    REQUIRE(combo != nullptr);
    const QString temporary =
        QG_LineTypeBox::tr("%1 (not in this drawing)").arg(QStringLiteral("VENDOR_PAD"));
    REQUIRE(combo->currentText() == QStringLiteral("VENDOR_PAD "));
    CHECK(countText(combo, temporary) == 0);

    toolbar.setGraphicView(&secondView);

    LC_LineType* adopted = second.findLineType(QStringLiteral("VENDOR_PAD"));
    REQUIRE(adopted != nullptr);
    CHECK(adopted->pattern == std::vector<double>({10.0, -5.0}));
}

TEST_CASE("A name without a known definition still shows as missing",
          "[gui][pen][linetype][toolbar]") {
    (void)lc::test::application();
    RS_Graphic first;
    first.initForNewDocument();
    RS_Graphic second;
    second.initForNewDocument();
    lc::test::TestGraphicView firstView;
    firstView.setDocument(&first);
    lc::test::TestGraphicView secondView;
    secondView.setDocument(&second);

    QG_PenToolBar toolbar(QStringLiteral("pen"));
    toolbar.setGraphicView(&firstView);
    QG_LineTypeBox* combo = lineTypeBox(toolbar);
    REQUIRE(combo != nullptr);

    const auto checkMissing = [&combo, &toolbar, &second, &secondView](
                                  const QString& name, const QString& shownBefore) {
        const QString temporary = QG_LineTypeBox::tr("%1 (not in this drawing)").arg(name);
        REQUIRE(combo->currentText() == shownBefore);
        const unsigned before = second.countLineTypes();
        toolbar.setGraphicView(&secondView);
        CHECK(second.countLineTypes() == before);
        CHECK(second.findLineType(name) == nullptr);
        CHECK(combo->currentText() == temporary);
        CHECK(countText(combo, temporary) == 1);
        CHECK(toolbar.getPen().getLineTypeName() == name);
    };

    SECTION("a name the drawing lacks") {
        toolbar.setLineType(namedPen(QStringLiteral("VENDOR_GONE")));
        checkMissing(QStringLiteral("VENDOR_GONE"), QG_LineTypeBox::tr("%1 (not in this drawing)").arg(QStringLiteral("VENDOR_GONE")));
    }

    SECTION("a record without dashes") {
        addLineType(first, QStringLiteral("VENDOR_BARE"), {});
        toolbar.setGraphicView(&firstView);
        const int row = combo->findText(QStringLiteral("VENDOR_BARE"));
        REQUIRE(row >= 0);
        combo->setCurrentIndex(row);
        emit combo->activated(row);
        REQUIRE(toolbar.getPen().getLineTypeName() == QStringLiteral("VENDOR_BARE"));
        checkMissing(QStringLiteral("VENDOR_BARE"), QStringLiteral("VENDOR_BARE"));
    }
}

TEST_CASE("The By Layer row of the toolbar previews the layer's line type",
          "[gui][pen][linetype][toolbar]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, "VENDOR_TAB", {10.0, -5.0});
    auto* namedLayer = new RS_Layer(QStringLiteral("NAMED"));
    namedLayer->setPen(namedPen(QStringLiteral("VENDOR_TAB")));
    graphic.addLayer(namedLayer);
    lc::test::TestGraphicView view;
    view.setDocument(&graphic);

    QG_PenToolBar toolbar(QStringLiteral("pen"));
    toolbar.setGraphicView(&view);
    QG_LineTypeBox* combo = lineTypeBox(toolbar);
    REQUIRE(combo != nullptr);
    const int tabRow = combo->findText(QStringLiteral("VENDOR_TAB"));
    REQUIRE(tabRow >= 0);

    REQUIRE(graphic.activateLayer(namedLayer, true));
    CHECK(rowImage(*combo, 0) == rowImage(*combo, tabRow));
}
