/*
 * Copyright (C) 2000 Lars Knoll (knoll@kde.org)
 *           (C) 2000 Antti Koivisto (koivisto@kde.org)
 *           (C) 2000 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2003-2024 Apple Inc. All rights reserved.
 * Copyright (C) 2006 Graham Dennis (graham.dennis@gmail.com)
 * Copyright (C) 2025 Samuel Weinig <sam@webkit.org>
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
 *
 */

#pragma once

#include "CSSPropertyNames.h"
#include "CounterDirectives.h"
#include "LengthPoint.h"
#include "LineClampValue.h"
#include "NameScope.h"
#include "NinePieceImage.h"
#include "PositionArea.h"
#include "PositionTryFallback.h"
#include "ScopedName.h"
#include "ScrollAxis.h"
#include "ScrollTimeline.h"
#include "ScrollTypes.h"
#include "ScrollbarGutter.h"
#include "ShapeValue.h"
#include "StyleClipPath.h"
#include "StyleColor.h"
#include "StyleContentAlignmentData.h"
#include "StyleGapGutter.h"
#include "StyleOffsetAnchor.h"
#include "StyleOffsetDistance.h"
#include "StyleOffsetPath.h"
#include "StyleOffsetPosition.h"
#include "StyleOffsetRotate.h"
#include "StylePerspective.h"
#include "StylePrimitiveNumericTypes.h"
#include "StyleRotate.h"
#include "StyleScale.h"
#include "StyleScrollMargin.h"
#include "StyleScrollPadding.h"
#include "StyleScrollSnapPoints.h"
#include "StyleSelfAlignmentData.h"
#include "StyleTextEdge.h"
#include "StyleTranslate.h"
#include "TextDecorationThickness.h"
#include "TouchAction.h"
#include "ViewTimeline.h"
#include "ViewTransitionName.h"
#include <memory>
#include <wtf/DataRef.h>
#include <wtf/FixedVector.h>
#include <wtf/Markable.h>
#include <wtf/OptionSet.h>

namespace WTF {
class TextStream;
}

namespace WebCore {

using namespace CSS::Literals;

class AnimationList;
class ContentData;
class PathOperation;
class StyleCustomPropertyData;
class StyleDeprecatedFlexibleBoxData;
class StyleFilterData;
class StyleFlexibleBoxData;
class StyleGridData;
class StyleGridItemData;
class StyleMultiColData;
class StyleReflection;
class StyleResolver;
class StyleTransformData;
class WillChangeData;

struct LengthSize;
struct StyleMarqueeData;

namespace Style {
class CustomPropertyData;
}

// Page size type.
// StyleRareNonInheritedData::pageSize is meaningful only when
// StyleRareNonInheritedData::pageSizeType is PAGE_SIZE_RESOLVED.
enum class PageSizeType : uint8_t {
    Auto, // size: auto
    AutoLandscape, // size: landscape
    AutoPortrait, // size: portrait
    Resolved // Size is fully resolved.
};

// This struct is for rarely used non-inherited CSS3, CSS2, and WebKit-specific properties.
// By grouping them together, we save space, and only allocate this object when someone
// actually uses one of these properties.
DECLARE_ALLOCATOR_WITH_HEAP_IDENTIFIER(StyleRareNonInheritedData);
class StyleRareNonInheritedData : public RefCounted<StyleRareNonInheritedData> {
    WTF_MAKE_FAST_ALLOCATED_WITH_HEAP_IDENTIFIER(StyleRareNonInheritedData);
public:
    static Ref<StyleRareNonInheritedData> create() { return adoptRef(*new StyleRareNonInheritedData); }
    Ref<StyleRareNonInheritedData> copy() const;
    ~StyleRareNonInheritedData();
    
    bool operator==(const StyleRareNonInheritedData&) const;

#if !LOG_DISABLED
    void dumpDifferences(TextStream&, const StyleRareNonInheritedData&) const;
#endif

    LengthPoint perspectiveOrigin() const { return { perspectiveOriginX, perspectiveOriginY }; }

    bool hasBackdropFilters() const;

    bool hasScrollTimelines() const
    {
        return scrollTimelines.size() || scrollTimelineNames.size();
    }

    bool hasViewTimelines() const
    {
        return viewTimelines.size() || viewTimelineNames.size();
    }

    OptionSet<Containment> usedContain() const;

    Markable<Length> containIntrinsicWidth;
    Markable<Length> containIntrinsicHeight;

    Length perspectiveOriginX;
    Length perspectiveOriginY;

    LineClampValue lineClamp; // An Apple extension.

    float zoom;

    size_t maxLines { 0 };

    OverflowContinue overflowContinue { OverflowContinue::Auto };

    OptionSet<TouchAction> touchActions;
    OptionSet<MarginTrimType> marginTrim;
    OptionSet<Containment> contain;

    IntSize initialLetter;

    DataRef<StyleMarqueeData> marquee; // Marquee properties

    DataRef<StyleFilterData> backdropFilter; // Filter operations (url, sepia, blur, etc.)

    DataRef<StyleGridData> grid;
    DataRef<StyleGridItemData> gridItem;

    // Only meaningful when `hasClip` is true.
    LengthBox clip;

