/*******************************************************************************
 *
 This file is part of the LibreCAD project, a 2D CAD program

 Copyright (C) 2024 LibreCAD.org
 Copyright (C) 2024 sand1024

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 ******************************************************************************/

#include "qg_pentoolbar.h"

#include <QSignalBlocker>

#include "qg_colorbox.h"
#include "qg_linetypebox.h"
#include "qg_widthbox.h"
#include "lc_linetypenames.h"
#include "rs_graphic.h"
#include "rs_graphicview.h"
#include "rs_layer.h"
#include "rs_pen.h"

/**
 * Constructor.
 */
QG_PenToolBar::QG_PenToolBar(const QString& title, QWidget* parent)
    : QToolBar(title, parent), m_currentPen(new RS_Pen{}), m_colorBox(new QG_ColorBox{true, false, this, "colorbox"}),
      m_widthBox(new QG_WidthBox{true, false, this, "widthbox"}), m_lineTypeBox(new QG_LineTypeBox{true, false, this, "lineTypebox"}) {
    m_colorBox->setToolTip(tr("Line color"));
    connect(m_colorBox.get(), &QG_ColorBox::colorChanged, this, &QG_PenToolBar::slotColorChanged);

    m_widthBox->setToolTip(tr("Line width"));
    connect(m_widthBox.get(), &QG_WidthBox::widthChanged, this, &QG_PenToolBar::slotWidthChanged);

    m_lineTypeBox->setToolTip(tr("Line type"));
    connect(m_lineTypeBox.get(), &QG_LineTypeBox::lineTypeChanged, this, &QG_PenToolBar::slotLineTypeChanged);

    m_currentPen->setColor(m_colorBox->getColor());
    m_currentPen->setWidth(m_widthBox->getWidth());
    m_currentPen->setLineType(m_lineTypeBox->getLineType());

    addWidget(m_colorBox.get());
    addWidget(m_widthBox.get());
    addWidget(m_lineTypeBox.get());
}

/**
 * Destructor
 */
QG_PenToolBar::~QG_PenToolBar() = default;

/**
 * The layer list is being destroyed with its drawing, which was not detached from this tool bar first:
 * the list has already dropped this listener, so forget it.
 */
void QG_PenToolBar::layerListDestroyed() {
    m_layerList = nullptr;
}

void QG_PenToolBar::updateByLayer(const RS_Layer* l) const {
    if (l == nullptr) {
        return;
    }
    const auto& pen = l->getPen();
    m_colorBox->setLayerColor(pen.getColor());
    m_widthBox->setLayerWidth(pen.getWidth());
    m_lineTypeBox->setLayerLineTypeOf(pen);
}

/**
 * Called by the layer list if this object was added as a listener
 * to a layer list.
 */
void QG_PenToolBar::layerActivated(RS_Layer* l) {
    updateByLayer(l);
}

/**
 * Called by the layer list when another layer became current, as the current one was locked.
 */
void QG_PenToolBar::activeLayerReplaced(RS_Layer* l) {
    updateByLayer(l);
}

void QG_PenToolBar::setLayerColor(const RS_Color& color, const bool updateSelection) {
    m_colorBox->setLayerColor(color);
    if (updateSelection) {
        m_colorBox->setCurrentIndex(0);
    }
    m_currentPen->setColor(color);
    emitPenChanged();
}

void QG_PenToolBar::setColor(const RS_Color& color) const {
    m_colorBox->setColor(color);
}

void QG_PenToolBar::setLayerLineType(const RS2::LineType lineType, const bool updateSelection) {
    m_lineTypeBox->setLayerLineType(lineType);
    if (updateSelection) {
        m_lineTypeBox->setCurrentIndex(0);
    }
    m_currentPen->setLineType(lineType);
    emitPenChanged();
}

void QG_PenToolBar::setLineType(const RS2::LineType lineType) const {
    // An enum with no pen behind it: set it as a pen without a name, so the
    // combo cannot hand back the name of a pen it was given earlier.
    RS_Pen pen;
    pen.setLineType(lineType);
    m_lineTypeBox->setLineTypeOf(pen);
}

void QG_PenToolBar::setLineType(const RS_Pen& pen) const {
    m_lineTypeBox->setLineTypeOf(pen);
}

void QG_PenToolBar::setLayerLineType(const RS_Pen& layerPen, const bool updateSelection) {
    {
        // The By Layer icon repaint needs no signal; only the final penChanged
        // may carry the layer pen.
        const QSignalBlocker blocker(m_lineTypeBox.get());
        m_lineTypeBox->setLayerLineTypeOf(layerPen);
    }
    if (updateSelection) {
        m_lineTypeBox->setCurrentIndex(0);
    }
    m_currentPen->setLineTypeFromPen(layerPen);
    emitPenChanged();
}

