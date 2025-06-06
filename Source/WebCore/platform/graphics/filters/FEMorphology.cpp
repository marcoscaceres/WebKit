/*
 * Copyright (C) 2004, 2005, 2006, 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005 Rob Buis <buis@kde.org>
 * Copyright (C) 2005 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
 * Copyright (C) 2017-2022 Apple Inc. All rights reserved.
 * Copyright (C) 2015 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "FEMorphology.h"

#include "Filter.h"
#include "FEMorphologySoftwareApplier.h"
#include <wtf/text/TextStream.h>

namespace WebCore {

Ref<FEMorphology> FEMorphology::create(MorphologyOperatorType type, float radiusX, float radiusY, DestinationColorSpace colorSpace)
{
    return adoptRef(*new FEMorphology(type, radiusX, radiusY, colorSpace));
}

FEMorphology::FEMorphology(MorphologyOperatorType type, float radiusX, float radiusY, DestinationColorSpace colorSpace)
    : FilterEffect(FilterEffect::Type::FEMorphology, colorSpace)
    , m_type(type)
    , m_radiusX(std::max(0.0f, radiusX))
    , m_radiusY(std::max(0.0f, radiusY))
{
}

bool FEMorphology::operator==(const FEMorphology& other) const
{
    return FilterEffect::operator==(other)
        && m_type == other.m_type
        && m_radiusX == other.m_radiusX
        && m_radiusY == other.m_radiusY;
}

bool FEMorphology::setMorphologyOperator(MorphologyOperatorType type)
{
    if (m_type == type)
        return false;
    m_type = type;
    return true;
}

bool FEMorphology::setRadiusX(float radiusX)
{
    radiusX = std::max(0.0f, radiusX);
    if (m_radiusX == radiusX)
        return false;
    m_radiusX = radiusX;
    return true;
}

bool FEMorphology::setRadiusY(float radiusY)
{
    radiusY = std::max(0.0f, radiusY);
    if (m_radiusY == radiusY)
        return false;
    m_radiusY = radiusY;
    return true;
}

FloatRect FEMorphology::calculateImageRect(const Filter& filter, std::span<const FloatRect> inputImageRects, const FloatRect& primitiveSubregion) const
{
    auto imageRect = inputImageRects[0];
    imageRect.inflate(filter.resolvedSize({ m_radiusX, m_radiusY }));
    return filter.clipToMaxEffectRect(imageRect, primitiveSubregion);
}

bool FEMorphology::resultIsAlphaImage(std::span<const Ref<FilterImage>> inputs) const
{
    return inputs[0]->isAlphaImage();
}

std::unique_ptr<FilterEffectApplier> FEMorphology::createSoftwareApplier() const
{
    return FilterEffectApplier::create<FEMorphologySoftwareApplier>(*this);
}

static TextStream& operator<<(TextStream& ts, const MorphologyOperatorType& type)
{
    switch (type) {
    case MorphologyOperatorType::Unknown:
        ts << "UNKNOWN"_s;
        break;
    case MorphologyOperatorType::Erode:
        ts << "ERODE"_s;
        break;
    case MorphologyOperatorType::Dilate:
        ts << "DILATE"_s;
        break;
    }
    return ts;
}

TextStream& FEMorphology::externalRepresentation(TextStream& ts, FilterRepresentation representation) const
{
    ts << indent << "[feMorphology"_s;
    FilterEffect::externalRepresentation(ts, representation);

    ts << " operator=\""_s << morphologyOperator() << '"';
    ts << " radius=\""_s << radiusX() << ", "_s << radiusY() << '"';

    ts << "]\n"_s;
    return ts;
}

} // namespace WebCore
