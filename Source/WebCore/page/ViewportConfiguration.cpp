/*
 * Copyright (C) 2005-2014 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "ViewportConfiguration.h"

#include "Logging.h"
#include <wtf/Assertions.h>
#include <wtf/MathExtras.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/CString.h>
#include <wtf/text/TextStream.h>

#if PLATFORM(IOS_FAMILY)
#include "PlatformScreen.h"
#endif

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(ViewportConfiguration);

static inline bool viewportArgumentValueIsValid(float value)
{
    return value > 0;
}

static inline void adjustViewportArgumentsToAvoidExcessiveZooming(ViewportArguments& arguments)
{
    if (!viewportArgumentValueIsValid(arguments.zoom) || !viewportArgumentValueIsValid(arguments.width))
        return;

    constexpr float maximumInitialZoomScale = 1.2;
    auto zoomedWidthFromArguments = arguments.zoom * arguments.width;
    arguments.zoom = std::min(arguments.zoom, maximumInitialZoomScale);
    arguments.width = zoomedWidthFromArguments / arguments.zoom;
}

static inline void ignoreViewportArgumentsToAvoidEnlargedView(ViewportArguments& arguments, FloatSize viewLayoutSize)
{
    if (!viewportArgumentValueIsValid(arguments.width))
        return;

    if (arguments.width < viewLayoutSize.width())
        arguments.width = 0;
}

constexpr double defaultDesktopViewportWidth = 980;
constexpr double minimumShrinkToFitWidthWhenPreferringHorizontalScrolling = 820;

#if ASSERT_ENABLED
static bool constraintsAreAllRelative(const ViewportConfiguration::Parameters& configuration)
{
    return !configuration.widthIsSet && !configuration.heightIsSet && !configuration.initialScaleIsSet;
}
#endif // ASSERT_ENABLED

static constexpr float platformDeviceWidthOverride()
{
#if PLATFORM(WATCHOS)
    return 320;
#else
    return 0;
#endif
}

static constexpr double platformMinimumScaleForWebpage()
{
#if PLATFORM(WATCHOS)
    return 0.1;
#else
    return 0.25;
#endif
}

static constexpr bool shouldOverrideShrinkToFitArgument()
{
#if PLATFORM(WATCHOS)
    return true;
#else
    return false;
#endif
}

static bool needsUpdateAfterChangingDisabledAdaptations(const OptionSet<DisabledAdaptations>& oldDisabledAdaptations, const OptionSet<DisabledAdaptations>& newDisabledAdaptations)
{
    if (oldDisabledAdaptations == newDisabledAdaptations)
        return false;

#if PLATFORM(WATCHOS)
    if (oldDisabledAdaptations.contains(DisabledAdaptations::Watch) != newDisabledAdaptations.contains(DisabledAdaptations::Watch))
        return true;
#endif

    return false;
}

// Setup a reasonable default configuration to avoid computing infinite scale/sizes.
// Those are the original iPhone configuration.
ViewportConfiguration::ViewportConfiguration()
    : m_defaultConfiguration(ViewportConfiguration::webpageParameters())
    , m_minimumLayoutSize(1024, 768)
    , m_viewLayoutSize(1024, 768)
    , m_canIgnoreScalingConstraints(false)
    , m_forceAlwaysUserScalable(false)
{
    updateConfiguration();
}

void ViewportConfiguration::setDefaultConfiguration(const ViewportConfiguration::Parameters& defaultConfiguration)
{
    ASSERT(!constraintsAreAllRelative(m_configuration));
    ASSERT(!defaultConfiguration.initialScaleIsSet || defaultConfiguration.initialScale > 0);
    ASSERT(defaultConfiguration.minimumScale > 0);
    ASSERT(defaultConfiguration.maximumScale >= defaultConfiguration.minimumScale);

    if (m_defaultConfiguration == defaultConfiguration)
        return;

    m_defaultConfiguration = defaultConfiguration;
    updateConfiguration();
}

bool ViewportConfiguration::setContentsSize(const IntSize& contentSize)
{
    if (m_contentSize == contentSize)
        return false;

    LOG_WITH_STREAM(Viewports, stream << "ViewportConfiguration::setContentsSize " << contentSize << " (was " << m_contentSize << ")");

    m_contentSize = contentSize;
    updateConfiguration();
    return true;
}

bool ViewportConfiguration::setViewLayoutSize(const FloatSize& viewLayoutSize, std::optional<double>&& scaleFactor, std::optional<double>&& minimumEffectiveDeviceWidthFromClient)
{
    double newScaleFactor = scaleFactor.value_or(m_layoutSizeScaleFactor);
    double newEffectiveWidth = [&] {
        if (!m_configuration.shouldHonorMinimumEffectiveDeviceWidthFromClient)
            return m_minimumEffectiveDeviceWidthForView;

        if (!minimumEffectiveDeviceWidthFromClient)
            return m_minimumEffectiveDeviceWidthForView;

        m_minimumEffectiveDeviceWidthWasSetByClient = true;
        return *minimumEffectiveDeviceWidthFromClient;
    }();

    if (m_viewLayoutSize == viewLayoutSize && m_layoutSizeScaleFactor == newScaleFactor && newEffectiveWidth == m_minimumEffectiveDeviceWidthForView)
        return false;

    m_layoutSizeScaleFactor = newScaleFactor;
    m_viewLayoutSize = viewLayoutSize;
    m_minimumEffectiveDeviceWidthForView = newEffectiveWidth;

    updateMinimumLayoutSize();
    updateConfiguration();
    return true;
}

bool ViewportConfiguration::setDisabledAdaptations(const OptionSet<DisabledAdaptations>& disabledAdaptations)
{
    auto previousDisabledAdaptations = m_disabledAdaptations;
    m_disabledAdaptations = disabledAdaptations;

    if (!needsUpdateAfterChangingDisabledAdaptations(previousDisabledAdaptations, disabledAdaptations))
        return false;

    updateMinimumLayoutSize();
    updateConfiguration();
    return true;
}

bool ViewportConfiguration::canOverrideConfigurationParameters() const
{
    return m_defaultConfiguration == ViewportConfiguration::nativeWebpageParametersWithoutShrinkToFit() || m_defaultConfiguration == ViewportConfiguration::nativeWebpageParametersWithShrinkToFit();
}

void ViewportConfiguration::updateDefaultConfiguration()
{
    if (!canOverrideConfigurationParameters())
        return;

    m_defaultConfiguration = nativeWebpageParameters();
}

bool ViewportConfiguration::setViewportArguments(const ViewportArguments& viewportArguments)
{
    if (m_viewportArguments == viewportArguments)
        return false;

    LOG_WITH_STREAM(Viewports, stream << "ViewportConfiguration::setViewportArguments " << viewportArguments);
    m_viewportArguments = viewportArguments;

    if (m_canIgnoreViewportArgumentsToAvoidExcessiveZoom)
        adjustViewportArgumentsToAvoidExcessiveZooming(m_viewportArguments);

    if (m_canIgnoreViewportArgumentsToAvoidEnlargedView)
        ignoreViewportArgumentsToAvoidEnlargedView(m_viewportArguments, m_viewLayoutSize);

    updateDefaultConfiguration();
    updateMinimumLayoutSize();
    updateConfiguration();
    return true;
}

bool ViewportConfiguration::setCanIgnoreScalingConstraints(bool canIgnoreScalingConstraints)
{
    if (canIgnoreScalingConstraints == m_canIgnoreScalingConstraints)
        return false;
    
    m_canIgnoreScalingConstraints = canIgnoreScalingConstraints;
    updateDefaultConfiguration();
    updateMinimumLayoutSize();
    updateConfiguration();
    return true;
}

IntSize ViewportConfiguration::layoutSize() const
{
    return IntSize(layoutWidth(), layoutHeight());
}

bool ViewportConfiguration::shouldOverrideDeviceWidthAndShrinkToFit() const
{
    if (m_disabledAdaptations.contains(DisabledAdaptations::Watch))
        return false;

    auto viewWidth = m_viewLayoutSize.width();
    return 0 < viewWidth && viewWidth < platformDeviceWidthOverride();
}

bool ViewportConfiguration::shouldIgnoreHorizontalScalingConstraints() const
{
    if (!m_canIgnoreScalingConstraints)
        return false;

    if (shouldOverrideDeviceWidthAndShrinkToFit())
        return true;

    if (!m_configuration.allowsShrinkToFit)
        return false;

    bool laidOutWiderThanViewport = m_contentSize.width() > layoutWidth();
    if (m_viewportArguments.width == ViewportArguments::ValueDeviceWidth)
        return laidOutWiderThanViewport;

    if (m_configuration.initialScaleIsSet && m_configuration.initialScaleIgnoringLayoutScaleFactor == 1)
        return laidOutWiderThanViewport;

    return false;
}

bool ViewportConfiguration::shouldIgnoreVerticalScalingConstraints() const
{
    if (!m_canIgnoreScalingConstraints)
        return false;

    if (!m_configuration.allowsShrinkToFit)
        return false;

    bool laidOutTallerThanViewport = m_contentSize.height() > layoutHeight();
    if (m_viewportArguments.height == ViewportArguments::ValueDeviceHeight && m_viewportArguments.width == ViewportArguments::ValueAuto)
        return laidOutTallerThanViewport;

    return false;
}

bool ViewportConfiguration::shouldIgnoreScalingConstraints() const
{
    return shouldIgnoreHorizontalScalingConstraints() || shouldIgnoreVerticalScalingConstraints();
}

bool ViewportConfiguration::shouldIgnoreScalingConstraintsRegardlessOfContentSize() const
{
    return m_canIgnoreScalingConstraints && shouldOverrideDeviceWidthAndShrinkToFit();
}

double ViewportConfiguration::initialScaleFromSize(double width, double height, bool shouldIgnoreScalingConstraints) const
{
    ASSERT(!constraintsAreAllRelative(m_configuration));

    auto clampToMinimumAndMaximumScales = [&] (double initialScale) {
        return clampTo<double>(initialScale, shouldIgnoreScalingConstraints ? m_defaultConfiguration.minimumScale : m_configuration.minimumScale, m_configuration.maximumScale);
    };

    if (layoutSizeIsExplicitlyScaled()) {
        if (m_configuration.initialScaleIsSet)
            return clampToMinimumAndMaximumScales(m_configuration.initialScale);

        if (m_configuration.width > 0)
            return clampToMinimumAndMaximumScales(m_viewLayoutSize.width() / m_configuration.width);
    }

    // If the document has specified its own initial scale, use it regardless.
    // This is guaranteed to be sanity checked already, so no need for MIN/MAX.
    if (m_configuration.initialScaleIsSet && !shouldIgnoreScalingConstraints)
        return m_configuration.initialScale;

    // If not, it is up to us to determine the initial scale.
    // We want a scale small enough to fit the document width-wise.
    double initialScale = 0;
    if (!shouldIgnoreVerticalScalingConstraints()) {
        static const double maximumContentWidthBeforePreferringExplicitWidthToAvoidExcessiveScaling = 1920;
        if (width > maximumContentWidthBeforePreferringExplicitWidthToAvoidExcessiveScaling && m_configuration.widthIsSet && 0 < m_configuration.width && m_configuration.width < width)
            initialScale = m_viewLayoutSize.width() / m_configuration.width;
        else if (shouldShrinkToFitMinimumEffectiveDeviceWidthWhenIgnoringScalingConstraints())
            initialScale = effectiveLayoutSizeScaleFactor();
        else if (width > 0) {
            auto shrinkToFitWidth = m_viewLayoutSize.width();
            if (m_prefersHorizontalScrollingBelowDesktopViewportWidths)
                shrinkToFitWidth = std::max<float>(shrinkToFitWidth, std::min(width, minimumShrinkToFitWidthWhenPreferringHorizontalScrolling));
            initialScale = shrinkToFitWidth / width;
        }
    }

    // Prevent the initial scale from shrinking to a height smaller than our view's minimum height.
    if (height > 0 && height * initialScale < m_viewLayoutSize.height() && !shouldIgnoreHorizontalScalingConstraints())
        initialScale = m_viewLayoutSize.height() / height;

    return clampToMinimumAndMaximumScales(initialScale);
}

double ViewportConfiguration::initialScale() const
{
    return initialScaleFromSize(m_contentSize.width() > 0 ? m_contentSize.width() : layoutWidth(), m_contentSize.height() > 0 ? m_contentSize.height() : layoutHeight(), shouldIgnoreScalingConstraints());
}

double ViewportConfiguration::initialScaleIgnoringContentSize() const
{
    return initialScaleFromSize(layoutWidth(), layoutHeight(), shouldIgnoreScalingConstraintsRegardlessOfContentSize());
}

double ViewportConfiguration::minimumScale() const
{
    // If we scale to fit, then this is our minimum scale as well.
    if (!m_configuration.initialScaleIsSet || shouldIgnoreScalingConstraints())
        return initialScale();

    // If not, we still need to sanity check our value.
    double minimumScale = m_configuration.minimumScale;
    
    if (m_forceAlwaysUserScalable)
        minimumScale = std::min(minimumScale, forceAlwaysUserScalableMinimumScale());

    if (m_configuration.minimumScaleDoesNotAdaptToContent)
        return minimumScale;

    auto scaleForFittingContentIsApproximatelyEqualToMinimumScale = [] (double viewLength, double contentLength, double minimumScale) {
        if (contentLength <= 1 || viewLength <= 1)
            return false;

        if (minimumScale < (viewLength - 0.5) / (contentLength + 0.5))
            return false;

        if (minimumScale > (viewLength + 0.5) / (contentLength - 0.5))
            return false;

        return true;
    };

    double contentWidth = m_contentSize.width();
    if (contentWidth > 0 && contentWidth * minimumScale < m_viewLayoutSize.width() && !shouldIgnoreVerticalScalingConstraints()) {
        if (!scaleForFittingContentIsApproximatelyEqualToMinimumScale(m_viewLayoutSize.width(), contentWidth, minimumScale))
            minimumScale = m_viewLayoutSize.width() / contentWidth;
    }

    double contentHeight = m_contentSize.height();
    if (contentHeight > 0 && contentHeight * minimumScale < m_viewLayoutSize.height() && !shouldIgnoreHorizontalScalingConstraints()) {
        if (!scaleForFittingContentIsApproximatelyEqualToMinimumScale(m_viewLayoutSize.height(), contentHeight, minimumScale))
            minimumScale = m_viewLayoutSize.height() / contentHeight;
    }

    minimumScale = std::min(std::max(minimumScale, m_configuration.minimumScale), m_configuration.maximumScale);

    return minimumScale;
}

bool ViewportConfiguration::allowsUserScaling() const
{
    return m_forceAlwaysUserScalable || allowsUserScalingIgnoringAlwaysScalable();
}
    
bool ViewportConfiguration::allowsUserScalingIgnoringAlwaysScalable() const
{
    return shouldIgnoreScalingConstraints() || m_configuration.allowsUserScaling;
}

ViewportConfiguration::Parameters ViewportConfiguration::nativeWebpageParameters()
{
    if (m_canIgnoreScalingConstraints || !shouldIgnoreMinimumEffectiveDeviceWidthForShrinkToFit())
        return ViewportConfiguration::nativeWebpageParametersWithShrinkToFit();

    return ViewportConfiguration::nativeWebpageParametersWithoutShrinkToFit();
}

ViewportConfiguration::Parameters ViewportConfiguration::nativeWebpageParametersWithoutShrinkToFit()
{
    Parameters parameters;
    parameters.width = ViewportArguments::ValueDeviceWidth;
    parameters.widthIsSet = true;
    parameters.allowsUserScaling = true;
    parameters.allowsShrinkToFit = false;
    parameters.minimumScale = 1;
    parameters.maximumScale = 5;
    parameters.initialScale = 1;
    parameters.initialScaleIgnoringLayoutScaleFactor = 1;
    parameters.initialScaleIsSet = true;
    return parameters;
}

ViewportConfiguration::Parameters ViewportConfiguration::nativeWebpageParametersWithShrinkToFit()
{
    Parameters parameters = ViewportConfiguration::nativeWebpageParametersWithoutShrinkToFit();
    parameters.allowsShrinkToFit = true;
    parameters.minimumScale = platformMinimumScaleForWebpage();
    parameters.initialScaleIsSet = false;
    return parameters;
}

ViewportConfiguration::Parameters ViewportConfiguration::webpageParameters()
{
    Parameters parameters;
    parameters.width = defaultDesktopViewportWidth;
    parameters.widthIsSet = true;
    parameters.allowsUserScaling = true;
    parameters.allowsShrinkToFit = true;
    parameters.minimumScale = platformMinimumScaleForWebpage();
    parameters.maximumScale = 5;
    return parameters;
}

ViewportConfiguration::Parameters ViewportConfiguration::textDocumentParameters()
{
    Parameters parameters;

#if PLATFORM(IOS_FAMILY)
    parameters.width = static_cast<int>(screenSize().width());
#else
    // FIXME: this needs to be unified with ViewportArguments on all ports.
    parameters.width = 320;
#endif

    parameters.widthIsSet = true;
    parameters.allowsUserScaling = true;
    parameters.allowsShrinkToFit = false;
    parameters.minimumScale = 0.25;
    parameters.maximumScale = 5;
    return parameters;
}

ViewportConfiguration::Parameters ViewportConfiguration::imageDocumentParameters()
{
    Parameters parameters;
    parameters.width = defaultDesktopViewportWidth;
    parameters.widthIsSet = true;
    parameters.allowsUserScaling = true;
    parameters.allowsShrinkToFit = false;
    parameters.minimumScale = 0.01;
    parameters.maximumScale = 5;
    return parameters;
}

ViewportConfiguration::Parameters ViewportConfiguration::xhtmlMobileParameters()
{
    Parameters parameters = webpageParameters();
    parameters.width = 320;
    return parameters;
}

ViewportConfiguration::Parameters ViewportConfiguration::testingParameters()
{
    Parameters parameters;
    parameters.initialScale = 1;
    parameters.initialScaleIgnoringLayoutScaleFactor = 1;
    parameters.initialScaleIsSet = true;
    parameters.allowsShrinkToFit = true;
    parameters.minimumScale = 1;
    parameters.maximumScale = 5;
    return parameters;
}

static inline bool applyViewportArgument(double& value, float viewportArgumentValue, float minimum, float maximum)
{
    if (viewportArgumentValueIsValid(viewportArgumentValue)) {
        value = std::min(maximum, std::max(minimum, viewportArgumentValue));
        return true;
    }

    return false;
}

static inline bool booleanViewportArgumentIsSet(float value)
{
    return !value || value == 1;
}

void ViewportConfiguration::updateConfiguration()
{
    m_configuration = m_defaultConfiguration;

    const double minimumViewportArgumentsScaleFactor = 0.1;
    const double maximumViewportArgumentsScaleFactor = 10.0;

    auto effectiveLayoutScale = effectiveLayoutSizeScaleFactor();

    if (layoutSizeIsExplicitlyScaled())
        m_configuration.width /= effectiveLayoutScale;

    applyViewportArgument(m_configuration.minimumScale, m_viewportArguments.minZoom, minimumViewportArgumentsScaleFactor, maximumViewportArgumentsScaleFactor);
    applyViewportArgument(m_configuration.maximumScale, m_viewportArguments.maxZoom, m_configuration.minimumScale, maximumViewportArgumentsScaleFactor);

    bool viewportArgumentsOverridesInitialScale = applyViewportArgument(m_configuration.initialScale, m_viewportArguments.zoom, m_configuration.minimumScale, m_configuration.maximumScale);

    double minimumViewportArgumentsDimension = 10;
    double maximumViewportArgumentsDimension = 10000;

    auto viewportArgumentsOverridesWidth = applyViewportArgument(m_configuration.width, viewportArgumentsLength(m_viewportArguments.width), minimumViewportArgumentsDimension, maximumViewportArgumentsDimension);
    auto viewportArgumentsOverridesHeight = applyViewportArgument(m_configuration.height, viewportArgumentsLength(m_viewportArguments.height), minimumViewportArgumentsDimension, maximumViewportArgumentsDimension);

    if (viewportArgumentsOverridesInitialScale || viewportArgumentsOverridesWidth || viewportArgumentsOverridesHeight) {
        m_configuration.initialScaleIsSet = viewportArgumentsOverridesInitialScale;
        m_configuration.widthIsSet = viewportArgumentsOverridesWidth;
        m_configuration.heightIsSet = viewportArgumentsOverridesHeight;
    }

    if (!m_configuration.shouldHonorMinimumEffectiveDeviceWidthFromClient && std::exchange(m_minimumEffectiveDeviceWidthWasSetByClient, false))
        m_minimumEffectiveDeviceWidthForView = 0;

    if (m_configuration.initialScaleIsSet && m_minimumEffectiveDeviceWidthForView > m_viewLayoutSize.width())
        m_configuration.ignoreInitialScaleForLayoutWidth = true;

    if (booleanViewportArgumentIsSet(m_viewportArguments.userZoom))
        m_configuration.allowsUserScaling = m_viewportArguments.userZoom != 0.;

    if (shouldOverrideShrinkToFitArgument())
        m_configuration.allowsShrinkToFit = shouldOverrideDeviceWidthAndShrinkToFit();
    else if (booleanViewportArgumentIsSet(m_viewportArguments.shrinkToFit))
        m_configuration.allowsShrinkToFit = m_viewportArguments.shrinkToFit != 0.;

    if (canOverrideConfigurationParameters()) {
        if (!viewportArgumentsOverridesWidth)
            m_configuration.width = m_minimumLayoutSize.width();
        else if (layoutSizeIsExplicitlyScaled() && m_viewportArguments.width > 0)
            m_configuration.width /= effectiveLayoutScale;
    }

    m_configuration.avoidsUnsafeArea = m_viewportArguments.viewportFit != ViewportFit::Cover;
    m_configuration.initialScaleIgnoringLayoutScaleFactor = m_configuration.initialScale;
    m_configuration.initialScale *= effectiveLayoutScale;
    m_configuration.minimumScale *= effectiveLayoutScale;
    m_configuration.maximumScale *= effectiveLayoutScale;

    LOG_WITH_STREAM(Viewports, stream << "ViewportConfiguration " << this << " updateConfiguration " << *this << " gives initial scale " << initialScale() << " based on contentSize " << m_contentSize << " and layout size " << layoutWidth() << "x" << layoutHeight());
}

void ViewportConfiguration::updateMinimumLayoutSize()
{
    m_minimumLayoutSize = m_viewLayoutSize / effectiveLayoutSizeScaleFactor();

    if (!shouldOverrideDeviceWidthAndShrinkToFit())
        return;

    float minDeviceWidth = platformDeviceWidthOverride();
    m_minimumLayoutSize = FloatSize(minDeviceWidth, std::roundf(m_minimumLayoutSize.height() * (minDeviceWidth / m_minimumLayoutSize.width())));
}

double ViewportConfiguration::viewportArgumentsLength(double length) const
{
    if (length == ViewportArguments::ValueDeviceWidth)
        return m_minimumLayoutSize.width();
    if (length == ViewportArguments::ValueDeviceHeight)
        return m_minimumLayoutSize.height();
    return length;
}

int ViewportConfiguration::layoutWidth() const
{
    ASSERT(!constraintsAreAllRelative(m_configuration));

    const FloatSize& minimumLayoutSize = m_minimumLayoutSize;
    if (m_configuration.widthIsSet) {
        // If we scale to fit, then accept the viewport width with sanity checking.
        if (!m_configuration.initialScaleIsSet || m_configuration.ignoreInitialScaleForLayoutWidth) {
            double maximumScale = this->maximumScale();
            double maximumContentWidthInViewportCoordinate = maximumScale * m_configuration.width;
            if (maximumContentWidthInViewportCoordinate < minimumLayoutSize.width()) {
                // The content zoomed to maxScale does not fit the view. Return the minimum width
                // satisfying the constraint maximumScale.
                return std::round(minimumLayoutSize.width() / maximumScale);
            }
            return std::round(std::max(m_configuration.width, m_minimumEffectiveDeviceWidthForView));
        }

        // If not, make sure the viewport width and initial scale can co-exist.
        double initialContentWidthInViewportCoordinate = m_configuration.width * m_configuration.initialScaleIgnoringLayoutScaleFactor;
        if (initialContentWidthInViewportCoordinate < minimumLayoutSize.width()) {
            // The specified width does not fit in viewport. Return the minimum width that satisfy the initialScale constraint.
            return std::round(minimumLayoutSize.width() / m_configuration.initialScaleIgnoringLayoutScaleFactor);
        }
        return std::round(m_configuration.width);
    }

    // If the page has a real scale, then just return the minimum size over the initial scale.
    if (m_configuration.initialScaleIsSet && !m_configuration.heightIsSet)
        return std::round(minimumLayoutSize.width() / m_configuration.initialScaleIgnoringLayoutScaleFactor);

    if (minimumLayoutSize.height() > 0)
        return std::round(minimumLayoutSize.width() * layoutHeight() / minimumLayoutSize.height());
    return minimumLayoutSize.width();
}

int ViewportConfiguration::layoutHeight() const
{
    ASSERT(!constraintsAreAllRelative(m_configuration));

    const FloatSize& minimumLayoutSize = m_minimumLayoutSize;
    if (m_configuration.heightIsSet) {
        // If we scale to fit, then accept the viewport height with sanity checking.
        if (!m_configuration.initialScaleIsSet) {
            double maximumScale = this->maximumScale();
            double maximumContentHeightInViewportCoordinate = maximumScale * m_configuration.height;
            if (maximumContentHeightInViewportCoordinate < minimumLayoutSize.height()) {
                // The content zoomed to maxScale does not fit the view. Return the minimum height that
                // satisfy the constraint maximumScale.
                return std::round(minimumLayoutSize.height() / maximumScale);
            }
            return std::round(m_configuration.height);
        }

        // If not, make sure the viewport width and initial scale can co-exist.
        double initialContentHeightInViewportCoordinate = m_configuration.height * m_configuration.initialScaleIgnoringLayoutScaleFactor;
        if (initialContentHeightInViewportCoordinate < minimumLayoutSize.height()) {
            // The specified width does not fit in viewport. Return the minimum height that satisfy the initialScale constraint.
            return std::round(minimumLayoutSize.height() / m_configuration.initialScaleIgnoringLayoutScaleFactor);
        }
        return std::round(m_configuration.height);
    }

    // If the page has a real scale, then just return the minimum size over the initial scale.
    if (m_configuration.initialScaleIsSet && !m_configuration.widthIsSet)
        return std::round(minimumLayoutSize.height() / m_configuration.initialScaleIgnoringLayoutScaleFactor);

    if (minimumLayoutSize.width() > 0)
        return std::round(minimumLayoutSize.height() * layoutWidth() / minimumLayoutSize.width());
    return minimumLayoutSize.height();
}

bool ViewportConfiguration::setMinimumEffectiveDeviceWidthForShrinkToFit(double width)
{
    if (WTF::areEssentiallyEqual(m_minimumEffectiveDeviceWidthForShrinkToFit, width))
        return false;

    m_minimumEffectiveDeviceWidthForShrinkToFit = width;

    if (shouldIgnoreMinimumEffectiveDeviceWidthForShrinkToFit())
        return false;

    updateMinimumLayoutSize();
    updateConfiguration();
    return true;
}

bool ViewportConfiguration::setMinimumEffectiveDeviceWidthWhenIgnoringScalingConstraints(double width)
{
    if (WTF::areEssentiallyEqual(m_minimumEffectiveDeviceWidthWhenIgnoringScalingConstraints, width))
        return false;

    bool wasShrinkingToFitMinimumEffectiveDeviceWidth = shouldShrinkToFitMinimumEffectiveDeviceWidthWhenIgnoringScalingConstraints();
    m_minimumEffectiveDeviceWidthWhenIgnoringScalingConstraints = width;
    if (wasShrinkingToFitMinimumEffectiveDeviceWidth == shouldShrinkToFitMinimumEffectiveDeviceWidthWhenIgnoringScalingConstraints())
        return false;

    updateMinimumLayoutSize();
    updateConfiguration();
    return true;
}

bool ViewportConfiguration::setIsKnownToLayOutWiderThanViewport(bool value)
{
    if (m_isKnownToLayOutWiderThanViewport == value)
        return false;

    m_isKnownToLayOutWiderThanViewport = value;
    updateMinimumLayoutSize();
    updateConfiguration();
    return true;
}

TextStream& operator<<(TextStream& ts, const ViewportConfiguration::Parameters& parameters)
{
    ts.startGroup();
    ts << "width "_s << parameters.width << ", set: "_s << (parameters.widthIsSet ? "true"_s : "false"_s);
    ts.endGroup();

    ts.startGroup();
    ts << "height "_s << parameters.height << ", set: "_s << (parameters.heightIsSet ? "true"_s : "false"_s);
    ts.endGroup();

    ts.startGroup();
    ts << "initialScale "_s << parameters.initialScale << ", set: "_s << (parameters.initialScaleIsSet ? "true"_s : "false"_s);
    ts.endGroup();

    ts.dumpProperty("initialScaleIgnoringLayoutScaleFactor"_s, parameters.initialScaleIgnoringLayoutScaleFactor);
    ts.dumpProperty("minimumScale"_s, parameters.minimumScale);
    ts.dumpProperty("maximumScale"_s, parameters.maximumScale);
    ts.dumpProperty("allowsUserScaling"_s, parameters.allowsUserScaling);
    ts.dumpProperty("allowsShrinkToFit"_s, parameters.allowsShrinkToFit);
    ts.dumpProperty("avoidsUnsafeArea"_s, parameters.avoidsUnsafeArea);
    ts.dumpProperty("ignoreInitialScaleForLayoutWidth"_s, parameters.ignoreInitialScaleForLayoutWidth);
    ts.dumpProperty("shouldHonorMinimumEffectiveDeviceWidthFromClient"_s, parameters.shouldHonorMinimumEffectiveDeviceWidthFromClient);
    ts.dumpProperty("minimumScaleDoesNotAdaptToContent"_s, parameters.minimumScaleDoesNotAdaptToContent);

    return ts;
}

TextStream& operator<<(TextStream& ts, const ViewportConfiguration& config)
{
    return ts << config.description();
}

String ViewportConfiguration::description() const
{
    TextStream ts;

    ts.startGroup();
    ts << "viewport-configuration "_s << (void*)this;
    {
        TextStream::GroupScope scope(ts);
        ts << "viewport arguments"_s;
        ts << m_viewportArguments;
    }
    {
        TextStream::GroupScope scope(ts);
        ts << "configuration"_s;
        ts << m_configuration;
    }
    {
        TextStream::GroupScope scope(ts);
        ts << "default configuration"_s;
        ts << m_defaultConfiguration;
    }

    ts.dumpProperty("contentSize"_s, m_contentSize);
    ts.dumpProperty("minimumLayoutSize"_s, m_minimumLayoutSize);
    ts.dumpProperty("layoutSizeScaleFactor"_s, m_layoutSizeScaleFactor);
    ts.dumpProperty("computed initial scale"_s, initialScale());
    ts.dumpProperty("computed minimum scale"_s, minimumScale());
    ts.dumpProperty("computed layout size"_s, layoutSize());
    ts.dumpProperty("ignoring horizontal scaling constraints"_s, shouldIgnoreHorizontalScalingConstraints() ? "true"_s : "false"_s);
    ts.dumpProperty("ignoring vertical scaling constraints"_s, shouldIgnoreVerticalScalingConstraints() ? "true"_s : "false"_s);
    ts.dumpProperty("avoids unsafe area"_s, avoidsUnsafeArea() ? "true"_s : "false"_s);
    ts.dumpProperty("minimum effective device width (for view)"_s, m_minimumEffectiveDeviceWidthForView);
    ts.dumpProperty("minimum effective device width (for shrink-to-fit)"_s, m_minimumEffectiveDeviceWidthForShrinkToFit);
    ts.dumpProperty("known to lay out wider than viewport"_s, m_isKnownToLayOutWiderThanViewport ? "true"_s : "false"_s);
    ts.dumpProperty("prefers horizontal scrolling"_s, m_prefersHorizontalScrollingBelowDesktopViewportWidths ? "true"_s : "false"_s);
    ts.dumpProperty("can ignore viewport width and zoom"_s, m_canIgnoreViewportArgumentsToAvoidExcessiveZoom ? "true"_s : "false"_s);
    ts.dumpProperty("can ignore viewport width"_s, m_canIgnoreViewportArgumentsToAvoidEnlargedView ? "true"_s : "false"_s);

    ts.endGroup();

    return ts.release();
}

#if !LOG_DISABLED

void ViewportConfiguration::dump() const
{
    WTFLogAlways("%s", description().utf8().data());
}

#endif

} // namespace WebCore
