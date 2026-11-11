/****************************************************************************
**
** This file is part of the LibreCAD project, a 2D CAD program
**
** Copyright (C) 2026 LibreCAD.org
** Copyright (C) 2026 Dongxu Li
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

// The sheet carries a pen's line type by the name the drawing gives it: the
// value keeps the enum and the spelling, the editor offers the drawing's rows,
// and the cell paints what the row paints.

#include <functional>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QList>
#include <QPainter>
#include <QString>
#include <QTimer>
#include <QWidget>

#include "lc_actioncontext.h"
#include "lc_actiongroupmanager.h"
#include "lc_actiontestsupport.h"
#include "lc_linetype.h"
#include "lc_linetypelist.h"
#include "lc_peninforegistry.h"
#include "lc_properties_sheet.h"
#include "lc_propertiesprovider_active_pen.h"
#include "lc_propertiesprovider_graphic_layer.h"
#include "lc_propertysheetwidget.h"
#include "lc_selectedset.h"
#include "lc_property_linetype.h"
#include "lc_property_linetype_combobox.h"
#include "lc_property_linetype_combobox_view.h"
#include "lc_property_linetype_value.h"
#include "lc_property_multi.h"
#include "lc_property_view_utils.h"
#include "qg_linetypebox.h"
#include "qg_pentoolbar.h"
#include "rs_dimaligned.h"
#include "rs_graphic.h"
#include "rs_layer.h"
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

RS_Pen namedPen(const QString& name) {
    RS_Pen pen(RS_Color(Qt::red), RS2::Width00, RS2::SolidLine);
    pen.setLineTypeName(name);
    return pen;
}

RS_Line* addLine(RS_Graphic& graphic, const RS_Pen& pen) {
    auto* line = new RS_Line(&graphic, RS_LineData(RS_Vector(0, 0), RS_Vector(10, 0)));
    line->setPen(pen);
    graphic.addEntity(line);
    line->update();
    return line;
}

void collectProperties(LC_PropertyContainer* container, const QString& nameSuffix,
                       QList<LC_Property*>& out) {
    for (LC_Property* child : container->childProperties()) {
        if (child->getName().endsWith(nameSuffix)) {
            out.append(child);
        }
        if (LC_PropertyContainer* sub = child->asContainer()) {
            collectProperties(sub, nameSuffix, out);
        }
    }
}

/// The sheet's property of a name, as the section of that name owns it: a
/// sheet holds an entity's, the active layer's and the active pen's alike.
LC_Property* propertyUnder(LC_PropertiesSheet* sheet, const QString& sectionSuffix,
                           const QString& nameSuffix) {
    LC_PropertyContainer* root = sheet->getPropertyContainer();
    if (root == nullptr) {
        return nullptr;
    }
    QList<LC_Property*> found;
    collectProperties(root, nameSuffix, found);
    for (LC_Property* property : found) {
        QObject* parent = property->parent();
        if (parent != nullptr && parent->objectName().endsWith(sectionSuffix)) {
            return property;
        }
    }
    return nullptr;
}

/// The per-value property behind a container's multi holder.
LC_PropertyLineType* lineTypeOf(LC_Property* property) {
    LC_PropertyAtomic* atomic = property != nullptr ? property->asAtomic() : nullptr;
    if (auto* multi = dynamic_cast<LC_PropertyMulti*>(atomic)) {
        atomic = multi->getFirstProperty();
    }
    return dynamic_cast<LC_PropertyLineType*>(atomic);
}

struct SheetFixture {
    LC_ActionContext context;
    LC_ActionGroupManager actions{nullptr};
    lc::test::TestGraphicView view;
    LC_PropertySheetWidget sheet{nullptr, &context, &actions};

    SheetFixture() {
        sheet.getOptions()->noSelectionWorkspace = false;
        sheet.getOptions()->noSelectionGraphicView = false;
        sheet.resize(420, 900);
        sheet.show();
    }

    void show(RS_Graphic& graphic) {
        view.setDocument(&graphic);
        context.setDocumentAndView(&graphic, &view);
        sheet.setGraphicView(&view);
        QCoreApplication::processEvents();
    }

    LC_PropertiesSheet* properties() const {
        return sheet.findChild<LC_PropertiesSheet*>();
    }
};

//// Opens the sheet's inplace editor on a property, as a Return key does.
QComboBox* openLineTypeEditor(LC_PropertiesSheet* properties, LC_Property* property) {
    if (!properties->setActiveProperty(property, true)) {
        return nullptr;
    }
    (void)properties->grab();
    QKeyEvent activate(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(properties, &activate);
    QCoreApplication::processEvents();
    for (QComboBox* candidate : properties->findChildren<QComboBox*>()) {
        if (dynamic_cast<LC_PropertyLineTypeCombobox*>(candidate) != nullptr) {
            return candidate;
        }
    }
    return nullptr;
}

// A view whose value drawing the test may call directly.
class TestLineTypeView final : public LC_PropertyLineTypeComboboxView {
public:
    explicit TestLineTypeView(LC_PropertyLineType* property) : LC_PropertyLineTypeComboboxView(property) {}

    void paintValue(QPainter& painter, const QRect& rect) const {
        doDrawValueDetails(QApplication::style(), typedProperty().value(), painter, rect);
    }
};

qreal pixelRatio(const QImage& image) {
    return image.devicePixelRatio() > 0.0 ? image.devicePixelRatio() : 1.0;
}

/// A picture drawn in whole device pixels keeps the pixel layout the
/// comparisons describe; another ratio leaves them out.
void skipUnlessWholePixelRatio(const QImage& image) {
    const qreal ratio = pixelRatio(image);
    if (std::abs(ratio - qRound(ratio)) > 0.001) {
        SKIP("the picture is not drawn in whole device pixels");
    }
}

QRect deviceRect(const QRect& logical, const qreal ratio) {
    return QRect(qRound(logical.left() * ratio), qRound(logical.top() * ratio),
                 qRound(logical.width() * ratio), qRound(logical.height() * ratio));
}

QImage deviceImage(const QSize& logical, const qreal ratio) {
    QImage image(QSize(qRound(logical.width() * ratio), qRound(logical.height() * ratio)),
                 QImage::Format_ARGB32);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);
    return image;
}

QImage paintCell(const TestLineTypeView& view, const QSize& size, const qreal ratio) {
    QImage image = deviceImage(size, ratio);
    {
        QPainter painter(&image);
        view.paintValue(painter, QRect(QPoint(0, 0), size));
    }
    return image;
}

QImage paintCellWithoutList(const LC_PenLineTypeValue& value, const QSize& size, const qreal ratio) {
    LC_PropertyLineType property(nullptr, true);
    property.setValue(value);
    TestLineTypeView view(&property);
    return paintCell(view, size, ratio);
}

QImage paintedIcon(const QIcon& icon, const QSize& size, const qreal ratio) {
    QImage image = deviceImage(size, ratio);
    QPainter painter(&image);
    icon.paint(&painter, QRect(QPoint(0, 0), size));
    return image;
}

/// Runs the event loop in short slices until the sheet has committed the edit
/// it was given, or gives up. The sheet applies an edit through a timer.
bool waitForSheet(const std::function<bool()>& condition, const int timeoutMs = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() >= timeoutMs) {
            return false;
        }
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
        QCoreApplication::processEvents();
    }
    return true;
}

int litPixels(const QImage& image, const QRect& rect) {
    int lit = 0;
    for (int y = rect.top(); y <= rect.bottom() && y < image.height(); ++y) {
        for (int x = rect.left(); x <= rect.right() && x < image.width(); ++x) {
            if (image.pixelColor(x, y).alpha() > 32) {
                ++lit;
            }
        }
    }
    return lit;
}

} // namespace

TEST_CASE("A pen's line type value keeps the identity a pen holds",
          "[ui][linetype][property-sheet]") {
    (void)lc::test::application();

    // Two spellings of one name are one value.
    const LC_PenLineTypeValue upper(namedPen(QStringLiteral("VENDOR_TAB")));
    const LC_PenLineTypeValue lower(namedPen(QStringLiteral("vendor_tab")));
    CHECK(upper == lower);
    // Two names with one stand-in enum are two values.
    const LC_PenLineTypeValue dot(namedPen(QStringLiteral("vendor_dot")));
    CHECK(upper != dot);
    // A named value is not a nameless one with the same enum.
    const LC_PenLineTypeValue nameless(RS2::SolidLine);
    CHECK(upper != nameless);
    CHECK_FALSE(nameless.hasName());
    // Nameless values compare by their enum.
    CHECK(nameless == LC_PenLineTypeValue(RS2::SolidLine));
    CHECK(nameless != LC_PenLineTypeValue(RS2::DashLine));

    // applyTo() gives the name and leaves the pen's color and width alone.
    RS_Pen pen(RS_Color(Qt::green), RS2::Width02, RS2::DashLine);
    upper.applyTo(pen);
    CHECK(pen.getLineTypeName() == QStringLiteral("VENDOR_TAB"));
    CHECK(pen.getColor() == RS_Color(Qt::green));
    CHECK(pen.getWidth() == RS2::Width02);
    CHECK(pen.getLineTypeId() == namedPen(QStringLiteral("VENDOR_TAB")).getLineTypeId());

    // A value built from an enum writes the enum and no name.
    RS_Pen enumPen;
    LC_PenLineTypeValue(RS2::LineByLayer).applyTo(enumPen);
    CHECK_FALSE(enumPen.hasLineTypeName());
    CHECK(enumPen.getLineType() == RS2::LineByLayer);
    CHECK_FALSE(LC_PenLineTypeValue(RS2::LineByLayer).hasName());
}

TEST_CASE("The entity line type property carries the name",
          "[ui][linetype][property-sheet]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, QStringLiteral("VENDOR_TAB"), {10.0, -5.0});
    addLineType(graphic, QStringLiteral("VENDOR_DOT"), {0.0, -5.0});
    SheetFixture fixture;
    fixture.show(graphic);

    RS_Line* tabLine = addLine(graphic, namedPen(QStringLiteral("vendor_tab")));
    RS_Line* otherLine = addLine(graphic, namedPen(QStringLiteral("VENDOR_TAB")));
    graphic.select(QList<RS_Entity*>{tabLine, otherLine}, true);
    fixture.show(graphic);
    LC_PropertiesSheet* properties = fixture.properties();
    REQUIRE(properties != nullptr);
    LC_Property* entityProperty =
        propertyUnder(properties, QStringLiteral("_secGeneral"), QStringLiteral("$linetype"));
    REQUIRE(entityProperty != nullptr);
    // Two spellings of one name are one value for the sheet.
    CHECK_FALSE(entityProperty->isMultiValue());
    LC_PropertyLineType* typed = lineTypeOf(entityProperty);
    REQUIRE(typed != nullptr);
    CHECK(typed->value().hasName());
    CHECK(typed->value().name() == QStringLiteral("vendor_tab"));
    CHECK(typed->lineTypeList() == &graphic.getLineTypeList());

    // Two names with the same stand-in enum are two values.
    RS_Line* dotLine = addLine(graphic, namedPen(QStringLiteral("VENDOR_DOT")));
    graphic.select(QList<RS_Entity*>{tabLine, dotLine}, true);
    fixture.show(graphic);
    entityProperty = propertyUnder(properties, QStringLiteral("_secGeneral"), QStringLiteral("$linetype"));
    REQUIRE(entityProperty != nullptr);
    CHECK(entityProperty->isMultiValue());

    // One entity: the value writes the name into its pen.
    graphic.unselect(tabLine);
    graphic.unselect(otherLine);
    graphic.select(QList<RS_Entity*>{dotLine}, true);
    const auto editLineType = [&](const QString& text, const bool byEnum, const RS2::LineType type,
                                  const LC_PenLineTypeValue& expected) {
        fixture.show(graphic);
        LC_Property* property =
            propertyUnder(properties, QStringLiteral("_secGeneral"), QStringLiteral("$linetype"));
        REQUIRE(property != nullptr);
        QComboBox* combo = openLineTypeEditor(properties, property);
        REQUIRE(combo != nullptr);
        const int row = byEnum ? combo->findData(static_cast<int>(type)) : combo->findText(text);
        REQUIRE(row >= 0);
        combo->setCurrentIndex(row);
        emit combo->activated(row);
        REQUIRE(waitForSheet([&graphic, expected]() {
            if (graphic.getSelection()->isEmpty()) {
                return false;
            }
            return LC_PenLineTypeValue(graphic.getSelection()->first()->getPen(false)) == expected;
        }));
    };

    editLineType(QStringLiteral("VENDOR_TAB"), false, RS2::SolidLine,
                 LC_PenLineTypeValue(namedPen(QStringLiteral("VENDOR_TAB"))));
    auto* updated = dynamic_cast<RS_Line*>(graphic.getSelection()->first());
    REQUIRE(updated != nullptr);
    CHECK(updated->getPen(false).hasLineTypeName());
    CHECK(updated->getPen(false).getLineTypeName() == QStringLiteral("VENDOR_TAB"));

    // A built-in value writes the enum and no name.
    editLineType(QString(), true, RS2::DashLine, LC_PenLineTypeValue(RS2::DashLine));
    updated = dynamic_cast<RS_Line*>(graphic.getSelection()->first());
    REQUIRE(updated != nullptr);
    CHECK_FALSE(updated->getPen(false).hasLineTypeName());
    CHECK(updated->getPen(false).getLineType() == RS2::DashLine);
}

TEST_CASE("The active layer line type property carries the name",
          "[ui][linetype][property-sheet]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, QStringLiteral("VENDOR_TAB"), {10.0, -5.0});
    RS_Layer* layer = graphic.getActiveLayer();
    REQUIRE(layer != nullptr);
    RS_Pen layerPen = layer->getPen();
    layerPen.setLineTypeName(QStringLiteral("VENDOR_TAB"));
    layer->setPen(layerPen);

    class LayerProvider final : public LC_PropertiesProviderGraphicLayer {
    public:
        LayerProvider() : LC_PropertiesProviderGraphicLayer(nullptr, nullptr) {}
        void fill(LC_PropertyContainer* container, RS_Graphic* owner) {
            createLineType(container, owner);
        }
    };
    LayerProvider provider;
    LC_PropertyContainer container;
    provider.fill(&container, &graphic);
    const QList<LC_Property*> found = container.findChildProperties(QStringLiteral("$linetype"));
    REQUIRE(found.size() == 1);
    auto* typed = dynamic_cast<LC_PropertyLineType*>(found.constFirst());
    REQUIRE(typed != nullptr);
    CHECK(typed->lineTypeList() == &graphic.getLineTypeList());
    CHECK(typed->value().hasName());
    CHECK(typed->value().name() == QStringLiteral("VENDOR_TAB"));

    RS_Pen pen = layer->getPen();
    typed->value().applyTo(pen);
    CHECK(pen.getLineTypeName() == QStringLiteral("VENDOR_TAB"));

    typed->setValue(LC_PenLineTypeValue(RS2::DashLine));
    CHECK_FALSE(layer->getPen().hasLineTypeName());
    CHECK(layer->getPen().getLineType() == RS2::DashLine);

    typed->setValue(LC_PenLineTypeValue(namedPen(QStringLiteral("VENDOR_TAB"))));
    CHECK(layer->getPen().hasLineTypeName());
    CHECK(layer->getPen().getLineTypeName() == QStringLiteral("VENDOR_TAB"));
}

TEST_CASE("The active pen line type property hands the toolbar a whole pen",
          "[ui][linetype][property-sheet]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, QStringLiteral("VENDOR_TAB"), {10.0, -5.0});
    lc::test::TestGraphicView view;
    view.setDocument(&graphic);
    QG_PenToolBar toolbar(QStringLiteral("pen"));
    toolbar.setGraphicView(&view);

    // The conversion the property's set uses, with a toolbar a test can reach.
    LC_PropertiesProviderActivePen::applyLineTypeToToolBar(
        &toolbar, LC_PenLineTypeValue(namedPen(QStringLiteral("VENDOR_TAB"))));
    REQUIRE(toolbar.getPen().hasLineTypeName());
    CHECK(toolbar.getPen().getLineTypeName() == QStringLiteral("VENDOR_TAB"));

    LC_PropertiesProviderActivePen::applyLineTypeToToolBar(
        &toolbar, LC_PenLineTypeValue(RS2::DashLine));
    CHECK_FALSE(toolbar.getPen().hasLineTypeName());
    CHECK(toolbar.getPen().getLineType() == RS2::DashLine);

    // The get side: a property built for a named pen reads its name.
    class PenProvider final : public LC_PropertiesProviderActivePen {
    public:
        explicit PenProvider(LC_ActionContext* context) : LC_PropertiesProviderActivePen(context, nullptr) {}
        void fill(LC_PropertyContainer* container, const RS_Pen& pen) {
            // This provider's two function names are swapped upstream.
            createLineWidth(container, pen);
        }
    };
    LC_ActionContext context;
    context.setDocumentAndView(&graphic, &view);
    PenProvider provider(&context);
    LC_PropertyContainer container;
    provider.fill(&container, namedPen(QStringLiteral("VENDOR_TAB")));
    const QList<LC_Property*> found = container.findChildProperties(QStringLiteral("$linetype"));
    REQUIRE(found.size() == 1);
    auto* typed = dynamic_cast<LC_PropertyLineType*>(found.constFirst());
    REQUIRE(typed != nullptr);
    CHECK(typed->value().hasName());
    CHECK(typed->value().name() == QStringLiteral("VENDOR_TAB"));
}

TEST_CASE("A dimension line type property still reads and writes its enum",
          "[ui][linetype][property-sheet]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    SheetFixture fixture;
    RS_DimensionData data;
    data.definitionPoint = RS_Vector(0, 3);
    data.middleOfText = RS_Vector(5, 3);
    data.style = "Standard";
    data.text = " ";
    auto* dimension = new RS_DimAligned(&graphic, data, {RS_Vector(0, 0), RS_Vector(10, 0)});
    graphic.addEntity(dimension);
    dimension->update();
    graphic.select(QList<RS_Entity*>{dimension}, true);
    fixture.show(graphic);
    LC_PropertiesSheet* properties = fixture.properties();
    REQUIRE(properties != nullptr);
    QList<LC_Property*> dimProperties;
    collectProperties(properties->getPropertyContainer(), QStringLiteral("$dimLineLineType"),
                      dimProperties);
    REQUIRE(dimProperties.size() == 1);
    LC_PropertyLineType* typed = lineTypeOf(dimProperties.constFirst());
    REQUIRE(typed != nullptr);
    // A dimension style has no drawing table behind its enum.
    CHECK(typed->lineTypeList() == nullptr);
    CHECK_FALSE(typed->value().hasName());

    QComboBox* combo = openLineTypeEditor(properties, dimProperties.constFirst());
    REQUIRE(combo != nullptr);
    const int dashRow = combo->findData(static_cast<int>(RS2::DashLine));
    REQUIRE(dashRow >= 0);
    combo->setCurrentIndex(dashRow);
    emit combo->activated(dashRow);
    const RS_Dimension* before = dimension;
    REQUIRE(waitForSheet([&graphic, before]() {
        if (graphic.getSelection()->isEmpty()) {
            return false;
        }
        RS_Entity* selected = graphic.getSelection()->first();
        if (selected == before) {
            return false;
        }
        auto* updated = dynamic_cast<RS_Dimension*>(selected);
        return updated != nullptr &&
               updated->getEffectiveCachedDimStyle()->dimensionLine()->lineType() == RS2::DashLine;
    }));
    auto* updated = dynamic_cast<RS_Dimension*>(graphic.getSelection()->first());
    REQUIRE(updated != nullptr);
    CHECK(updated->getEffectiveCachedDimStyle()->dimensionLine()->lineType() == RS2::DashLine);
}

TEST_CASE("The line type editor offers the drawing's rows",
          "[ui][linetype][property-sheet]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, QStringLiteral("VENDOR_TAB"), {10.0, -5.0});
    addLineType(graphic, QStringLiteral("VENDOR_DOT"), {0.0, -5.0});

    LC_PropertyLineType property(nullptr, true);
    property.setLineTypeList(&graphic.getLineTypeList());
    property.setValue(LC_PenLineTypeValue(namedPen(QStringLiteral("VENDOR_TAB"))));
    LC_PropertyLineTypeComboboxView view(&property);
    QWidget parent;
    QEvent activation(QEvent::MouseButtonRelease);
    LC_PropertyViewEditable::EditActivationContext context(&activation);
    auto* editor = dynamic_cast<LC_PropertyLineTypeCombobox*>(
        view.createValueEditor(&parent, QRect(0, 0, 200, 24), &context));
    REQUIRE(editor != nullptr);
    // The drawing's rows, and the row of the value.
    CHECK(editor->findText(QStringLiteral("VENDOR_TAB")) >= 0);
    CHECK(editor->findText(QStringLiteral("VENDOR_DOT")) >= 0);
    CHECK(editor->currentText() == QStringLiteral("VENDOR_TAB"));

    // A name the drawing lacks shows the temporary row; an editor opened on it
    // reads the value as it is.
    property.setValue(LC_PenLineTypeValue(namedPen(QStringLiteral("VENDOR_GONE"))));
    QWidget missingParent;
    auto* missingEditor = dynamic_cast<LC_PropertyLineTypeCombobox*>(
        view.createValueEditor(&missingParent, QRect(0, 0, 200, 24), &context));
    REQUIRE(missingEditor != nullptr);
    const QString temporary =
        QG_LineTypeBox::tr("%1 (not in this drawing)").arg(QStringLiteral("VENDOR_GONE"));
    CHECK(missingEditor->currentText() == temporary);

    // Choosing a row sets the value once, and between rows of one enum too.
    int changes = 0;
    QObject::connect(&property, &LC_Property::afterPropertyChange,
                     [&changes](const LC_PropertyChangeReason reason) {
                         if ((reason & PropertyChangeReasonValue) != 0) {
                             ++changes;
                         }
                     });
    const int dotRow = missingEditor->findText(QStringLiteral("VENDOR_DOT"));
    REQUIRE(dotRow >= 0);
    missingEditor->setCurrentIndex(dotRow);
    emit missingEditor->activated(dotRow);
    CHECK(changes == 1);
    CHECK(property.value().hasName());
    CHECK(property.value().name() == QStringLiteral("VENDOR_DOT"));

    // VENDOR_TAB and VENDOR_DOT share a stand-in enum: the row, not the enum,
    // decides, and the combo reports the move.
    const int tabRow = missingEditor->findText(QStringLiteral("VENDOR_TAB"));
    REQUIRE(tabRow >= 0);
    missingEditor->setCurrentIndex(tabRow);
    emit missingEditor->activated(tabRow);
    CHECK(changes == 2);
    CHECK(property.value().name() == QStringLiteral("VENDOR_TAB"));

    // Clearing to a stock row writes the enum and no name.
    const int dashRow = missingEditor->findData(static_cast<int>(RS2::DashLine));
    REQUIRE(dashRow >= 0);
    missingEditor->setCurrentIndex(dashRow);
    emit missingEditor->activated(dashRow);
    CHECK(property.value().lineType() == RS2::DashLine);
    CHECK_FALSE(property.value().hasName());
}

TEST_CASE("The line type cell shows the combo's row",
          "[ui][linetype][property-sheet]") {
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, QStringLiteral("VENDOR_TAB"), {10.0, -5.0});

    QG_LineTypeBox combo;
    combo.init(&graphic.getLineTypeList(), true, false);
    const QSize iconSize = QG_LineTypeBox::listIconSize();
    REQUIRE(iconSize == combo.iconSize());

    LC_PropertyLineType property(nullptr, true);
    property.setLineTypeList(&graphic.getLineTypeList());
    TestLineTypeView view(&property);
    const QSize cellSize(220, 24);
    const QRect iconRect(2, (cellSize.height() - iconSize.height()) / 2,
                         iconSize.width(), iconSize.height());
    const QRect textRect(iconRect.right() + 3, 0, cellSize.width() - iconRect.right() - 3,
                         cellSize.height());
    const auto comboRowPixmap = [&combo, iconSize](const RS_Pen& pen) {
        combo.setLineTypeOf(pen);
        const int row = combo.currentIndex();
        REQUIRE(row >= 0);
        return combo.itemIcon(row).pixmap(iconSize);
    };
    // The row picture and the cell must be compared at one device pixel ratio.
    const auto ratioOf = [](const QPixmap& pixmap) {
        return pixmap.devicePixelRatio() > 0.0 ? pixmap.devicePixelRatio() : 1.0;
    };

    SECTION("a name the drawing defines") {
        property.setValue(LC_PenLineTypeValue(namedPen(QStringLiteral("VENDOR_TAB"))));
        const QPixmap row = comboRowPixmap(namedPen(QStringLiteral("VENDOR_TAB")));
        const qreal ratio = ratioOf(row);
        const QImage cell = paintCell(view, cellSize, ratio);
        skipUnlessWholePixelRatio(cell);
        CHECK(cell.copy(deviceRect(iconRect, ratio)) ==
              row.toImage().convertToFormat(QImage::Format_ARGB32));
        // Not the stock picture of its stand-in enum.
        const QImage stock = paintedIcon(QIcon(QStringLiteral(":linetypes/linetype01.lci")), iconSize,
                                         ratio);
        CHECK(cell.copy(deviceRect(iconRect, ratio)) != stock);
        CHECK(litPixels(cell, deviceRect(textRect, ratio)) > 0);
    }

    SECTION("a name the drawing does not define") {
        property.setValue(LC_PenLineTypeValue(namedPen(QStringLiteral("VENDOR_GONE"))));
        const QPixmap row = comboRowPixmap(namedPen(QStringLiteral("VENDOR_GONE")));
        const qreal ratio = ratioOf(row);
        const QImage cell = paintCell(view, cellSize, ratio);
        skipUnlessWholePixelRatio(cell);
        CHECK(cell.copy(deviceRect(iconRect, ratio)) ==
              row.toImage().convertToFormat(QImage::Format_ARGB32));
        CHECK(litPixels(cell, deviceRect(textRect, ratio)) > 0);
    }

    SECTION("a plain built-in") {
        property.setValue(LC_PenLineTypeValue(RS2::DashLine));
        const QPixmap row = comboRowPixmap(
            RS_Pen(RS_Color(Qt::black), RS2::Width00, RS2::DashLine));
        const qreal ratio = ratioOf(row);
        const QImage cell = paintCell(view, cellSize, ratio);
        skipUnlessWholePixelRatio(cell);
        const QImage stock = paintedIcon(QIcon(QStringLiteral(":linetypes/linetype03.lci")), iconSize,
                                         ratio);
        CHECK(cell.copy(deviceRect(iconRect, ratio)) == stock);
    }

    SECTION("By Layer") {
        const LC_PenLineTypeValue value(RS2::LineByLayer);
        property.setValue(value);
        const QPixmap row = comboRowPixmap(
            RS_Pen(RS_Color(Qt::black), RS2::Width00, RS2::LineByLayer));
        const qreal ratio = ratioOf(row);
        const QImage cell = paintCell(view, cellSize, ratio);
        skipUnlessWholePixelRatio(cell);
        // Among the Color and Line Width cells it reads as their own square
        // By Layer picture, as a property without a drawing does.
        CHECK(cell == paintCellWithoutList(value, cellSize, ratio));
    }

    SECTION("By Block") {
        const LC_PenLineTypeValue value(RS2::LineByBlock);
        property.setValue(value);
        const QPixmap row = comboRowPixmap(
            RS_Pen(RS_Color(Qt::black), RS2::Width00, RS2::LineByBlock));
        const qreal ratio = ratioOf(row);
        const QImage cell = paintCell(view, cellSize, ratio);
        skipUnlessWholePixelRatio(cell);
        CHECK(cell == paintCellWithoutList(value, cellSize, ratio));
    }

    SECTION("a built-in the drawing redefines, without a name") {
        RS_Graphic redefining;
        redefining.initForNewDocument();
        addLineType(redefining, QStringLiteral("DASHED"), {2.0, -6.0});
        LC_PropertyLineType redefinedProperty(nullptr, true);
        redefinedProperty.setLineTypeList(&redefining.getLineTypeList());
        redefinedProperty.setValue(LC_PenLineTypeValue(RS2::DashLine));
        TestLineTypeView redefinedView(&redefinedProperty);

        QG_LineTypeBox redefiningCombo;
        redefiningCombo.init(&redefining.getLineTypeList(), true, false);
        redefiningCombo.setLineTypeOf(RS_Pen(RS_Color(Qt::black), RS2::Width00, RS2::DashLine));
        const int row = redefiningCombo.currentIndex();
        REQUIRE(row >= 0);
        const QPixmap rowPixmap = redefiningCombo.itemIcon(row).pixmap(iconSize);
        const qreal ratio = ratioOf(rowPixmap);
        const QImage cell = paintCell(redefinedView, cellSize, ratio);
        skipUnlessWholePixelRatio(cell);
        CHECK(cell.copy(deviceRect(iconRect, ratio)) ==
              rowPixmap.toImage().convertToFormat(QImage::Format_ARGB32));
        // The row keeps the registry text of the enum.
        CHECK(litPixels(cell, deviceRect(textRect, ratio)) > 0);
        CHECK(QG_LineTypeBox::rowLookOf(&redefining.getLineTypeList(),
                                        RS_Pen(RS_Color(Qt::black), RS2::Width00, RS2::DashLine),
                                        QApplication::palette(), ratio, QFont()).text.isEmpty());
    }

    SECTION("without a list") {
        // Today's drawing: the registry picture in the square rect. Both sides
        // are painted here at one ratio, so no display can change the result.
        const LC_PenLineTypeValue value(RS2::DashLine);
        const QImage cell = paintCellWithoutList(value, cellSize, 1.0);

        QImage expected(cellSize, QImage::Format_ARGB32);
        expected.fill(Qt::transparent);
        {
            QPainter painter(&expected);
            QRect square = QRect(QPoint(0, 0), cellSize);
            square.setWidth(square.height());
            square.adjust(2, 2, -2, -2);
            LC_PenInfoRegistry::instance()->getLineTypeIcon(value.lineType()).paint(&painter, square);
            QRect stockTextRect = QRect(QPoint(0, 0), cellSize);
            stockTextRect.setLeft(square.right() + 3);
            LC_PropertyViewUtils::drawElidedText(
                painter, LC_PenInfoRegistry::instance()->getLineTypeText(value.lineType()),
                stockTextRect, QApplication::style());
        }
        CHECK(cell == expected);
    }
}

TEST_CASE("Property sheet line type pictures", "[.][fixture][linetype-sheet-shots]") {
    const QString outDir = qEnvironmentVariable("LC_SHOT_DIR");
    if (outDir.isEmpty() || !QFileInfo(outDir).isDir()) {
        SKIP("LC_SHOT_DIR does not name an existing directory");
    }
    (void)lc::test::application();
    RS_Graphic graphic;
    graphic.initForNewDocument();
    addLineType(graphic, QStringLiteral("VENDOR_TAB"), {10.0, -5.0});
    addLineType(graphic, QStringLiteral("VENDOR_DOT"), {0.0, -4.0});
    addLineType(graphic, QStringLiteral("DASHED"), {2.0, -6.0});
    SheetFixture fixture;
    RS_Line* defined = addLine(graphic, namedPen(QStringLiteral("VENDOR_TAB")));
    RS_Line* missing = addLine(graphic, namedPen(QStringLiteral("VENDOR_GONE")));
    RS_Line* builtIn = addLine(graphic, RS_Pen(RS_Color(Qt::black), RS2::Width00, RS2::DashLine2));
    RS_Line* byLayer = addLine(graphic, RS_Pen(RS_Color(Qt::black), RS2::Width00, RS2::LineByLayer));
    RS_Line* redefined = addLine(graphic, RS_Pen(RS_Color(Qt::black), RS2::Width00, RS2::DashLine));

    RS_Line* shown = nullptr;
    const auto grabWith = [&](RS_Line* line, const QString& fileName) {
        if (shown != nullptr) {
            graphic.select(QList<RS_Entity*>{shown}, false);
        }
        shown = line;
        graphic.select(QList<RS_Entity*>{shown}, true);
        fixture.show(graphic);
        QList<RS_Entity*> selected;
        graphic.collectSelected(selected);
        REQUIRE(selected.size() == 1);
        CHECK(selected.constFirst() == shown);
        CHECK(fixture.sheet.grab().save(QDir(outDir).filePath(fileName)));
    };

    grabWith(defined, QStringLiteral("sheet_defined.png"));
    grabWith(missing, QStringLiteral("sheet_missing.png"));
    grabWith(builtIn, QStringLiteral("sheet_builtin.png"));
    grabWith(byLayer, QStringLiteral("sheet_bylayer.png"));
    grabWith(redefined, QStringLiteral("sheet_redefined.png"));

    // The editor on the defined name, one line selected, and its popup list.
    graphic.select(QList<RS_Entity*>{shown}, false);
    graphic.select(QList<RS_Entity*>{defined}, true);
    fixture.show(graphic);
    QList<RS_Entity*> selected;
    graphic.collectSelected(selected);
    REQUIRE(selected.size() == 1);
    LC_PropertiesSheet* properties = fixture.properties();
    REQUIRE(properties != nullptr);
    LC_Property* property =
        propertyUnder(properties, QStringLiteral("_secGeneral"), QStringLiteral("$linetype"));
    REQUIRE(property != nullptr);
    REQUIRE(properties->setActiveProperty(property, true));
    (void)properties->grab();
    QKeyEvent activate(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(properties, &activate);
    QCoreApplication::processEvents();
    auto* editor = fixture.sheet.findChild<QComboBox*>(QStringLiteral("LCPropertyValueEditor"));
    REQUIRE(editor != nullptr);
    CHECK(fixture.sheet.grab().save(QDir(outDir).filePath(QStringLiteral("sheet_editor.png"))));

    editor->showPopup();
    QCoreApplication::processEvents();
    if (QAbstractItemView* view = editor->view()) {
        view->scrollToTop();
        QCoreApplication::processEvents();
        CHECK(view->window()->grab().save(
            QDir(outDir).filePath(QStringLiteral("sheet_editor_list.png"))));
    }
    editor->hidePopup();
    QCoreApplication::processEvents();

    // The cell picture of the earlier round is gone.
    QFile::remove(QDir(outDir).filePath(QStringLiteral("sheet_cells.png")));
}