    Style::ScrollMarginBox scrollMargin { 0_css_px };
    Style::ScrollPaddingBox scrollPadding { CSS::Keyword::Auto { } };

    CounterDirectiveMap counterDirectives;

    RefPtr<WillChangeData> willChange; // Null indicates 'auto'.
    
    RefPtr<StyleReflection> boxReflect;

    NinePieceImage maskBorder;

    LengthSize pageSize;

    RefPtr<ShapeValue> shapeOutside;
    Length shapeMargin;
    float shapeImageThreshold;

    Style::Perspective perspective;

    Style::ClipPath clipPath;

    Style::Color textDecorationColor;

    DataRef<Style::CustomPropertyData> customProperties;
    HashSet<AtomString> customPaintWatchedProperties;

    Style::Rotate rotate;
    Style::Scale scale;
    Style::Translate translate;

    FixedVector<Style::ScopedName> containerNames;

    FixedVector<Style::ScopedName> viewTransitionClasses;
    Style::ViewTransitionName viewTransitionName;

    Style::GapGutter columnGap;
    Style::GapGutter rowGap;

    Style::OffsetPath offsetPath;
    Style::OffsetDistance offsetDistance;
    Style::OffsetPosition offsetPosition;
    Style::OffsetAnchor offsetAnchor;
    Style::OffsetRotate offsetRotate;

    TextDecorationThickness textDecorationThickness;

    FixedVector<Ref<ScrollTimeline>> scrollTimelines;
    FixedVector<ScrollAxis> scrollTimelineAxes;
    FixedVector<AtomString> scrollTimelineNames;

    FixedVector<Ref<ViewTimeline>> viewTimelines;
    FixedVector<ScrollAxis> viewTimelineAxes;
    FixedVector<ViewTimelineInsets> viewTimelineInsets;
    FixedVector<AtomString> viewTimelineNames;

    NameScope timelineScope;

    ScrollbarGutter scrollbarGutter;

    ScrollSnapType scrollSnapType;
    ScrollSnapAlign scrollSnapAlign;
    ScrollSnapStop scrollSnapStop { ScrollSnapStop::Normal };

    AtomString pseudoElementNameArgument;

    FixedVector<Style::ScopedName> anchorNames;
    NameScope anchorScope;
    std::optional<Style::ScopedName> positionAnchor;
    std::optional<PositionArea> positionArea;
    FixedVector<Style::PositionTryFallback> positionTryFallbacks;

    std::optional<Length> blockStepSize;
    unsigned blockStepAlign : 2; // BlockStepAlign
    unsigned blockStepInsert : 2; // BlockStepInsert
    unsigned blockStepRound : 2; // BlockStepRound

    unsigned overscrollBehaviorX : 2; // OverscrollBehavior
    unsigned overscrollBehaviorY : 2; // OverscrollBehavior

    unsigned pageSizeType : 2; // PageSizeType
    unsigned transformStyle3D : 2; // TransformStyle3D
    unsigned transformStyleForcedToFlat : 1; // The used value for transform-style is forced to flat by a grouping property.
    unsigned backfaceVisibility : 1; // BackfaceVisibility

    unsigned useSmoothScrolling : 1; // ScrollBehavior

    unsigned textDecorationStyle : 3; // TextDecorationStyle

    unsigned textGroupAlign : 3; // TextGroupAlign

    unsigned contentVisibility : 2; // ContentVisibility

    unsigned effectiveBlendMode: 5; // BlendMode
    unsigned isolation : 1; // Isolation

    unsigned inputSecurity : 1; // InputSecurity

#if ENABLE(APPLE_PAY)
    unsigned applePayButtonStyle : 2; // ApplePayButtonStyle
    unsigned applePayButtonType : 4; // ApplePayButtonType
#endif

    unsigned breakBefore : 4; // BreakBetween
    unsigned breakAfter : 4; // BreakBetween
    unsigned breakInside : 3; // BreakInside

    unsigned containIntrinsicWidthType : 2; // ContainIntrinsicSizeType
    unsigned containIntrinsicHeightType : 2; // ContainIntrinsicSizeType

    unsigned containerType : 2; // ContainerType

    unsigned textBoxTrim : 2; // TextBoxTrim

    unsigned overflowAnchor : 1; // Scroll Anchoring - OverflowAnchor

    bool hasClip : 1;

    unsigned positionTryOrder : 3; // Style::PositionTryOrder; 5 values so 3 bits.
    unsigned positionVisibility : 3; // OptionSet<PositionVisibilty>

    unsigned fieldSizing : 1; // FieldSizing

    unsigned nativeAppearanceDisabled : 1;

#if HAVE(CORE_MATERIAL)
    unsigned appleVisualEffect : 5; // AppleVisualEffect
#endif

    unsigned scrollbarWidth : 2; // ScrollbarWidth

    unsigned usesAnchorFunctions : 1;
    unsigned anchorFunctionScrollCompensatedAxes : 2;

    unsigned usesTreeCountingFunctions : 1;

    unsigned isPopoverInvoker : 1;

private:
    StyleRareNonInheritedData();
    StyleRareNonInheritedData(const StyleRareNonInheritedData&);
};

} // namespace WebCore