void QG_PenToolBar::setLayerWidth(const RS2::LineWidth width, const bool updateSelection) {
    m_widthBox->setLayerWidth(width);
    if (updateSelection) {
        m_widthBox->setCurrentIndex(0);
    }
    m_currentPen->setWidth(width);
    emitPenChanged();
}

void QG_PenToolBar::setWidth(const RS2::LineWidth width) const {
    m_widthBox->setWidth(width);
}

void QG_PenToolBar::emitPenChanged() {
    emit penChanged(*m_currentPen);
}

void QG_PenToolBar::setLayerList(RS_LayerList* ll) {
    if (m_layerList != nullptr) {
        m_layerList->removeListener(this);
    }
    m_layerList = ll;
    if (ll != nullptr) {
        m_layerList->addListener(this);
        const auto activeLayer = m_layerList->getActive();
        if (activeLayer != nullptr) {
            updateByLayer(activeLayer);
        }
    }
}

void QG_PenToolBar::setGraphicView(RS_GraphicView* gview) {
    // Refill first: setLayerList() repaints the By Layer row, and a refill after
    // it would reset that icon. The table that applies is the block-owning
    // drawing's for a block window.
    RS_Graphic* tableGraphic = gview != nullptr ? gview->getGraphic(true) : nullptr;
    if (tableGraphic != nullptr && m_lineTypeDefinition.has_value()) {
        // The new layer list reports its layer, and the pen then takes what the
        // row shown writes. Only that line type moves on: a pen that holds a
        // layer's line type behind the By Layer row becomes By Layer again.
        RS_Pen shown = *m_currentPen;
        m_lineTypeBox->applyLineTypeTo(shown);
        if (shown.hasLineTypeName() &&
            m_lineTypeDefinition->key() == LC_LineTypeNames::foldName(shown.getLineTypeName())) {
            // The pen's line type keeps working, and keeps looking right, here too.
            tableGraphic->adoptLineType(*m_lineTypeDefinition);
        }
    }
    m_lineTypeBox->rebuild(tableGraphic != nullptr ? &tableGraphic->getLineTypeList() : nullptr);
    if (gview == nullptr) {
        setLayerList(nullptr);
    }
    else {
        const auto graphic = gview->getGraphic();
        if (graphic == nullptr) {
            setLayerList(nullptr);
        }
        else {
            const auto layerList = graphic->getLayerList();
            setLayerList(layerList);
        }
    }
    // A view without a drawing has no rows for names, so the row shown may no
    // longer say what the active pen holds: show the pen's own row again, quietly.
    const RS2::LineType shown = m_lineTypeBox->getLineType();
    if (m_currentPen->hasLineTypeName() && shown != RS2::LineByLayer && shown != RS2::LineByBlock) {
        const QSignalBlocker blocker(m_lineTypeBox.get());
        m_lineTypeBox->setLineTypeOf(*m_currentPen);
    }
    // The drawing shown is the last word on what the pen's line type looks
    // like; the next drawing takes that definition.
    rememberLineTypeDefinition();
}

RS_Pen QG_PenToolBar::getPen() const {
    return *m_currentPen;
}

/**
 * Called by the layer list (if this object was previously
 * added as a listener to a layer list).
 */
void QG_PenToolBar::layerEdited(RS_Layer* l) {
    updateByLayer(l);
}

/**
 * Called when the color was changed by the user.
 */
void QG_PenToolBar::slotColorChanged(const RS_Color& color) {
    m_currentPen->setColor(color);
    //printf("  color changed\n");

    emit penChanged(*m_currentPen);
}

/**
 * Called when the width was changed by the user.
 */
void QG_PenToolBar::slotWidthChanged(const RS2::LineWidth w) {
    m_currentPen->setWidth(w);
    //printf("  width changed\n");

    emit penChanged(*m_currentPen);
}

/**
 * Called when the linetype was changed by the user.
 */
void QG_PenToolBar::slotLineTypeChanged([[maybe_unused]] const RS2::LineType w) {
    m_lineTypeBox->applyLineTypeTo(*m_currentPen);
    rememberLineTypeDefinition();
    //printf("  line type changed\n");

    emit penChanged(*m_currentPen);
}

void QG_PenToolBar::rememberLineTypeDefinition() {
    if (!m_currentPen->hasLineTypeName()) {
        m_lineTypeDefinition.reset();
        return;
    }
    const std::optional<LC_LineType> entry = m_lineTypeBox->definitionOf(*m_currentPen);
    if (entry.has_value()) {
        m_lineTypeDefinition = entry;
    }
    else if (m_lineTypeDefinition.has_value() &&
             m_lineTypeDefinition->key() !=
                 LC_LineTypeNames::foldName(m_currentPen->getLineTypeName())) {
        // A remembered definition of another name would describe a line type
        // this pen does not name.
        m_lineTypeDefinition.reset();
    }
}
