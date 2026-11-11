/*
 * ********************************************************************************
 * This file is part of the LibreCAD project, a 2D CAD program
 *
 * Copyright (C) 2026 LibreCAD.org
 * Copyright (C) 2026 sand1024
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * ********************************************************************************
 */

#include "lc_property_linetype_combobox_view.h"

#include <algorithm>

#include <QApplication>
#include <QWidget>

#include "lc_linetypelist.h"
#include "lc_peninforegistry.h"
#include "lc_property_linetype_combobox.h"
#include "lc_property_view_utils.h"
#include "qg_linetypebox.h"

const QByteArray LC_PropertyLineTypeComboboxView::VIEW_NAME = QByteArrayLiteral("LinetypeCombobox");
const QByteArray LC_PropertyLineTypeComboboxView::ATTR_SHOW_BY_LAYER = QByteArrayLiteral("showByLayer");

void LC_PropertyLineTypeComboboxView::doDrawValueDetails(const QStyle* style, const LC_PenLineTypeValue& value,
                                                         QPainter& painter, const QRect& rect) const {
    const auto pensRegistry = LC_PenInfoRegistry::instance();
    QRect textRect = rect;

    const LC_LineTypeList* list = this->typedProperty().lineTypeList();
    // By Layer and By Block have no pattern to show: they keep the square
    // picture, in line with the Color and Line Width cells beside them.
    const bool baseDrawing = list == nullptr || value.lineType() == RS2::LineByLayer ||
                             value.lineType() == RS2::LineByBlock;
    if (baseDrawing) {
        // The stock square picture and the registry text, as before.
        const auto lineTypeIcon = pensRegistry->getLineTypeIcon(value.lineType());
        QRect iconRect = rect;
        iconRect.setWidth(iconRect.height());
        iconRect.adjust(2, 2, -2, -2);
        lineTypeIcon.paint(&painter, iconRect);
        textRect.setLeft(iconRect.right() + 3);
        if (textRect.isValid()) {
            LC_PropertyViewUtils::drawElidedText(painter, pensRegistry->getLineTypeText(value.lineType()),
                                                 textRect, style);
        }
        return;
    }

    // The row's picture at the combo's list icon size, 2 px in and centered; a
    // cell lower than the icon lets QIcon::paint scale it down.
    const qreal ratio = painter.device() != nullptr ? painter.device()->devicePixelRatioF() : 1.0;
    const QWidget* widget = dynamic_cast<const QWidget*>(painter.device());
    const QPalette palette = widget != nullptr ? widget->palette() : QApplication::palette();
    const QG_LineTypeBox::RowLook look = QG_LineTypeBox::rowLookOf(
        list, value.pen(), palette, ratio, painter.font());
    const QSize iconSize = QG_LineTypeBox::listIconSize();
    const int iconHeight = std::min(iconSize.height(), rect.height());
    const QRect iconRect(rect.left() + 2, rect.top() + (rect.height() - iconHeight) / 2,
                         iconSize.width(), iconHeight);
    if (iconRect.isValid()) {
        // The row's own picture, or the stock picture of the enum when the
        // drawing leaves the row to the host.
        if (!look.icon.isNull()) {
            look.icon.paint(&painter, iconRect);
        }
        else {
            pensRegistry->getLineTypeIcon(value.lineType()).paint(&painter, iconRect);
        }
    }
    textRect.setLeft(iconRect.right() + 3);
    if (!textRect.isValid()) {
        return;
    }
    // The row's text, or the registry text when the row keeps the stock one.
    const QString text = look.text.isEmpty() ? pensRegistry->getLineTypeText(value.lineType())
                                             : look.text;
    LC_PropertyViewUtils::drawElidedText(painter, text, textRect, style);
}

void LC_PropertyLineTypeComboboxView::doApplyAttributes(const LC_PropertyViewDescriptor& info) {
    info.load(ATTR_SHOW_BY_LAYER, m_showByLayer);
}

QComboBox* LC_PropertyLineTypeComboboxView::doCreateEditCombobox(QWidget* parent) {
    return new LC_PropertyLineTypeCombobox(this, parent, m_showByLayer, false);
}
