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
#include "lc_property_linetype_combobox.h"

#include "lc_peninforegistry.h"
#include "lc_property_linetype_combobox_view.h"

LC_PropertyLineTypeCombobox::LC_PropertyLineTypeCombobox(LC_PropertyLineTypeComboboxView* view, QWidget* parent, const bool showByLayer,
                                                         const bool showUnchanged)
    : QG_LineTypeBox(showByLayer, showUnchanged, parent), m_view(view), m_property(static_cast<LC_PropertyLineType*>(view->getProperty())) {
    setLineEdit(nullptr);
    // The drawing's rows come first: the value may name one of them.
    rebuild(m_property->lineTypeList());
    updateByProperty();

    connect(m_property, &LC_Property::afterPropertyChange, this, &LC_PropertyLineTypeCombobox::onPropertyDidChange);
    connect(this, &QG_LineTypeBox::lineTypeChanged, this, &LC_PropertyLineTypeCombobox::onLinetypeChanged);
}

void LC_PropertyLineTypeCombobox::hidePopup() {
    QG_LineTypeBox::hidePopup();
    m_view->stopInplaceEditing(true, true);
}

void LC_PropertyLineTypeCombobox::updateByProperty() {
    m_updating++;
    const auto stateProperty = m_view->getStateProperty();
    setEnabled(stateProperty->isEditableByUser());
    // The pen view selects a drawing row by its name, a stock row by its enum.
    setLineTypeOf(m_property->value().pen());
    m_updating--;
}

void LC_PropertyLineTypeCombobox::onPropertyDidChange(const LC_PropertyChangeReason reason) {
    if (!(reason & PropertyChangeReasonValue)) {
        updateByProperty();
    }
}

void LC_PropertyLineTypeCombobox::onLinetypeChanged([[maybe_unused]] const RS2::LineType& linetype) {
    if (m_updating) {
        return;
    }
    // The row holds the whole line type: the name a drawing row writes and the
    // enum of a stock row.
    RS_Pen pen = m_property->value().pen();
    applyLineTypeTo(pen);
    m_property->setValue(LC_PenLineTypeValue(pen), m_view->changeReasonDueToEdit());
}
