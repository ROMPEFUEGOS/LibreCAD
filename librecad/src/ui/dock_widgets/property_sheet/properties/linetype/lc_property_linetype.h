/*
 * ********************************************************************************
 * This file is part of the LibreCAD project, a 2D CAD program
 *
 * Copyright (C) 2025 LibreCAD.org
 * Copyright (C) 2025 sand1024
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

#ifndef LC_PROPERTYLINETYPE_H
#define LC_PROPERTYLINETYPE_H

#include "lc_property_linetype_value.h"
#include "lc_property_single.h"

class LC_LineTypeList;

class LC_PropertyLineType : public LC_PropertySingle<LC_PenLineTypeValue> {
    Q_OBJECT

public:
    using ValueType = LC_PenLineTypeValue;
    LC_PropertyLineType(const LC_PropertyLineType& other) = delete;

    explicit LC_PropertyLineType(QObject* parent, bool holdValue = true)
        : LC_PropertySingle(parent, holdValue) {
    }

    /// The drawing's table the value's row is read from and offered in; the
    /// sheet sets it while it builds the property, and the drawing outlives it.
    void setLineTypeList(const LC_LineTypeList* list) {
        m_lineTypeList = list;
    }

    const LC_LineTypeList* lineTypeList() const {
        return m_lineTypeList;
    }

private:
    const LC_LineTypeList* m_lineTypeList = nullptr;
};

#endif
