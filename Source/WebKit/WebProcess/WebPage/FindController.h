/*
 * Copyright (C) 2010-2017 Apple Inc. All rights reserved.
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

#pragma once

#include "WebFindOptions.h"
#include <WebCore/FindOptions.h>
#include <WebCore/FrameIdentifier.h>
#include <WebCore/IntRect.h>
#include <WebCore/PageOverlay.h>
#include <WebCore/ShareableBitmap.h>
#include <WebCore/SimpleRange.h>
#include <wtf/CompletionHandler.h>
#include <wtf/Forward.h>
#include <wtf/Noncopyable.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/Vector.h>

#if PLATFORM(IOS_FAMILY)
#include "FindIndicatorOverlayClientIOS.h"
#endif

namespace WebCore {
class LocalFrame;
class Range;
enum class DidWrap : bool;
}

namespace WebKit {

class CallbackID;
class PluginView;
class WebPage;

class FindController final : private WebCore::PageOverlayClient {
    WTF_MAKE_TZONE_ALLOCATED(FindController);
    WTF_MAKE_NONCOPYABLE(FindController);

public:
    enum class TriggerImageAnalysis : bool { No, Yes };

    explicit FindController(WebPage*);
    virtual ~FindController();

    void findString(const String&, OptionSet<FindOptions>, unsigned maxMatchCount, CompletionHandler<void(std::optional<WebCore::FrameIdentifier>, Vector<WebCore::IntRect>&&, uint32_t, int32_t, bool)>&&);
#if ENABLE(IMAGE_ANALYSIS)
    void findStringIncludingImages(const String&, OptionSet<FindOptions>, unsigned maxMatchCount, CompletionHandler<void(std::optional<WebCore::FrameIdentifier>, Vector<WebCore::IntRect>&&, uint32_t, int32_t, bool)>&&);
#endif
    void findStringMatches(const String&, OptionSet<FindOptions>, unsigned maxMatchCount, CompletionHandler<void(Vector<Vector<WebCore::IntRect>>, int32_t)>&&);
    void findRectsForStringMatches(const String&, OptionSet<WebKit::FindOptions>, unsigned maxMatchCount, CompletionHandler<void(Vector<WebCore::FloatRect>&&)>&&);
    void getImageForFindMatch(uint32_t matchIndex);
    void selectFindMatch(uint32_t matchIndex);
    void indicateFindMatch(uint32_t matchIndex);
    void hideFindUI();
    void countStringMatches(const String&, OptionSet<FindOptions>, unsigned maxMatchCount, CompletionHandler<void(uint32_t)>&&);
    uint32_t replaceMatches(const Vector<uint32_t>& matchIndices, const String& replacementText, bool selectionOnly);
    
    void hideFindIndicator();
    void resetMatchIndex();
    void showFindIndicatorInSelection();

    bool isShowingOverlay() const { return m_isShowingFindIndicator && m_findPageOverlay; }

    void deviceScaleFactorDidChange();
    void didInvalidateFindRects();

    void redraw();

private:
    // PageOverlayClient.
    void willMoveToPage(WebCore::PageOverlay&, WebCore::Page*) override;
    void didMoveToPage(WebCore::PageOverlay&, WebCore::Page*) override;
    bool mouseEvent(WebCore::PageOverlay&, const WebCore::PlatformMouseEvent&) override;
    void drawRect(WebCore::PageOverlay&, WebCore::GraphicsContext&, const WebCore::IntRect& dirtyRect) override;

    Vector<WebCore::FloatRect> rectsForTextMatchesInRect(WebCore::IntRect clipRect);
    bool updateFindIndicator(bool isShowingOverlay, bool shouldAnimate = true);

    void updateFindUIAfterPageScroll(bool found, const String&, OptionSet<FindOptions>, unsigned maxMatchCount, WebCore::DidWrap, std::optional<WebCore::FrameIdentifier>, CompletionHandler<void(std::optional<WebCore::FrameIdentifier>, Vector<WebCore::IntRect>&&, uint32_t, int32_t, bool)>&& = [](auto&&...) { });

    void willFindString();
    void didFindString();
    void didFailToFindString();
    void didHideFindIndicator();
    
    unsigned findIndicatorRadius() const;
    bool shouldHideFindIndicatorOnScroll() const;
    void didScrollAffectingFindIndicatorPosition();

    RefPtr<WebCore::LocalFrame> frameWithSelection(WebCore::Page*);

    RefPtr<WebPage> protectedWebPage() const;

#if ENABLE(PDF_PLUGIN)
    PluginView* mainFramePlugIn();
#endif

    const WeakPtr<WebPage> m_webPage;
    WeakPtr<WebCore::PageOverlay> m_findPageOverlay;

    // Whether the UI process is showing the find indicator. Note that this can be true even if
    // the find indicator isn't showing, but it will never be false when it is showing.
    bool m_isShowingFindIndicator { false };
    WebCore::IntRect m_findIndicatorRect;
    Vector<WebCore::SimpleRange> m_findMatches;
    // Index value is -1 if not found or if number of matches exceeds provided maximum.
    int m_foundStringMatchIndex { -1 };

#if PLATFORM(IOS_FAMILY)
    RefPtr<WebCore::PageOverlay> m_findIndicatorOverlay;
    std::unique_ptr<FindIndicatorOverlayClientIOS> m_findIndicatorOverlayClient;
#endif
};

} // namespace WebKit
