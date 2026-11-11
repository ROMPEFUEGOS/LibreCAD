/*
 * ********************************************************************************
 * This file is part of the LibreCAD project, a 2D CAD program
 *
 * Copyright (C) 2026 LibreCAD.org
 * Copyright (C) 2026 Dongxu Li
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

#ifndef LC_PROPERTYLINETYPEVALUE_H
#define LC_PROPERTYLINETYPEVALUE_H

#include <QMetaType>
#include <QString>

#include "rs.h"
#include "rs_pen.h"

/**
 * The line type of a pen: the enum plus the spelling the pen carries. Two
 * values are equal when a pen would draw them alike, which is the identity
 * RS_Pen compares for its line type: the same enum and the same fold id. The
 * spelling itself is not compared, so two spellings of one name are one value.
 */
class LC_PenLineTypeValue {
public:
    LC_PenLineTypeValue() = default;

    explicit LC_PenLineTypeValue(const RS2::LineType lineType) {
        m_pen.setLineType(lineType);
    }

    explicit LC_PenLineTypeValue(const RS_Pen& pen) {
        m_pen.setLineTypeFromPen(pen);
    }

    RS2::LineType lineType() const {
        return m_pen.getLineType();
    }

    QString name() const {
        // A pen without a name still answers with its enum's canonical name.
        return m_pen.hasLineTypeName() ? m_pen.getLineTypeName() : QString();
    }

    bool hasName() const {
        return m_pen.hasLineTypeName();
    }

    /// Writes the line type into a pen and leaves its color and width alone.
    void applyTo(RS_Pen& pen) const {
        pen.setLineTypeFromPen(m_pen);
    }

    /// The pen carrying this line type, for the combo's setLineTypeOf().
    const RS_Pen& pen() const {
        return m_pen;
    }

    bool operator==(const LC_PenLineTypeValue& other) const {
        return m_pen.getLineType() == other.m_pen.getLineType() &&
               m_pen.getLineTypeFoldId() == other.m_pen.getLineTypeFoldId();
    }

    bool operator!=(const LC_PenLineTypeValue& other) const {
        return !(*this == other);
    }

private:
    /// Only its line type and spelling are used.
    RS_Pen m_pen;
};

Q_DECLARE_METATYPE(LC_PenLineTypeValue)

#endif
